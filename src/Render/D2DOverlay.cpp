#include "Render/D2DOverlay.h"

#include <Windows.h>
#include <d2d1_1.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <dwrite.h>
#include <dxgi1_2.h>

#include <atomic>

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

constexpr UINT64 kMutexKey = 0;

using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(__stdcall*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using ResizeBuffersFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

Detour<PresentFn> g_present;
Detour<Present1Fn> g_present1;
Detour<ResizeBuffersFn> g_resizeBuffers;

std::atomic<uint64_t> g_presentCalls{0};

std::atomic<uint64_t> g_acquireFailures{0};
constexpr uint64_t kAcquireComplaint = 60;

// The game side taking the shared texture is the last thing between us and the
// screen, and it used to fail in silence: the overlay drew, the composite ran,
// and the one call that matters was skipped.
std::atomic<uint64_t> g_compositeFailures{0};
constexpr uint64_t kCompositeStall = 30;

// Rebuilding costs a device and a shared texture, so a target that keeps
// refusing draws gets a couple of tries and then we hand the frame back to the
// game's own renderer rather than churn every frame.
std::atomic<uint64_t> g_drawFailures{0};
constexpr uint64_t kDrawRetries = 3;

template <typename T>
void release(T*& object) {
    if (object) {
        object->Release();
        object = nullptr;
    }
}

constexpr char kShaderSource[] = R"(
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

VSOut vs_main(uint id : SV_VertexID) {
    VSOut output;
    output.uv  = float2((id << 1) & 2, id & 2);
    output.pos = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

Texture2D    overlayTexture : register(t0);
SamplerState overlaySampler : register(s0);

float4 ps_main(VSOut input) : SV_TARGET {
    return overlayTexture.Sample(overlaySampler, input.uv);
}
)";

}

struct GameResources {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ID3D11RenderTargetView* target = nullptr;
    ID3D11Texture2D* targetTexture = nullptr;
    ID3D11Texture2D* overlay = nullptr;
    ID3D11ShaderResourceView* overlayView = nullptr;
    IDXGIKeyedMutex* mutex = nullptr;
    ID3D11VertexShader* vertexShader = nullptr;
    ID3D11PixelShader* pixelShader = nullptr;
    ID3D11BlendState* blend = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11RasterizerState* rasteriser = nullptr;
    ID3D11DepthStencilState* depth = nullptr;

    void releaseAll() {
        release(target);
        targetTexture = nullptr;
        release(overlayView);
        release(overlay);
        release(mutex);
        release(vertexShader);
        release(pixelShader);
        release(blend);
        release(sampler);
        release(rasteriser);
        release(depth);
        release(context);
        release(device);
    }
};

namespace {
GameResources g_game;

ID3D11Device* g_ownDevice = nullptr;
ID3D11DeviceContext* g_ownContext = nullptr;
ID3D11Texture2D* g_sharedTexture = nullptr;
IDXGIKeyedMutex* g_sharedMutex = nullptr;
HANDLE g_sharedHandle = nullptr;

bool compileShaders() {
    ID3DBlob* vertexBlob = nullptr;
    ID3DBlob* pixelBlob = nullptr;
    ID3DBlob* errors = nullptr;

    HRESULT hr = D3DCompile(kShaderSource, sizeof(kShaderSource) - 1, nullptr, nullptr, nullptr,
                            "vs_main", "vs_4_0", 0, 0, &vertexBlob, &errors);
    if (FAILED(hr)) {
        LOG_ERROR("D2D", "vertex shader failed: {}",
                  errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
        release(errors);
        return false;
    }
    release(errors);

    hr = D3DCompile(kShaderSource, sizeof(kShaderSource) - 1, nullptr, nullptr, nullptr, "ps_main",
                    "ps_4_0", 0, 0, &pixelBlob, &errors);
    if (FAILED(hr)) {
        LOG_ERROR("D2D", "pixel shader failed: {}",
                  errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
        release(errors);
        release(vertexBlob);
        return false;
    }
    release(errors);

    hr = g_game.device->CreateVertexShader(vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(),
                                           nullptr, &g_game.vertexShader);
    if (SUCCEEDED(hr))
        hr = g_game.device->CreatePixelShader(pixelBlob->GetBufferPointer(), pixelBlob->GetBufferSize(),
                                              nullptr, &g_game.pixelShader);

    release(vertexBlob);
    release(pixelBlob);
    return SUCCEEDED(hr);
}

bool createStates() {

    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(g_game.device->CreateBlendState(&blend, &g_game.blend)))
        return false;

    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(g_game.device->CreateSamplerState(&sampler, &g_game.sampler)))
        return false;

    D3D11_RASTERIZER_DESC rasteriser{};
    rasteriser.FillMode = D3D11_FILL_SOLID;
    rasteriser.CullMode = D3D11_CULL_NONE;
    rasteriser.DepthClipEnable = FALSE;
    if (FAILED(g_game.device->CreateRasterizerState(&rasteriser, &g_game.rasteriser)))
        return false;

    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.StencilEnable = FALSE;
    return SUCCEEDED(g_game.device->CreateDepthStencilState(&depth, &g_game.depth));
}

}

struct D2DHooks {
    static HRESULT __stdcall present(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
        guarded("motion blur", [&] { MotionBlur::get().onPresent(swapChain); });
        guarded("D2D present", [&] { D2DOverlay::get().onPresent(swapChain); });
        return g_present.call(swapChain, syncInterval, flags);
    }

    static HRESULT __stdcall present1(IDXGISwapChain1* swapChain, UINT syncInterval, UINT flags,
                                      const DXGI_PRESENT_PARAMETERS* parameters) {
        auto* chain = reinterpret_cast<IDXGISwapChain*>(swapChain);
        guarded("motion blur", [&] { MotionBlur::get().onPresent(chain); });
        guarded("D2D present1", [&] { D2DOverlay::get().onPresent(chain); });
        return g_present1.call(swapChain, syncInterval, flags, parameters);
    }

    static HRESULT __stdcall resizeBuffers(IDXGISwapChain* swapChain, UINT bufferCount, UINT width,
                                           UINT height, DXGI_FORMAT format, UINT flags) {

        MotionBlur::get().onResize();
        D2DOverlay::get().onResize();
        return g_resizeBuffers.call(swapChain, bufferCount, width, height, format, flags);
    }
};

D2DOverlay& D2DOverlay::get() {
    static D2DOverlay instance;
    return instance;
}

void D2DOverlay::setFrameCallback(std::function<void()> callback) {
    m_frameCallback = std::move(callback);
}

bool D2DOverlay::install() {
    if (m_installed)
        return true;

    IDXGIFactory2* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory)))) {
        m_status = "CreateDXGIFactory1 failed";
        LOG_ERROR("D2D", "{}", m_status);
        return false;
    }

    ID3D11Device* device = nullptr;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                        D3D_FEATURE_LEVEL_10_0};

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels),
                                   D3D11_SDK_VERSION, &device, nullptr, nullptr);
    if (FAILED(hr)) {
        release(factory);
        m_status = "D3D11CreateDevice failed";
        LOG_ERROR("D2D", "{} ({:#x})", m_status, static_cast<uint32_t>(hr));
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
        LOG_ERROR("D2D", "{} ({:#x})", m_status, static_cast<uint32_t>(hr));
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
        g_present.attach("IDXGISwapChain::Present", presentAddress, &D2DHooks::present) &&
        g_present1.attach("IDXGISwapChain1::Present1", present1Address, &D2DHooks::present1) &&
        g_resizeBuffers.attach("IDXGISwapChain::ResizeBuffers", resizeAddress,
                               &D2DHooks::resizeBuffers);
    if (!hooked) {
        m_status = "could not detour Present";
        LOG_ERROR("D2D", "{}", m_status);
        return false;
    }

    m_installed = true;
    m_status = "waiting for the first frame";
    LOG_INFO("D2D", "swap chain hooked (Present {:#x}, Present1 {:#x})", presentAddress,
             present1Address);
    return true;
}

bool D2DOverlay::createTarget(IDXGISwapChain* swapChain) {

    if (FAILED(swapChain->GetDevice(__uuidof(ID3D11Device),
                                    reinterpret_cast<void**>(&g_game.device)))) {
        m_status = "swap chain has no D3D11 device";
        return false;
    }
    g_game.device->GetImmediateContext(&g_game.context);

    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                    reinterpret_cast<void**>(&backBuffer)))) {
        m_status = "GetBuffer(0) failed";
        return false;
    }

    D3D11_TEXTURE2D_DESC backDesc{};
    backBuffer->GetDesc(&backDesc);
    m_size = {static_cast<float>(backDesc.Width), static_cast<float>(backDesc.Height)};
    release(backBuffer);

    HRESULT hr = S_OK;

    IDXGIAdapter* adapter = nullptr;
    {
        IDXGIDevice* gameDxgi = nullptr;
        if (SUCCEEDED(g_game.device->QueryInterface(__uuidof(IDXGIDevice),
                                                    reinterpret_cast<void**>(&gameDxgi)))) {
            gameDxgi->GetAdapter(&adapter);
            release(gameDxgi);
        }
    }

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1};
    hr = D3D11CreateDevice(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
                           nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels),
                           D3D11_SDK_VERSION, &g_ownDevice, nullptr, &g_ownContext);
    release(adapter);
    if (FAILED(hr)) {
        m_status = "could not create a BGRA device";
        LOG_ERROR("D2D", "{} ({:#x})", m_status, static_cast<uint32_t>(hr));
        return false;
    }

    D3D11_TEXTURE2D_DESC shared{};
    shared.Width = backDesc.Width;
    shared.Height = backDesc.Height;
    shared.MipLevels = 1;
    shared.ArraySize = 1;
    shared.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    shared.SampleDesc.Count = 1;
    shared.Usage = D3D11_USAGE_DEFAULT;
    shared.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    shared.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

    if (FAILED(g_ownDevice->CreateTexture2D(&shared, nullptr, &g_sharedTexture))) {
        m_status = "shared texture creation failed";
        return false;
    }

    IDXGIResource* resource = nullptr;
    if (FAILED(g_sharedTexture->QueryInterface(__uuidof(IDXGIResource),
                                               reinterpret_cast<void**>(&resource))) ||
        FAILED(resource->GetSharedHandle(&g_sharedHandle))) {
        release(resource);
        m_status = "could not share the overlay texture";
        return false;
    }
    release(resource);

    if (FAILED(g_sharedTexture->QueryInterface(__uuidof(IDXGIKeyedMutex),
                                               reinterpret_cast<void**>(&g_sharedMutex)))) {
        m_status = "no keyed mutex on our side";
        return false;
    }

    if (FAILED(g_game.device->OpenSharedResource(g_sharedHandle, __uuidof(ID3D11Texture2D),
                                                 reinterpret_cast<void**>(&g_game.overlay)))) {
        m_status = "OpenSharedResource failed";
        return false;
    }
    if (FAILED(g_game.overlay->QueryInterface(__uuidof(IDXGIKeyedMutex),
                                              reinterpret_cast<void**>(&g_game.mutex)))) {
        m_status = "no keyed mutex on the game side";
        return false;
    }
    if (FAILED(g_game.device->CreateShaderResourceView(g_game.overlay, nullptr,
                                                       &g_game.overlayView))) {
        m_status = "CreateShaderResourceView failed";
        return false;
    }

    if (!compileShaders() || !createStates()) {
        m_status = "composite pipeline setup failed";
        return false;
    }

    IDXGIDevice* dxgiDevice = nullptr;
    if (FAILED(g_ownDevice->QueryInterface(__uuidof(IDXGIDevice),
                                           reinterpret_cast<void**>(&dxgiDevice)))) {
        m_status = "no IDXGIDevice";
        return false;
    }

    if (!m_factory) {
        D2D1_FACTORY_OPTIONS options{};
        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options,
                               reinterpret_cast<void**>(&m_factory));
        if (FAILED(hr)) {
            release(dxgiDevice);
            m_status = "D2D1CreateFactory failed";
            return false;
        }
    }

    ID2D1Device* d2dDevice = nullptr;
    hr = m_factory->CreateDevice(dxgiDevice, &d2dDevice);
    release(dxgiDevice);
    if (FAILED(hr)) {
        m_status = "ID2D1Factory1::CreateDevice failed";
        LOG_ERROR("D2D", "{} ({:#x})", m_status, static_cast<uint32_t>(hr));
        return false;
    }

    hr = d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_context);
    release(d2dDevice);
    if (FAILED(hr)) {
        m_status = "CreateDeviceContext failed";
        return false;
    }

    IDXGISurface* surface = nullptr;
    if (FAILED(g_sharedTexture->QueryInterface(__uuidof(IDXGISurface),
                                               reinterpret_cast<void**>(&surface)))) {
        m_status = "overlay texture is not a DXGI surface";
        return false;
    }

    const auto properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    ID2D1Bitmap1* bitmap = nullptr;
    hr = m_context->CreateBitmapFromDxgiSurface(surface, properties, &bitmap);
    release(surface);
    if (FAILED(hr)) {
        m_status = "CreateBitmapFromDxgiSurface failed";
        LOG_ERROR("D2D", "{} ({:#x})", m_status, static_cast<uint32_t>(hr));
        return false;
    }

    m_targetBitmap = bitmap;
    m_context->SetTarget(bitmap);
    m_context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    m_context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (!m_dwrite &&
        FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&m_dwrite)))) {
        m_status = "DWriteCreateFactory failed";
        return false;
    }

    m_chain = swapChain;
    ++m_generation;
    m_ready = true;
    m_status = "ready";
    LOG_INFO("D2D", "overlay ready: {}x{} on a private BGRA device", backDesc.Width, backDesc.Height);
    return true;
}

bool D2DOverlay::boundTo(IDXGISwapChain* swapChain) {
    if (swapChain != m_chain)
        return false;

    ID3D11Device* device = nullptr;
    if (FAILED(swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device))))
        return false;

    const bool same = device == g_game.device;
    release(device);
    return same;
}

ID3D11RenderTargetView* D2DOverlay::currentTarget(IDXGISwapChain* swapChain) {
    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                    reinterpret_cast<void**>(&backBuffer))))
        return nullptr;

    if (backBuffer == g_game.targetTexture && g_game.target) {
        release(backBuffer);
        return g_game.target;
    }

    release(g_game.target);
    if (FAILED(g_game.device->CreateRenderTargetView(backBuffer, nullptr, &g_game.target))) {
        release(backBuffer);
        g_game.targetTexture = nullptr;
        return nullptr;
    }

    g_game.targetTexture = backBuffer;
    release(backBuffer);
    return g_game.target;
}

void D2DOverlay::composite(IDXGISwapChain* swapChain) {
    static bool complained = false;

    if (!g_game.context || !g_game.overlayView) {
        if (!complained) {
            complained = true;
            LOG_WARN("D2D", "nothing to composite with: context {}, overlay view {}",
                     static_cast<void*>(g_game.context), static_cast<void*>(g_game.overlayView));
        }
        return;
    }

    ID3D11RenderTargetView* target = currentTarget(swapChain);
    if (!target) {
        if (!complained) {
            complained = true;
            LOG_WARN("D2D", "no render target for the game's back buffer; the overlay is drawing "
                            "into nothing");
        }
        return;
    }

    complained = false;

    auto* context = g_game.context;

    ID3D11RenderTargetView* savedTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ID3D11DepthStencilView* savedDepth = nullptr;
    context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedTargets, &savedDepth);

    ID3D11BlendState* savedBlend = nullptr;
    FLOAT savedBlendFactor[4]{};
    UINT savedSampleMask = 0;
    context->OMGetBlendState(&savedBlend, savedBlendFactor, &savedSampleMask);

    ID3D11DepthStencilState* savedDepthState = nullptr;
    UINT savedStencilRef = 0;
    context->OMGetDepthStencilState(&savedDepthState, &savedStencilRef);

    ID3D11RasterizerState* savedRasteriser = nullptr;
    context->RSGetState(&savedRasteriser);

    UINT viewportCount = 1;
    D3D11_VIEWPORT savedViewport{};
    context->RSGetViewports(&viewportCount, &savedViewport);

    ID3D11VertexShader* savedVertexShader = nullptr;
    ID3D11PixelShader* savedPixelShader = nullptr;
    context->VSGetShader(&savedVertexShader, nullptr, nullptr);
    context->PSGetShader(&savedPixelShader, nullptr, nullptr);

    ID3D11ShaderResourceView* savedResource = nullptr;
    context->PSGetShaderResources(0, 1, &savedResource);
    ID3D11SamplerState* savedSampler = nullptr;
    context->PSGetSamplers(0, 1, &savedSampler);

    ID3D11InputLayout* savedLayout = nullptr;
    context->IAGetInputLayout(&savedLayout);
    D3D11_PRIMITIVE_TOPOLOGY savedTopology{};
    context->IAGetPrimitiveTopology(&savedTopology);

    ID3D11GeometryShader* savedGeometryShader = nullptr;
    ID3D11HullShader* savedHullShader = nullptr;
    ID3D11DomainShader* savedDomainShader = nullptr;
    context->GSGetShader(&savedGeometryShader, nullptr, nullptr);
    context->HSGetShader(&savedHullShader, nullptr, nullptr);
    context->DSGetShader(&savedDomainShader, nullptr, nullptr);

    UINT savedScissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_RECT savedScissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    context->RSGetScissorRects(&savedScissorCount, savedScissors);

    if (const HRESULT taken = g_game.mutex->AcquireSync(kMutexKey, 16); FAILED(taken)) {
        const uint64_t missed = g_compositeFailures.fetch_add(1, std::memory_order_relaxed) + 1;
        if (missed >= kCompositeStall) {
            g_compositeFailures.store(0, std::memory_order_relaxed);
            m_needsRebuild = true;
            LOG_WARN("D2D", "the game could not take the overlay texture for {} frames ({:#x}); "
                            "rebuilding the overlay",
                     missed, static_cast<uint32_t>(taken));
        }
    } else {
        g_compositeFailures.store(0, std::memory_order_relaxed);

        const D3D11_VIEWPORT viewport{0.0f, 0.0f, m_size.x, m_size.y, 0.0f, 1.0f};
        const FLOAT blendFactor[4]{0.0f, 0.0f, 0.0f, 0.0f};

        context->GSSetShader(nullptr, nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0);
        context->DSSetShader(nullptr, nullptr, 0);
        context->OMSetRenderTargets(1, &target, nullptr);
        context->OMSetBlendState(g_game.blend, blendFactor, 0xFFFFFFFF);
        context->OMSetDepthStencilState(g_game.depth, 0);
        context->RSSetState(g_game.rasteriser);
        context->RSSetViewports(1, &viewport);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(g_game.vertexShader, nullptr, 0);
        context->PSSetShader(g_game.pixelShader, nullptr, 0);
        context->PSSetShaderResources(0, 1, &g_game.overlayView);
        context->PSSetSamplers(0, 1, &g_game.sampler);
        context->Draw(3, 0);

        g_game.mutex->ReleaseSync(kMutexKey);
    }

    context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedTargets, savedDepth);
    context->OMSetBlendState(savedBlend, savedBlendFactor, savedSampleMask);
    context->OMSetDepthStencilState(savedDepthState, savedStencilRef);
    context->RSSetState(savedRasteriser);
    if (viewportCount > 0)
        context->RSSetViewports(1, &savedViewport);
    context->VSSetShader(savedVertexShader, nullptr, 0);
    context->PSSetShader(savedPixelShader, nullptr, 0);
    context->PSSetShaderResources(0, 1, &savedResource);
    context->PSSetSamplers(0, 1, &savedSampler);
    context->IASetInputLayout(savedLayout);
    context->IASetPrimitiveTopology(savedTopology);
    context->GSSetShader(savedGeometryShader, nullptr, 0);
    context->HSSetShader(savedHullShader, nullptr, 0);
    context->DSSetShader(savedDomainShader, nullptr, 0);
    if (savedScissorCount > 0)
        context->RSSetScissorRects(savedScissorCount, savedScissors);

    release(savedGeometryShader);
    release(savedHullShader);
    release(savedDomainShader);

    for (auto* view : savedTargets)
        release(view);
    release(savedDepth);
    release(savedBlend);
    release(savedDepthState);
    release(savedRasteriser);
    release(savedVertexShader);
    release(savedPixelShader);
    release(savedResource);
    release(savedSampler);
    release(savedLayout);
}

void D2DOverlay::releaseTarget() {
    m_ready = false;
    m_chain = nullptr;

    images::releaseAll();

    if (m_context)
        m_context->SetTarget(nullptr);

    auto* bitmap = static_cast<ID2D1Bitmap1*>(m_targetBitmap);
    release(bitmap);
    m_targetBitmap = nullptr;

    release(m_context);

    g_game.releaseAll();

    release(g_sharedMutex);
    release(g_sharedTexture);
    release(g_ownContext);
    release(g_ownDevice);
    g_sharedHandle = nullptr;
}

void D2DOverlay::onResize() { releaseTarget(); }

void D2DOverlay::abandon(const char* why) {
    if (m_abandoned.exchange(true, std::memory_order_relaxed))
        return;

    m_status = why;
    LOG_WARN("D2D", "giving up on the overlay ({}); the game's renderer takes over. "
                    "Toggle the Direct2D module off and on to try again.",
             why);
}

uint64_t D2DOverlay::presentCount() const {
    return g_presentCalls.load(std::memory_order_relaxed);
}

void D2DOverlay::onPresent(IDXGISwapChain* swapChain) {
    if (g_presentCalls.fetch_add(1, std::memory_order_relaxed) == 0)
        LOG_INFO("D2D", "first present intercepted, swap chain {}", static_cast<void*>(swapChain));

    if (!m_enabled || m_abandoned.load(std::memory_order_relaxed))
        return;

    if (m_needsRebuild) {
        m_needsRebuild = false;
        releaseTarget();
    }

    if (m_ready && !boundTo(swapChain)) {
        LOG_INFO("D2D", "the game moved to another swap chain; rebuilding the overlay");
        releaseTarget();
    }

    if (!m_ready) {
        if (m_failed)
            return;
        if (!createTarget(swapChain)) {
            m_failed = true;
            releaseTarget();
            LOG_WARN("D2D", "overlay unavailable ({}), staying on the game renderer", m_status);
            return;
        }
    }

    if (!m_frameCallback)
        return;

    if (const HRESULT acquired = g_sharedMutex->AcquireSync(kMutexKey, 16); FAILED(acquired)) {
        if (g_acquireFailures.fetch_add(1, std::memory_order_relaxed) == kAcquireComplaint) {
            LOG_WARN("D2D", "the overlay texture lock has failed {} frames running ({:#x})",
                     kAcquireComplaint + 1, static_cast<uint32_t>(acquired));
        }
        return;
    }
    g_acquireFailures.store(0, std::memory_order_relaxed);

    m_context->BeginDraw();
    m_context->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    m_frameCallback();
    const HRESULT hr = m_context->EndDraw();
    g_ownContext->Flush();

    g_sharedMutex->ReleaseSync(kMutexKey);

    if (FAILED(hr)) {
        const uint64_t failures = g_drawFailures.fetch_add(1, std::memory_order_relaxed) + 1;

        if (failures == 1)
            LOG_WARN("D2D", "EndDraw failed ({:#x}); rebuilding the overlay",
                     static_cast<uint32_t>(hr));

        releaseTarget();

        if (failures > kDrawRetries)
            abandon("Direct2D stopped accepting draws");

        return;
    }

    g_drawFailures.store(0, std::memory_order_relaxed);

    composite(swapChain);
}

void D2DOverlay::shutdown() {
    releaseTarget();
    release(m_dwrite);
    release(m_factory);
    m_installed = false;
    m_status = "shut down";
}

}
