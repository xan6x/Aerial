#include "Render/MotionBlur.h"

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include <algorithm>
#include <cmath>

#include "GUI/ClickGui.h"
#include "SDK/Context.h"
#include "Utils/Logger.h"

namespace aerial::render {
namespace {

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

cbuffer Params : register(b0) {
    float blend;
    float opacity;
    float2 padding;
};

Texture2D    sceneTexture   : register(t0);
Texture2D    historyTexture : register(t1);
SamplerState linearClamp    : register(s0);

float4 ps_accumulate(VSOut input) : SV_TARGET {
    return float4(sceneTexture.Sample(linearClamp, input.uv).rgb, blend);
}

float4 ps_present(VSOut input) : SV_TARGET {
    return float4(historyTexture.Sample(linearClamp, input.uv).rgb, opacity);
}
)";

struct Params {
    float blend = 1.0f;
    float opacity = 1.0f;
    float padding[2]{};
};

struct Resources {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;

    ID3D11Texture2D* scene = nullptr;
    ID3D11ShaderResourceView* sceneView = nullptr;

    ID3D11Texture2D* history = nullptr;
    ID3D11ShaderResourceView* historyView = nullptr;
    ID3D11RenderTargetView* historyTarget = nullptr;

    ID3D11Texture2D* backBuffer = nullptr;
    ID3D11RenderTargetView* backBufferTarget = nullptr;

    ID3D11VertexShader* vertexShader = nullptr;
    ID3D11PixelShader* accumulateShader = nullptr;
    ID3D11PixelShader* presentShader = nullptr;
    ID3D11Buffer* params = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11BlendState* accumulateBlend = nullptr;
    ID3D11BlendState* presentBlend = nullptr;
    ID3D11DepthStencilState* depth = nullptr;
    ID3D11RasterizerState* rasteriser = nullptr;

    UINT width = 0;
    UINT height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

    void releaseFrames() {
        release(sceneView);
        release(scene);
        release(historyView);
        release(historyTarget);
        release(history);
        release(backBufferTarget);
        release(backBuffer);
        width = 0;
        height = 0;
        format = DXGI_FORMAT_UNKNOWN;
    }

    void releaseAll() {
        releaseFrames();
        release(vertexShader);
        release(accumulateShader);
        release(presentShader);
        release(params);
        release(sampler);
        release(accumulateBlend);
        release(presentBlend);
        release(depth);
        release(rasteriser);
        release(context);
        release(device);
    }
};

Resources g_res;

struct SavedState {
    ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ID3D11DepthStencilView* depthView = nullptr;
    ID3D11BlendState* blend = nullptr;
    FLOAT blendFactor[4]{};
    UINT sampleMask = 0;
    ID3D11DepthStencilState* depthState = nullptr;
    UINT stencilRef = 0;
    ID3D11RasterizerState* rasteriser = nullptr;
    UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    UINT scissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    ID3D11VertexShader* vertexShader = nullptr;
    ID3D11PixelShader* pixelShader = nullptr;
    ID3D11GeometryShader* geometryShader = nullptr;
    ID3D11HullShader* hullShader = nullptr;
    ID3D11DomainShader* domainShader = nullptr;
    ID3D11ShaderResourceView* resources[2]{};
    ID3D11SamplerState* sampler = nullptr;
    ID3D11Buffer* constants = nullptr;
    ID3D11InputLayout* layout = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY topology{};

    void capture(ID3D11DeviceContext* context) {
        context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, &depthView);
        context->OMGetBlendState(&blend, blendFactor, &sampleMask);
        context->OMGetDepthStencilState(&depthState, &stencilRef);
        context->RSGetState(&rasteriser);
        context->RSGetViewports(&viewportCount, viewports);
        context->RSGetScissorRects(&scissorCount, scissors);
        context->VSGetShader(&vertexShader, nullptr, nullptr);
        context->PSGetShader(&pixelShader, nullptr, nullptr);
        context->GSGetShader(&geometryShader, nullptr, nullptr);
        context->HSGetShader(&hullShader, nullptr, nullptr);
        context->DSGetShader(&domainShader, nullptr, nullptr);
        context->PSGetShaderResources(0, 2, resources);
        context->PSGetSamplers(0, 1, &sampler);
        context->PSGetConstantBuffers(0, 1, &constants);
        context->IAGetInputLayout(&layout);
        context->IAGetPrimitiveTopology(&topology);
    }

    void restore(ID3D11DeviceContext* context) {
        ID3D11ShaderResourceView* none[2]{};
        context->PSSetShaderResources(0, 2, none);

        context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, depthView);
        context->OMSetBlendState(blend, blendFactor, sampleMask);
        context->OMSetDepthStencilState(depthState, stencilRef);
        context->RSSetState(rasteriser);
        if (viewportCount > 0)
            context->RSSetViewports(viewportCount, viewports);
        if (scissorCount > 0)
            context->RSSetScissorRects(scissorCount, scissors);
        context->VSSetShader(vertexShader, nullptr, 0);
        context->PSSetShader(pixelShader, nullptr, 0);
        context->GSSetShader(geometryShader, nullptr, 0);
        context->HSSetShader(hullShader, nullptr, 0);
        context->DSSetShader(domainShader, nullptr, 0);
        context->PSSetShaderResources(0, 2, resources);
        context->PSSetSamplers(0, 1, &sampler);
        context->PSSetConstantBuffers(0, 1, &constants);
        context->IASetInputLayout(layout);
        context->IASetPrimitiveTopology(topology);

        for (auto*& view : targets)
            release(view);
        release(depthView);
        release(blend);
        release(depthState);
        release(rasteriser);
        release(vertexShader);
        release(pixelShader);
        release(geometryShader);
        release(hullShader);
        release(domainShader);
        for (auto*& view : resources)
            release(view);
        release(sampler);
        release(constants);
        release(layout);
    }
};

bool compilePixelShader(const char* entry, ID3D11PixelShader** out) {
    ID3DBlob* blob = nullptr;
    ID3DBlob* errors = nullptr;

    HRESULT hr = D3DCompile(kShaderSource, sizeof(kShaderSource) - 1, nullptr, nullptr, nullptr,
                            entry, "ps_4_0", 0, 0, &blob, &errors);
    if (FAILED(hr)) {
        LOG_ERROR("MotionBlur", "{} failed to compile: {}", entry,
                  errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
        release(errors);
        return false;
    }
    release(errors);

    hr = g_res.device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr,
                                         out);
    release(blob);
    return SUCCEEDED(hr);
}

bool createPipeline() {
    ID3DBlob* blob = nullptr;
    ID3DBlob* errors = nullptr;

    HRESULT hr = D3DCompile(kShaderSource, sizeof(kShaderSource) - 1, nullptr, nullptr, nullptr,
                            "vs_main", "vs_4_0", 0, 0, &blob, &errors);
    if (FAILED(hr)) {
        LOG_ERROR("MotionBlur", "vertex shader failed to compile: {}",
                  errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
        release(errors);
        return false;
    }
    release(errors);

    hr = g_res.device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr,
                                          &g_res.vertexShader);
    release(blob);
    if (FAILED(hr))
        return false;

    if (!compilePixelShader("ps_accumulate", &g_res.accumulateShader) ||
        !compilePixelShader("ps_present", &g_res.presentShader))
        return false;

    D3D11_BUFFER_DESC buffer{};
    buffer.ByteWidth = sizeof(Params);
    buffer.Usage = D3D11_USAGE_DYNAMIC;
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(g_res.device->CreateBuffer(&buffer, nullptr, &g_res.params)))
        return false;

    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(g_res.device->CreateSamplerState(&sampler, &g_res.sampler)))
        return false;

    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask =
        D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN |
        D3D11_COLOR_WRITE_ENABLE_BLUE;
    if (FAILED(g_res.device->CreateBlendState(&blend, &g_res.accumulateBlend)))
        return false;
    if (FAILED(g_res.device->CreateBlendState(&blend, &g_res.presentBlend)))
        return false;

    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.StencilEnable = FALSE;
    if (FAILED(g_res.device->CreateDepthStencilState(&depth, &g_res.depth)))
        return false;

    D3D11_RASTERIZER_DESC rasteriser{};
    rasteriser.FillMode = D3D11_FILL_SOLID;
    rasteriser.CullMode = D3D11_CULL_NONE;
    rasteriser.DepthClipEnable = FALSE;
    rasteriser.ScissorEnable = FALSE;
    return SUCCEEDED(g_res.device->CreateRasterizerState(&rasteriser, &g_res.rasteriser));
}

bool createFrames(const D3D11_TEXTURE2D_DESC& backDesc) {
    D3D11_TEXTURE2D_DESC scene{};
    scene.Width = backDesc.Width;
    scene.Height = backDesc.Height;
    scene.MipLevels = 1;
    scene.ArraySize = 1;
    scene.Format = backDesc.Format;
    scene.SampleDesc.Count = 1;
    scene.Usage = D3D11_USAGE_DEFAULT;
    scene.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(g_res.device->CreateTexture2D(&scene, nullptr, &g_res.scene)))
        return false;
    if (FAILED(g_res.device->CreateShaderResourceView(g_res.scene, nullptr, &g_res.sceneView)))
        return false;

    D3D11_TEXTURE2D_DESC history = scene;
    history.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    history.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    if (FAILED(g_res.device->CreateTexture2D(&history, nullptr, &g_res.history)))
        return false;
    if (FAILED(g_res.device->CreateShaderResourceView(g_res.history, nullptr, &g_res.historyView)))
        return false;
    if (FAILED(g_res.device->CreateRenderTargetView(g_res.history, nullptr, &g_res.historyTarget)))
        return false;

    g_res.width = backDesc.Width;
    g_res.height = backDesc.Height;
    g_res.format = backDesc.Format;
    return true;
}

bool gateOpen() {
    if (gui::ClickGui::get().isOpen())
        return false;

    auto& context = sdk::Context::get();
    if (!context.inGame() || !context.client)
        return false;

    return context.client->mouseGrabbed();
}

double seconds(long long from, long long to) {
    static LARGE_INTEGER frequency{};
    if (frequency.QuadPart == 0)
        QueryPerformanceFrequency(&frequency);
    if (frequency.QuadPart == 0)
        return 0.0;
    return static_cast<double>(to - from) / static_cast<double>(frequency.QuadPart);
}

long long now() {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}

}

MotionBlur& MotionBlur::get() {
    static MotionBlur instance;
    return instance;
}

void MotionBlur::setEnabled(bool enabled) {
    if (enabled) {
        m_failed.store(false, std::memory_order_relaxed);
        m_status.store("waiting for a frame", std::memory_order_relaxed);
    } else {
        m_status.store("off", std::memory_order_relaxed);
    }
    m_primed = false;
    m_enabled.store(enabled, std::memory_order_relaxed);
}

void MotionBlur::fail(const char* why) {
    m_failed.store(true, std::memory_order_relaxed);
    m_status.store(why, std::memory_order_relaxed);
    LOG_WARN("MotionBlur", "unavailable: {}", why);
    releaseResources();
}

void MotionBlur::releaseResources() {
    g_res.releaseAll();
    m_primed = false;
}

void MotionBlur::onResize() {
    g_res.releaseFrames();
    m_primed = false;
}

void MotionBlur::shutdown() {
    m_enabled.store(false, std::memory_order_relaxed);
    releaseResources();
}

float MotionBlur::blendFactor() {
    const long long tick = now();
    const double delta = m_lastTick == 0 ? 0.0 : seconds(m_lastTick, tick);
    m_lastTick = tick;

    if (delta <= 0.0 || delta > 0.25)
        return 1.0f;

    const float amount = std::clamp(m_amount.load(std::memory_order_relaxed), 0.0f, 1.0f);
    const float constant = 0.007f + amount * 0.15f;

    return std::clamp(1.0f - std::exp(static_cast<float>(-delta) / constant), 0.008f, 1.0f);
}

bool MotionBlur::ensureBackBuffer(IDXGISwapChain* swapChain) {
    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                    reinterpret_cast<void**>(&backBuffer))))
        return false;

    D3D11_TEXTURE2D_DESC desc{};
    backBuffer->GetDesc(&desc);

    if (desc.SampleDesc.Count != 1) {
        release(backBuffer);
        fail("the swap chain is multisampled");
        return false;
    }

    if (desc.Width != g_res.width || desc.Height != g_res.height || desc.Format != g_res.format) {
        g_res.releaseFrames();
        if (!createFrames(desc)) {
            release(backBuffer);
            fail("could not create the frame buffers");
            return false;
        }
        m_primed = false;
        LOG_INFO("MotionBlur", "buffers ready: {}x{}", desc.Width, desc.Height);
    }

    if (backBuffer != g_res.backBuffer || !g_res.backBufferTarget) {
        release(g_res.backBufferTarget);
        release(g_res.backBuffer);

        if (FAILED(g_res.device->CreateRenderTargetView(backBuffer, nullptr,
                                                        &g_res.backBufferTarget))) {
            release(backBuffer);
            fail("could not view the back buffer");
            return false;
        }
        g_res.backBuffer = backBuffer;
        return true;
    }

    release(backBuffer);
    return true;
}

bool MotionBlur::ensureResources(IDXGISwapChain* swapChain) {
    if (!g_res.device) {
        if (FAILED(swapChain->GetDevice(__uuidof(ID3D11Device),
                                        reinterpret_cast<void**>(&g_res.device)))) {
            fail("the swap chain has no D3D11 device");
            return false;
        }
        g_res.device->GetImmediateContext(&g_res.context);

        if (!createPipeline()) {
            fail("the blur pipeline could not be built");
            return false;
        }
    }

    return ensureBackBuffer(swapChain);
}

void MotionBlur::onPresent(IDXGISwapChain* swapChain) {
    if (!m_enabled.load(std::memory_order_relaxed) || m_failed.load(std::memory_order_relaxed))
        return;

    if (!gateOpen()) {
        m_primed = false;
        m_lastTick = 0;
        return;
    }

    if (!ensureResources(swapChain))
        return;

    const float blend = m_primed ? blendFactor() : 1.0f;
    const float opacity = std::clamp(m_opacity.load(std::memory_order_relaxed), 0.0f, 1.0f);

    auto* context = g_res.context;

    SavedState saved;
    saved.capture(context);

    context->OMSetRenderTargets(0, nullptr, nullptr);
    context->CopyResource(g_res.scene, g_res.backBuffer);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(g_res.params, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        auto* params = static_cast<Params*>(mapped.pData);
        params->blend = blend;
        params->opacity = opacity;
        context->Unmap(g_res.params, 0);
    }

    const D3D11_VIEWPORT viewport{0.0f,
                                  0.0f,
                                  static_cast<float>(g_res.width),
                                  static_cast<float>(g_res.height),
                                  0.0f,
                                  1.0f};
    const FLOAT factor[4]{0.0f, 0.0f, 0.0f, 0.0f};

    context->GSSetShader(nullptr, nullptr, 0);
    context->HSSetShader(nullptr, nullptr, 0);
    context->DSSetShader(nullptr, nullptr, 0);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(g_res.vertexShader, nullptr, 0);
    context->RSSetState(g_res.rasteriser);
    context->RSSetViewports(1, &viewport);
    context->OMSetDepthStencilState(g_res.depth, 0);
    context->PSSetSamplers(0, 1, &g_res.sampler);
    context->PSSetConstantBuffers(0, 1, &g_res.params);

    ID3D11ShaderResourceView* sceneOnly[2]{g_res.sceneView, nullptr};
    context->OMSetRenderTargets(1, &g_res.historyTarget, nullptr);
    context->OMSetBlendState(g_res.accumulateBlend, factor, 0xFFFFFFFF);
    context->PSSetShader(g_res.accumulateShader, nullptr, 0);
    context->PSSetShaderResources(0, 2, sceneOnly);
    context->Draw(3, 0);

    ID3D11ShaderResourceView* none[2]{};
    context->PSSetShaderResources(0, 2, none);

    ID3D11ShaderResourceView* historyOnly[2]{nullptr, g_res.historyView};
    context->OMSetRenderTargets(1, &g_res.backBufferTarget, nullptr);
    context->OMSetBlendState(g_res.presentBlend, factor, 0xFFFFFFFF);
    context->PSSetShader(g_res.presentShader, nullptr, 0);
    context->PSSetShaderResources(0, 2, historyOnly);
    context->Draw(3, 0);

    saved.restore(context);

    if (!m_primed) {
        m_primed = true;
        m_status.store("running", std::memory_order_relaxed);
    }
}

}
