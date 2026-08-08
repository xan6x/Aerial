#include "Render/Overlay.h"

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#include <atomic>

#include "Render/D3DRenderer.h"
#include "Render/Images.h"
#include "Render/MotionBlur.h"
#include "Utils/Guard.h"
#include "Utils/Hook.h"
#include "Utils/Logger.h"

namespace aerial::render {
namespace {

constexpr int kPresentIndex = 8;
constexpr int kResizeBuffersIndex = 13;
constexpr int kPresent1Index = 22;

using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(__stdcall*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using ResizeBuffersFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

Detour<PresentFn> g_present;
Detour<Present1Fn> g_present1;
Detour<ResizeBuffersFn> g_resizeBuffers;

std::atomic<uint64_t> g_presentCalls{0};

template <typename T>
void release(T*& object) {
    if (object) {
        object->Release();
        object = nullptr;
    }
}

}

struct OverlayHooks {
    static HRESULT __stdcall present(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
        guarded("motion blur", [&] { MotionBlur::get().onPresent(swapChain); });
        guarded("overlay present", [&] { Overlay::get().onPresent(swapChain); });
        return g_present.call(swapChain, syncInterval, flags);
    }

    static HRESULT __stdcall present1(IDXGISwapChain1* swapChain, UINT syncInterval, UINT flags,
                                      const DXGI_PRESENT_PARAMETERS* parameters) {
        auto* chain = reinterpret_cast<IDXGISwapChain*>(swapChain);
        guarded("motion blur", [&] { MotionBlur::get().onPresent(chain); });
        guarded("overlay present1", [&] { Overlay::get().onPresent(chain); });
        return g_present1.call(swapChain, syncInterval, flags, parameters);
    }

    static HRESULT __stdcall resizeBuffers(IDXGISwapChain* swapChain, UINT bufferCount, UINT width,
                                           UINT height, DXGI_FORMAT format, UINT flags) {
        MotionBlur::get().onResize();
        Overlay::get().onResize();
        return g_resizeBuffers.call(swapChain, bufferCount, width, height, format, flags);
    }
};

Overlay& Overlay::get() {
    static Overlay instance;
    return instance;
}

void Overlay::setFrameCallback(std::function<void()> callback) {
    m_frameCallback = std::move(callback);
}

bool Overlay::install() {
    if (m_installed)
        return true;

    IDXGIFactory2* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory)))) {
        m_status = "CreateDXGIFactory1 failed";
        LOG_ERROR("D3D", "{}", m_status);
        return false;
    }

    ID3D11Device* device = nullptr;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                        D3D_FEATURE_LEVEL_10_0};

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels,
                                   ARRAYSIZE(levels), D3D11_SDK_VERSION, &device, nullptr, nullptr);
    if (FAILED(hr)) {
        release(factory);
        m_status = "D3D11CreateDevice failed";
        LOG_ERROR("D3D", "{} ({:#x})", m_status, static_cast<uint32_t>(hr));
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = 16;
    desc.Height = 16;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    IDXGISwapChain1* dummy = nullptr;
    hr = factory->CreateSwapChainForComposition(device, &desc, nullptr, &dummy);
    if (FAILED(hr)) {
        release(device);
        release(factory);
        m_status = "CreateSwapChainForComposition failed";
        LOG_ERROR("D3D", "{} ({:#x})", m_status, static_cast<uint32_t>(hr));
        return false;
    }

    auto** vtable = *reinterpret_cast<void***>(dummy);
    const auto presentAddress = reinterpret_cast<uintptr_t>(vtable[kPresentIndex]);
    const auto present1Address = reinterpret_cast<uintptr_t>(vtable[kPresent1Index]);
    const auto resizeAddress = reinterpret_cast<uintptr_t>(vtable[kResizeBuffersIndex]);

    release(dummy);
    release(device);
    release(factory);

    const bool hooked =
        g_present.attach("IDXGISwapChain::Present", presentAddress, &OverlayHooks::present) &&
        g_present1.attach("IDXGISwapChain1::Present1", present1Address, &OverlayHooks::present1) &&
        g_resizeBuffers.attach("IDXGISwapChain::ResizeBuffers", resizeAddress,
                               &OverlayHooks::resizeBuffers);
    if (!hooked) {
        m_status = "could not detour Present";
        LOG_ERROR("D3D", "{}", m_status);
        return false;
    }

    m_installed = true;
    m_status = "waiting for the first frame";
    LOG_INFO("D3D", "swap chain hooked (Present {:#x}, Present1 {:#x})", presentAddress,
             present1Address);
    return true;
}

ID3D11RenderTargetView* Overlay::currentTarget(IDXGISwapChain* swapChain) {
    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                    reinterpret_cast<void**>(&backBuffer))))
        return nullptr;

    D3D11_TEXTURE2D_DESC desc{};
    backBuffer->GetDesc(&desc);
    m_size = {static_cast<float>(desc.Width), static_cast<float>(desc.Height)};

    if (backBuffer == m_targetTexture && m_target) {
        release(backBuffer);
        return m_target;
    }

    release(m_target);
    if (FAILED(m_device->CreateRenderTargetView(backBuffer, nullptr, &m_target))) {
        release(backBuffer);
        m_targetTexture = nullptr;
        return nullptr;
    }

    m_targetTexture = backBuffer;
    release(backBuffer);
    return m_target;
}

void Overlay::releaseTarget() {
    m_ready = false;
    images::releaseAll();
    D3DRenderer::get().releaseDeviceResources();
    release(m_target);
    m_targetTexture = nullptr;
    release(m_context);
    release(m_device);
    m_chain = nullptr;
}

void Overlay::onResize() {
    m_ready = false;
    release(m_target);
    m_targetTexture = nullptr;
}

void Overlay::abandon(const char* why) {
    if (m_abandoned.exchange(true, std::memory_order_relaxed))
        return;

    m_status = why;
    LOG_WARN("D3D", "giving up on the overlay ({}); the game's renderer takes over. Toggle the "
                    "Overlay module off and on to try again.",
             why);
}

uint64_t Overlay::presentCount() const {
    return g_presentCalls.load(std::memory_order_relaxed);
}

void Overlay::onPresent(IDXGISwapChain* swapChain) {
    if (g_presentCalls.fetch_add(1, std::memory_order_relaxed) == 0)
        LOG_INFO("D3D", "first present intercepted, swap chain {}", static_cast<void*>(swapChain));

    if (!m_enabled || m_abandoned.load(std::memory_order_relaxed))
        return;

    ID3D11Device* device = nullptr;
    if (FAILED(swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device))) ||
        !device)
        return;

    if (device != m_device) {
        releaseTarget();
        m_device = device;
        m_device->GetImmediateContext(&m_context);
        m_chain = swapChain;
        ++m_generation;
        LOG_INFO("D3D", "overlay bound to device {} (generation {})", static_cast<void*>(m_device),
                 m_generation);
    } else {
        device->Release();
        if (swapChain != m_chain) {
            m_chain = swapChain;
            release(m_target);
            m_targetTexture = nullptr;
        }
    }

    ID3D11RenderTargetView* target = currentTarget(swapChain);
    if (!target || !m_frameCallback)
        return;

    auto& renderer = D3DRenderer::get();
    if (!renderer.beginFrame(m_device, m_context, target, m_size.x, m_size.y)) {
        m_ready = false;
        m_status = "renderer setup failed";
        return;
    }

    m_ready = true;
    m_status = "ready";
    m_frameCallback();
    renderer.endFrame();
}

void Overlay::shutdown() {
    releaseTarget();
    m_installed = false;
    m_status = "shut down";
}

}
