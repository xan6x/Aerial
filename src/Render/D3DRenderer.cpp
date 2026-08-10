#include "Render/D3DRenderer.h"

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dwrite.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

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

struct Params {
    float screen[4];  // width, height, 0, 0
    float rect[4];    // minX, minY, maxX, maxY
    float colorA[4];
    float colorB[4];
    float p[4];       // radius, thickness, mode(0 rect,1 image,2 text), gradient(0,1 vert,2 horiz)
    float p2[4];      // aa, 0, 0, 0
};

struct GeoVertex {
    float x, y;
    float r, g, b, a;
};

struct TexVertex {
    float x, y;
    float u, v;
    float r, g, b, a;
};

constexpr char kShader[] = R"(
cbuffer Params : register(b0) {
    float4 uScreen;
    float4 uRect;
    float4 uColorA;
    float4 uColorB;
    float4 uP;    // radius, thickness, mode, gradient
    float4 uP2;   // aa
};

Texture2D    tex : register(t0);
SamplerState smp : register(s0);

struct VSOut { float4 pos : SV_POSITION; float2 px : TEXCOORD0; float2 uv : TEXCOORD1; };

VSOut vs_main(uint id : SV_VertexID) {
    float2 c  = float2(id & 1, (id >> 1) & 1);
    float2 mn = uRect.xy;
    float2 mx = uRect.zw;
    float expand = (uP.z < 0.5) ? uP2.x : 0.0;
    float2 p = lerp(mn - expand, mx + expand, c);
    VSOut o;
    o.px  = p;
    o.uv  = c;
    o.pos = float4(p / uScreen.xy * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

float sdRound(float2 p, float2 h, float r) {
    float2 q = abs(p) - h + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

float4 ps_main(VSOut i) : SV_TARGET {
    float mode = uP.z;
    if (mode > 1.5) {
        float2 auv = lerp(uColorB.xy, uColorB.zw, i.uv);
        float cov = tex.Sample(smp, auv).r;
        return float4(uColorA.rgb, uColorA.a * cov);
    }
    if (mode > 0.5) {
        float4 t = tex.Sample(smp, i.uv);
        return t * uColorA;
    }
    float2 mn = uRect.xy;
    float2 mx = uRect.zw;
    float2 center = (mn + mx) * 0.5;
    float2 half = (mx - mn) * 0.5;
    float r = min(uP.x, min(half.x, half.y));
    float d = sdRound(i.px - center, half, r);
    float aa = max(uP2.x, 0.001);
    float cov;
    if (uP.y > 0.0)
        cov = 1.0 - smoothstep(0.0, aa, abs(d) - uP.y * 0.5);
    else
        cov = 1.0 - smoothstep(-aa, 0.0, d);
    float4 col = uColorA;
    if (uP.w > 0.5) {
        float t = (uP.w < 1.5) ? saturate((i.px.y - mn.y) / max(mx.y - mn.y, 0.001))
                               : saturate((i.px.x - mn.x) / max(mx.x - mn.x, 0.001));
        col = lerp(uColorA, uColorB, t);
    }
    return float4(col.rgb, col.a * cov);
}

struct GVSIn  { float2 pos : POSITION; float4 col : COLOR; };
struct GVSOut { float4 pos : SV_POSITION; float4 col : COLOR; };

GVSOut gvs_main(GVSIn i) {
    GVSOut o;
    o.pos = float4(i.pos / uScreen.xy * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.col = i.col;
    return o;
}

float4 gps_main(GVSOut i) : SV_TARGET { return i.col; }

struct TVSIn  { float2 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; };
struct TVSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; };

TVSOut tvs_main(TVSIn i) {
    TVSOut o;
    o.pos = float4(i.pos / uScreen.xy * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.uv = i.uv;
    o.col = i.col;
    return o;
}

float4 tps_main(TVSOut i) : SV_TARGET {
    float cov = tex.Sample(smp, i.uv).r;
    return float4(i.col.rgb, i.col.a * cov);
}
)";

struct Resources {
    ID3D11Device* device = nullptr;  // owned by the game, only compared
    ID3D11DeviceContext* context = nullptr;

    ID3D11VertexShader* rectVS = nullptr;
    ID3D11PixelShader* rectPS = nullptr;
    ID3D11VertexShader* geoVS = nullptr;
    ID3D11PixelShader* geoPS = nullptr;
    ID3D11InputLayout* geoLayout = nullptr;

    ID3D11VertexShader* textVS = nullptr;
    ID3D11PixelShader* textPS = nullptr;
    ID3D11InputLayout* textLayout = nullptr;

    ID3D11Buffer* cbuffer = nullptr;
    ID3D11Buffer* geoBuffer = nullptr;
    size_t geoCapacity = 0;
    ID3D11Buffer* textBuffer = nullptr;
    size_t textCapacity = 0;

    ID3D11BlendState* blend = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11RasterizerState* rasteriser = nullptr;
    ID3D11DepthStencilState* depth = nullptr;

    ID3D11Texture2D* white = nullptr;
    ID3D11ShaderResourceView* whiteView = nullptr;

    int pipeMode = 0;
    ID3D11ShaderResourceView* boundView = nullptr;

    void releaseAll() {
        release(rectVS);
        release(rectPS);
        release(geoVS);
        release(geoPS);
        release(geoLayout);
        release(textVS);
        release(textPS);
        release(textLayout);
        release(cbuffer);
        release(geoBuffer);
        geoCapacity = 0;
        release(textBuffer);
        textCapacity = 0;
        release(blend);
        release(sampler);
        release(rasteriser);
        release(depth);
        release(whiteView);
        release(white);
        release(context);
        device = nullptr;
    }
};

Resources g_r;

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
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11GeometryShader* gs = nullptr;
    ID3D11HullShader* hs = nullptr;
    ID3D11DomainShader* ds = nullptr;
    ID3D11Buffer* vsCB = nullptr;
    ID3D11Buffer* psCB = nullptr;
    ID3D11ShaderResourceView* psSRV = nullptr;
    ID3D11SamplerState* psSampler = nullptr;
    ID3D11InputLayout* layout = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11Buffer* vertexBuffer = nullptr;
    UINT vertexStride = 0;
    UINT vertexOffset = 0;
};

SavedState g_saved;

void saveState(ID3D11DeviceContext* c) {
    c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, g_saved.targets, &g_saved.depthView);
    c->OMGetBlendState(&g_saved.blend, g_saved.blendFactor, &g_saved.sampleMask);
    c->OMGetDepthStencilState(&g_saved.depthState, &g_saved.stencilRef);
    c->RSGetState(&g_saved.rasteriser);
    g_saved.viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    c->RSGetViewports(&g_saved.viewportCount, g_saved.viewports);
    g_saved.scissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    c->RSGetScissorRects(&g_saved.scissorCount, g_saved.scissors);
    c->VSGetShader(&g_saved.vs, nullptr, nullptr);
    c->PSGetShader(&g_saved.ps, nullptr, nullptr);
    c->GSGetShader(&g_saved.gs, nullptr, nullptr);
    c->HSGetShader(&g_saved.hs, nullptr, nullptr);
    c->DSGetShader(&g_saved.ds, nullptr, nullptr);
    c->VSGetConstantBuffers(0, 1, &g_saved.vsCB);
    c->PSGetConstantBuffers(0, 1, &g_saved.psCB);
    c->PSGetShaderResources(0, 1, &g_saved.psSRV);
    c->PSGetSamplers(0, 1, &g_saved.psSampler);
    c->IAGetInputLayout(&g_saved.layout);
    c->IAGetPrimitiveTopology(&g_saved.topology);
    c->IAGetVertexBuffers(0, 1, &g_saved.vertexBuffer, &g_saved.vertexStride, &g_saved.vertexOffset);
}

void restoreState(ID3D11DeviceContext* c) {
    c->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, g_saved.targets, g_saved.depthView);
    c->OMSetBlendState(g_saved.blend, g_saved.blendFactor, g_saved.sampleMask);
    c->OMSetDepthStencilState(g_saved.depthState, g_saved.stencilRef);
    c->RSSetState(g_saved.rasteriser);
    if (g_saved.viewportCount > 0)
        c->RSSetViewports(g_saved.viewportCount, g_saved.viewports);
    if (g_saved.scissorCount > 0)
        c->RSSetScissorRects(g_saved.scissorCount, g_saved.scissors);
    c->VSSetShader(g_saved.vs, nullptr, 0);
    c->PSSetShader(g_saved.ps, nullptr, 0);
    c->GSSetShader(g_saved.gs, nullptr, 0);
    c->HSSetShader(g_saved.hs, nullptr, 0);
    c->DSSetShader(g_saved.ds, nullptr, 0);
    c->VSSetConstantBuffers(0, 1, &g_saved.vsCB);
    c->PSSetConstantBuffers(0, 1, &g_saved.psCB);
    c->PSSetShaderResources(0, 1, &g_saved.psSRV);
    c->PSSetSamplers(0, 1, &g_saved.psSampler);
    c->IASetInputLayout(g_saved.layout);
    c->IASetPrimitiveTopology(g_saved.topology);
    c->IASetVertexBuffers(0, 1, &g_saved.vertexBuffer, &g_saved.vertexStride, &g_saved.vertexOffset);

    for (auto*& view : g_saved.targets)
        release(view);
    release(g_saved.depthView);
    release(g_saved.blend);
    release(g_saved.depthState);
    release(g_saved.rasteriser);
    release(g_saved.vs);
    release(g_saved.ps);
    release(g_saved.gs);
    release(g_saved.hs);
    release(g_saved.ds);
    release(g_saved.vsCB);
    release(g_saved.psCB);
    release(g_saved.psSRV);
    release(g_saved.psSampler);
    release(g_saved.layout);
    release(g_saved.vertexBuffer);
}

constexpr UINT kAtlasSize = 1024;

struct Glyph {
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    float w = 0, h = 0;
    float bearingX = 0, bearingY = 0;
    float advance = 0;
    bool blank = false;
};

DWRITE_FONT_WEIGHT dwriteWeight(D3DRenderer::Weight weight) {
    switch (weight) {
    case D3DRenderer::Weight::Medium:   return DWRITE_FONT_WEIGHT_MEDIUM;
    case D3DRenderer::Weight::SemiBold: return DWRITE_FONT_WEIGHT_SEMI_BOLD;
    case D3DRenderer::Weight::Bold:     return DWRITE_FONT_WEIGHT_BOLD;
    case D3DRenderer::Weight::Regular:  break;
    }
    return DWRITE_FONT_WEIGHT_NORMAL;
}

struct Atlas {
    IDWriteFactory* dwrite = nullptr;
    std::unordered_map<int, IDWriteFontFace*> faces;

    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* view = nullptr;
    UINT penX = 1, penY = 1, rowHeight = 0;

    std::unordered_map<uint64_t, Glyph> glyphs;

    void releaseFaces() {
        for (auto& [key, face] : faces)
            release(face);
        faces.clear();
    }

    void releaseDevice() {
        release(view);
        release(texture);
        glyphs.clear();
        penX = penY = 1;
        rowHeight = 0;
    }

    IDWriteFactory* factory() {
        if (!dwrite)
            DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                reinterpret_cast<IUnknown**>(&dwrite));
        return dwrite;
    }

    IDWriteFontFace* face(D3DRenderer::Weight weight) {
        const int key = static_cast<int>(weight);
        if (const auto it = faces.find(key); it != faces.end())
            return it->second;

        IDWriteFactory* dw = factory();
        if (!dw)
            return nullptr;

        IDWriteFontCollection* collection = nullptr;
        if (FAILED(dw->GetSystemFontCollection(&collection)))
            return nullptr;

        UINT32 index = 0;
        BOOL exists = FALSE;
        collection->FindFamilyName(L"Segoe UI", &index, &exists);

        IDWriteFontFace* result = nullptr;
        IDWriteFontFamily* family = nullptr;
        if (exists && SUCCEEDED(collection->GetFontFamily(index, &family))) {
            IDWriteFont* font = nullptr;
            if (SUCCEEDED(family->GetFirstMatchingFont(dwriteWeight(weight), DWRITE_FONT_STRETCH_NORMAL,
                                                       DWRITE_FONT_STYLE_NORMAL, &font))) {
                font->CreateFontFace(&result);
                font->Release();
            }
            family->Release();
        }
        collection->Release();

        faces[key] = result;
        return result;
    }

    bool ensureTexture(ID3D11Device* device) {
        if (texture)
            return true;

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = kAtlasSize;
        desc.Height = kAtlasSize;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture)))
            return false;
        if (FAILED(device->CreateShaderResourceView(texture, nullptr, &view))) {
            release(texture);
            return false;
        }
        penX = penY = 1;
        rowHeight = 0;
        return true;
    }

    const Glyph* glyph(ID3D11Device* device, ID3D11DeviceContext* context, uint32_t codepoint,
                       float pixelSize, D3DRenderer::Weight weight) {
        const uint64_t key = (static_cast<uint64_t>(codepoint) << 24) |
                             (static_cast<uint64_t>(static_cast<int>(weight)) << 20) |
                             (static_cast<uint64_t>(static_cast<int>(pixelSize * 4.0f)) & 0xFFFFF);
        if (const auto it = glyphs.find(key); it != glyphs.end())
            return &it->second;

        if (!ensureTexture(device))
            return nullptr;

        IDWriteFontFace* fontFace = face(weight);
        if (!fontFace)
            return nullptr;

        DWRITE_FONT_METRICS fontMetrics{};
        fontFace->GetMetrics(&fontMetrics);
        const float scale = pixelSize / fontMetrics.designUnitsPerEm;

        UINT16 glyphIndex = 0;
        fontFace->GetGlyphIndices(&codepoint, 1, &glyphIndex);

        DWRITE_GLYPH_METRICS gm{};
        fontFace->GetDesignGlyphMetrics(&glyphIndex, 1, &gm, FALSE);

        Glyph out;
        out.advance = static_cast<float>(gm.advanceWidth) * scale;

        FLOAT advanceZero = 0.0f;
        DWRITE_GLYPH_OFFSET offset{};
        DWRITE_GLYPH_RUN run{};
        run.fontFace = fontFace;
        run.fontEmSize = pixelSize;
        run.glyphCount = 1;
        run.glyphIndices = &glyphIndex;
        run.glyphAdvances = &advanceZero;
        run.glyphOffsets = &offset;

        IDWriteGlyphRunAnalysis* analysis = nullptr;
        if (FAILED(factory()->CreateGlyphRunAnalysis(&run, 1.0f, nullptr,
                                                     DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
                                                     DWRITE_MEASURING_MODE_NATURAL, 0.0f, 0.0f,
                                                     &analysis))) {
            glyphs[key] = out;
            return &glyphs[key];
        }

        RECT bounds{};
        analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds);
        const int gw = bounds.right - bounds.left;
        const int gh = bounds.bottom - bounds.top;

        if (gw <= 0 || gh <= 0) {
            analysis->Release();
            out.blank = true;
            glyphs[key] = out;
            return &glyphs[key];
        }

        std::vector<BYTE> rgb(static_cast<size_t>(gw) * gh * 3);
        if (FAILED(analysis->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds, rgb.data(),
                                                static_cast<UINT>(rgb.size())))) {
            analysis->Release();
            glyphs[key] = out;
            return &glyphs[key];
        }
        analysis->Release();

        std::vector<BYTE> grey(static_cast<size_t>(gw) * gh);
        for (int i = 0; i < gw * gh; ++i)
            grey[static_cast<size_t>(i)] = rgb[static_cast<size_t>(i) * 3 + 1];

        if (penX + gw + 1 > kAtlasSize) {
            penX = 1;
            penY += rowHeight + 1;
            rowHeight = 0;
        }
        if (penY + gh + 1 > kAtlasSize) {
            out.blank = true;
            glyphs[key] = out;
            return &glyphs[key];
        }

        D3D11_BOX box{penX, penY, 0, penX + gw, penY + gh, 1};
        context->UpdateSubresource(texture, 0, &box, grey.data(), static_cast<UINT>(gw), 0);

        out.u0 = static_cast<float>(penX) / kAtlasSize;
        out.v0 = static_cast<float>(penY) / kAtlasSize;
        out.u1 = static_cast<float>(penX + gw) / kAtlasSize;
        out.v1 = static_cast<float>(penY + gh) / kAtlasSize;
        out.w = static_cast<float>(gw);
        out.h = static_cast<float>(gh);
        out.bearingX = static_cast<float>(bounds.left);
        out.bearingY = static_cast<float>(bounds.top);

        penX += gw + 1;
        rowHeight = std::max<UINT>(rowHeight, static_cast<UINT>(gh));

        glyphs[key] = out;
        return &glyphs[key];
    }

    float ascent(D3DRenderer::Weight weight, float pixelSize) {
        IDWriteFontFace* fontFace = face(weight);
        if (!fontFace)
            return pixelSize;
        DWRITE_FONT_METRICS m{};
        fontFace->GetMetrics(&m);
        return static_cast<float>(m.ascent) * pixelSize / m.designUnitsPerEm;
    }
};

Atlas g_atlas;

uint32_t nextCodepoint(const std::string& s, size_t& i) {
    const auto b0 = static_cast<unsigned char>(s[i]);
    if (b0 < 0x80) {
        ++i;
        return b0;
    }
    int extra = 0;
    uint32_t cp = 0;
    if ((b0 & 0xE0) == 0xC0) { cp = b0 & 0x1F; extra = 1; }
    else if ((b0 & 0xF0) == 0xE0) { cp = b0 & 0x0F; extra = 2; }
    else if ((b0 & 0xF8) == 0xF0) { cp = b0 & 0x07; extra = 3; }
    else { ++i; return 0xFFFD; }

    ++i;
    for (int k = 0; k < extra && i < s.size(); ++k, ++i) {
        const auto cb = static_cast<unsigned char>(s[i]);
        if ((cb & 0xC0) != 0x80)
            return 0xFFFD;
        cp = (cp << 6) | (cb & 0x3F);
    }
    return cp;
}

bool compile(const char* entry, const char* target, ID3DBlob** blob) {
    ID3DBlob* errors = nullptr;
    const HRESULT hr = D3DCompile(kShader, sizeof(kShader) - 1, nullptr, nullptr, nullptr, entry,
                                  target, 0, 0, blob, &errors);
    if (FAILED(hr)) {
        LOG_ERROR("D3D", "{} failed: {}", entry,
                  errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
        release(errors);
        return false;
    }
    release(errors);
    return true;
}

std::vector<Rect> g_scissorStack;

}  // namespace

D3DRenderer& D3DRenderer::get() {
    static D3DRenderer instance;
    return instance;
}

bool D3DRenderer::ensureResources(ID3D11Device* device) {
    if (g_r.device == device && g_r.rectVS)
        return true;

    releaseDeviceResources();
    g_r.device = device;
    device->GetImmediateContext(&g_r.context);

    ID3DBlob* rectVSBlob = nullptr;
    ID3DBlob* rectPSBlob = nullptr;
    ID3DBlob* geoVSBlob = nullptr;
    ID3DBlob* geoPSBlob = nullptr;
    ID3DBlob* textVSBlob = nullptr;
    ID3DBlob* textPSBlob = nullptr;
    bool ok = compile("vs_main", "vs_4_0", &rectVSBlob) &&
              compile("ps_main", "ps_4_0", &rectPSBlob) &&
              compile("gvs_main", "vs_4_0", &geoVSBlob) &&
              compile("gps_main", "ps_4_0", &geoPSBlob) &&
              compile("tvs_main", "vs_4_0", &textVSBlob) &&
              compile("tps_main", "ps_4_0", &textPSBlob);

    if (ok)
        ok = SUCCEEDED(device->CreateVertexShader(rectVSBlob->GetBufferPointer(),
                                                  rectVSBlob->GetBufferSize(), nullptr, &g_r.rectVS)) &&
             SUCCEEDED(device->CreatePixelShader(rectPSBlob->GetBufferPointer(),
                                                 rectPSBlob->GetBufferSize(), nullptr, &g_r.rectPS)) &&
             SUCCEEDED(device->CreateVertexShader(geoVSBlob->GetBufferPointer(),
                                                  geoVSBlob->GetBufferSize(), nullptr, &g_r.geoVS)) &&
             SUCCEEDED(device->CreatePixelShader(geoPSBlob->GetBufferPointer(),
                                                 geoPSBlob->GetBufferSize(), nullptr, &g_r.geoPS)) &&
             SUCCEEDED(device->CreateVertexShader(textVSBlob->GetBufferPointer(),
                                                  textVSBlob->GetBufferSize(), nullptr, &g_r.textVS)) &&
             SUCCEEDED(device->CreatePixelShader(textPSBlob->GetBufferPointer(),
                                                 textPSBlob->GetBufferSize(), nullptr, &g_r.textPS));

    if (ok) {
        const D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        ok = SUCCEEDED(device->CreateInputLayout(layout, 2, geoVSBlob->GetBufferPointer(),
                                                 geoVSBlob->GetBufferSize(), &g_r.geoLayout));
    }

    if (ok) {
        const D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        ok = SUCCEEDED(device->CreateInputLayout(layout, 3, textVSBlob->GetBufferPointer(),
                                                 textVSBlob->GetBufferSize(), &g_r.textLayout));
    }

    release(rectVSBlob);
    release(rectPSBlob);
    release(geoVSBlob);
    release(geoPSBlob);
    release(textVSBlob);
    release(textPSBlob);
    if (!ok) {
        releaseDeviceResources();
        return false;
    }

    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(Params);
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateBuffer(&cb, nullptr, &g_r.cbuffer))) {
        releaseDeviceResources();
        return false;
    }

    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device->CreateBlendState(&blend, &g_r.blend);

    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    device->CreateSamplerState(&sampler, &g_r.sampler);

    D3D11_RASTERIZER_DESC rasteriser{};
    rasteriser.FillMode = D3D11_FILL_SOLID;
    rasteriser.CullMode = D3D11_CULL_NONE;
    rasteriser.DepthClipEnable = FALSE;
    rasteriser.ScissorEnable = TRUE;
    device->CreateRasterizerState(&rasteriser, &g_r.rasteriser);

    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = FALSE;
    depth.StencilEnable = FALSE;
    device->CreateDepthStencilState(&depth, &g_r.depth);

    const uint8_t whitePixel[4] = {255, 255, 255, 255};
    D3D11_TEXTURE2D_DESC whiteDesc{};
    whiteDesc.Width = 1;
    whiteDesc.Height = 1;
    whiteDesc.MipLevels = 1;
    whiteDesc.ArraySize = 1;
    whiteDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    whiteDesc.SampleDesc.Count = 1;
    whiteDesc.Usage = D3D11_USAGE_IMMUTABLE;
    whiteDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA whiteData{whitePixel, 4, 0};
    if (SUCCEEDED(device->CreateTexture2D(&whiteDesc, &whiteData, &g_r.white)))
        device->CreateShaderResourceView(g_r.white, nullptr, &g_r.whiteView);

    if (!g_r.blend || !g_r.sampler || !g_r.rasteriser || !g_r.depth || !g_r.whiteView) {
        releaseDeviceResources();
        return false;
    }

    g_atlas.releaseDevice();
    LOG_INFO("D3D", "renderer resources built for device {}", static_cast<void*>(device));
    return true;
}

void D3DRenderer::releaseDeviceResources() {
    g_atlas.releaseDevice();
    g_r.releaseAll();
    m_ready = false;
}

bool D3DRenderer::beginFrame(ID3D11Device* device, ID3D11DeviceContext* context,
                             ID3D11RenderTargetView* target, float width, float height) {
    if (!device || !context || !target)
        return false;

    if (!ensureResources(device))
        return false;

    m_width = width;
    m_height = height;
    g_scissorStack.clear();
    g_r.pipeMode = 0;
    g_r.boundView = nullptr;

    saveState(context);

    const D3D11_VIEWPORT viewport{0.0f, 0.0f, width, height, 0.0f, 1.0f};
    const D3D11_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    const FLOAT blendFactor[4]{0, 0, 0, 0};

    context->OMSetRenderTargets(1, &target, nullptr);
    context->OMSetBlendState(g_r.blend, blendFactor, 0xFFFFFFFF);
    context->OMSetDepthStencilState(g_r.depth, 0);
    context->RSSetState(g_r.rasteriser);
    context->RSSetViewports(1, &viewport);
    context->RSSetScissorRects(1, &scissor);
    context->GSSetShader(nullptr, nullptr, 0);
    context->HSSetShader(nullptr, nullptr, 0);
    context->DSSetShader(nullptr, nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &g_r.cbuffer);
    context->PSSetConstantBuffers(0, 1, &g_r.cbuffer);
    context->PSSetSamplers(0, 1, &g_r.sampler);

    m_ready = true;
    return true;
}

void D3DRenderer::endFrame() {
    if (!m_ready)
        return;
    restoreState(g_r.context);
    m_ready = false;
}

namespace {

void writeParams(const Params& params) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(g_r.context->Map(g_r.cbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, &params, sizeof(Params));
        g_r.context->Unmap(g_r.cbuffer, 0);
    }
}

void drawRectPrim(const Rect& area, const Colour& a, const Colour& b, float radius, float thickness,
                  float mode, float gradient, ID3D11ShaderResourceView* srv) {
    Params p{};
    p.screen[0] = D3DRenderer::get().size().x;
    p.screen[1] = D3DRenderer::get().size().y;
    p.rect[0] = area.left;
    p.rect[1] = area.top;
    p.rect[2] = area.right;
    p.rect[3] = area.bottom;
    p.colorA[0] = a.r; p.colorA[1] = a.g; p.colorA[2] = a.b; p.colorA[3] = a.a;
    p.colorB[0] = b.r; p.colorB[1] = b.g; p.colorB[2] = b.b; p.colorB[3] = b.a;
    p.p[0] = radius;
    p.p[1] = thickness;
    p.p[2] = mode;
    p.p[3] = gradient;
    p.p2[0] = 1.0f;
    writeParams(p);

    auto* c = g_r.context;
    ID3D11ShaderResourceView* view = srv ? srv : g_r.whiteView;

    if (g_r.pipeMode != 1) {
        c->VSSetShader(g_r.rectVS, nullptr, 0);
        c->PSSetShader(g_r.rectPS, nullptr, 0);
        c->IASetInputLayout(nullptr);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ID3D11Buffer* noBuffer = nullptr;
        UINT stride = 0, offset = 0;
        c->IASetVertexBuffers(0, 1, &noBuffer, &stride, &offset);
        g_r.pipeMode = 1;
        g_r.boundView = nullptr;
    }

    if (view != g_r.boundView) {
        c->PSSetShaderResources(0, 1, &view);
        g_r.boundView = view;
    }

    c->Draw(4, 0);
}

}  // namespace

void D3DRenderer::fillRect(const Rect& area, const Colour& colour, float radius) {
    if (!m_ready || colour.a <= 0.001f || area.width() <= 0.0f || area.height() <= 0.0f)
        return;
    drawRectPrim(area, colour, colour, radius, 0.0f, 0.0f, 0.0f, nullptr);
}

void D3DRenderer::outlineRect(const Rect& area, const Colour& colour, float thickness, float radius) {
    if (!m_ready || colour.a <= 0.001f || thickness <= 0.0f)
        return;
    drawRectPrim(area, colour, colour, radius, thickness, 0.0f, 0.0f, nullptr);
}

void D3DRenderer::gradientRect(const Rect& area, const Colour& from, const Colour& to, bool vertical,
                               float radius) {
    if (!m_ready || area.width() <= 0.0f || area.height() <= 0.0f)
        return;
    drawRectPrim(area, from, to, radius, 0.0f, 0.0f, vertical ? 1.0f : 2.0f, nullptr);
}

void D3DRenderer::image(ID3D11ShaderResourceView* texture, const Rect& dest, float opacity) {
    if (!m_ready || !texture || opacity <= 0.001f)
        return;
    const Colour tint{1.0f, 1.0f, 1.0f, opacity};
    drawRectPrim(dest, tint, tint, 0.0f, 0.0f, 1.0f, 0.0f, texture);
}

void D3DRenderer::polygon(const Vec2* points, size_t count, const Colour& colour) {
    if (!m_ready || !points || count < 3 || colour.a <= 0.001f)
        return;

    std::vector<GeoVertex> vertices;
    vertices.reserve((count - 2) * 3);
    for (size_t i = 1; i + 1 < count; ++i) {
        const Vec2 tri[3] = {points[0], points[i], points[i + 1]};
        for (const Vec2& v : tri)
            vertices.push_back({v.x, v.y, colour.r, colour.g, colour.b, colour.a});
    }

    const size_t bytes = vertices.size() * sizeof(GeoVertex);
    if (g_r.geoCapacity < vertices.size()) {
        release(g_r.geoBuffer);
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(bytes);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(g_r.device->CreateBuffer(&desc, nullptr, &g_r.geoBuffer)))
            return;
        g_r.geoCapacity = vertices.size();
    }

    auto* c = g_r.context;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(c->Map(g_r.geoBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;
    std::memcpy(mapped.pData, vertices.data(), bytes);
    c->Unmap(g_r.geoBuffer, 0);

    Params p{};
    p.screen[0] = m_width;
    p.screen[1] = m_height;
    writeParams(p);

    UINT stride = sizeof(GeoVertex);
    UINT offset = 0;
    if (g_r.pipeMode != 2) {
        c->VSSetShader(g_r.geoVS, nullptr, 0);
        c->PSSetShader(g_r.geoPS, nullptr, 0);
        c->IASetInputLayout(g_r.geoLayout);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_r.pipeMode = 2;
        g_r.boundView = nullptr;
    }
    c->IASetVertexBuffers(0, 1, &g_r.geoBuffer, &stride, &offset);
    c->Draw(static_cast<UINT>(vertices.size()), 0);
}

void D3DRenderer::text(const std::string& value, float x, float y, const Colour& colour, float size,
                       Weight weight) {
    if (!m_ready || value.empty() || colour.a <= 0.001f)
        return;

    const float baseline = std::round(y + g_atlas.ascent(weight, size));
    float penX = x;

    static std::vector<TexVertex> verts;
    verts.clear();
    verts.reserve(value.size() * 6);

    for (size_t i = 0; i < value.size();) {
        const uint32_t cp = nextCodepoint(value, i);
        const Glyph* g = g_atlas.glyph(g_r.device, g_r.context, cp, size, weight);
        if (!g)
            continue;
        if (!g->blank && g->w > 0.0f) {
            const float x0 = std::round(penX + g->bearingX);
            const float y0 = baseline + g->bearingY;
            const float x1 = x0 + g->w;
            const float y1 = y0 + g->h;
            const TexVertex tl{x0, y0, g->u0, g->v0, colour.r, colour.g, colour.b, colour.a};
            const TexVertex tr{x1, y0, g->u1, g->v0, colour.r, colour.g, colour.b, colour.a};
            const TexVertex bl{x0, y1, g->u0, g->v1, colour.r, colour.g, colour.b, colour.a};
            const TexVertex br{x1, y1, g->u1, g->v1, colour.r, colour.g, colour.b, colour.a};
            verts.push_back(tl);
            verts.push_back(tr);
            verts.push_back(bl);
            verts.push_back(bl);
            verts.push_back(tr);
            verts.push_back(br);
        }
        penX += g->advance;
    }

    if (verts.empty())
        return;

    const size_t bytes = verts.size() * sizeof(TexVertex);
    if (g_r.textCapacity < verts.size()) {
        release(g_r.textBuffer);
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(bytes);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(g_r.device->CreateBuffer(&desc, nullptr, &g_r.textBuffer)))
            return;
        g_r.textCapacity = verts.size();
    }

    auto* c = g_r.context;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(c->Map(g_r.textBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;
    std::memcpy(mapped.pData, verts.data(), bytes);
    c->Unmap(g_r.textBuffer, 0);

    Params p{};
    p.screen[0] = m_width;
    p.screen[1] = m_height;
    writeParams(p);

    UINT stride = sizeof(TexVertex);
    UINT offset = 0;
    c->VSSetShader(g_r.textVS, nullptr, 0);
    c->PSSetShader(g_r.textPS, nullptr, 0);
    c->PSSetShaderResources(0, 1, &g_atlas.view);
    c->IASetInputLayout(g_r.textLayout);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->IASetVertexBuffers(0, 1, &g_r.textBuffer, &stride, &offset);
    c->Draw(static_cast<UINT>(verts.size()), 0);

    g_r.pipeMode = 0;
    g_r.boundView = nullptr;
}

float D3DRenderer::measure(const std::string& value, float size, Weight weight) {
    float width = 0.0f;
    for (size_t i = 0; i < value.size();) {
        const uint32_t cp = nextCodepoint(value, i);
        const Glyph* g = g_atlas.glyph(g_r.device, g_r.context, cp, size, weight);
        if (g)
            width += g->advance;
    }
    return width;
}

float D3DRenderer::lineHeight(float size) const { return size * 1.35f; }

void D3DRenderer::pushScissor(const Rect& area) {
    Rect clip = area;
    if (!g_scissorStack.empty()) {
        const Rect& top = g_scissorStack.back();
        clip.left = std::max(clip.left, top.left);
        clip.top = std::max(clip.top, top.top);
        clip.right = std::min(clip.right, top.right);
        clip.bottom = std::min(clip.bottom, top.bottom);
    }
    g_scissorStack.push_back(clip);

    const D3D11_RECT r{static_cast<LONG>(clip.left), static_cast<LONG>(clip.top),
                       static_cast<LONG>(std::max(clip.left, clip.right)),
                       static_cast<LONG>(std::max(clip.top, clip.bottom))};
    if (g_r.context)
        g_r.context->RSSetScissorRects(1, &r);
}

void D3DRenderer::popScissor() {
    if (!g_scissorStack.empty())
        g_scissorStack.pop_back();

    if (!g_r.context)
        return;

    if (g_scissorStack.empty()) {
        const D3D11_RECT full{0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
        g_r.context->RSSetScissorRects(1, &full);
    } else {
        const Rect& clip = g_scissorStack.back();
        const D3D11_RECT r{static_cast<LONG>(clip.left), static_cast<LONG>(clip.top),
                           static_cast<LONG>(std::max(clip.left, clip.right)),
                           static_cast<LONG>(std::max(clip.top, clip.bottom))};
        g_r.context->RSSetScissorRects(1, &r);
    }
}

}  // namespace aerial::render
