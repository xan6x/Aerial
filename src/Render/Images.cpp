#include "Render/Images.h"

#include <Windows.h>
#include <d3d11.h>
#include <wincodec.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "Render/Overlay.h"
#include "Utils/Logger.h"

namespace aerial::render::images {
namespace {

struct Entry {
    ID3D11ShaderResourceView* view = nullptr;
    Vec2 size;
};

std::unordered_map<int, Entry> g_cache;
ID3D11Device* g_owner = nullptr;
uint64_t g_generation = 0;
IWICImagingFactory* g_wic = nullptr;

template <typename T>
void release(T*& object) {
    if (object) {
        object->Release();
        object = nullptr;
    }
}

IWICImagingFactory* wic() {
    if (g_wic)
        return g_wic;

    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE)
        return nullptr;

    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&g_wic)))) {
        LOG_ERROR("Images", "WIC is unavailable");
        return nullptr;
    }
    return g_wic;
}

HMODULE selfModule() {
    static HMODULE module = nullptr;
    if (!module) {
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&selfModule), &module);
    }
    return module;
}

Entry decode(int resourceId, ID3D11Device* device) {
    Entry entry;

    auto* factory = wic();
    if (!factory)
        return entry;

    const HRSRC info = FindResourceW(selfModule(), MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!info) {
        LOG_ERROR("Images", "resource {} not found in the DLL", resourceId);
        return entry;
    }

    const DWORD bytes = SizeofResource(selfModule(), info);
    const HGLOBAL handle = LoadResource(selfModule(), info);
    void* data = handle ? LockResource(handle) : nullptr;
    if (!data || bytes == 0)
        return entry;

    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;

    HRESULT hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr))
        hr = stream->InitializeFromMemory(static_cast<BYTE*>(data), bytes);
    if (SUCCEEDED(hr))
        hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(hr))
        hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr))
        hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr))
        hr = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                   nullptr, 0.0, WICBitmapPaletteTypeMedianCut);

    UINT width = 0;
    UINT height = 0;
    std::vector<uint8_t> pixels;
    if (SUCCEEDED(hr))
        hr = converter->GetSize(&width, &height);
    if (SUCCEEDED(hr) && width > 0 && height > 0) {
        pixels.resize(static_cast<size_t>(width) * height * 4);
        const UINT stride = width * 4;
        hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data());
    }

    release(converter);
    release(frame);
    release(decoder);
    release(stream);

    if (FAILED(hr) || pixels.empty()) {
        LOG_ERROR("Images", "resource {} failed to decode ({:#x})", resourceId,
                  static_cast<uint32_t>(hr));
        return entry;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = pixels.data();
    init.SysMemPitch = width * 4;

    ID3D11Texture2D* texture = nullptr;
    if (FAILED(device->CreateTexture2D(&desc, &init, &texture))) {
        LOG_ERROR("Images", "resource {} texture creation failed", resourceId);
        return entry;
    }

    hr = device->CreateShaderResourceView(texture, nullptr, &entry.view);
    release(texture);
    if (FAILED(hr)) {
        entry.view = nullptr;
        return entry;
    }

    entry.size = {static_cast<float>(width), static_cast<float>(height)};
    LOG_INFO("Images", "resource {} loaded, {}x{}", resourceId, width, height);
    return entry;
}

}

ID3D11ShaderResourceView* get(int resourceId) {
    auto& overlay = Overlay::get();
    ID3D11Device* device = overlay.device();
    if (!overlay.ready() || !device)
        return nullptr;

    if (g_owner != device || g_generation != overlay.generation()) {
        releaseAll();
        g_owner = device;
        g_generation = overlay.generation();
    }

    const auto it = g_cache.find(resourceId);
    if (it != g_cache.end())
        return it->second.view;

    const Entry entry = decode(resourceId, device);
    g_cache[resourceId] = entry;
    return entry.view;
}

Vec2 size(int resourceId) {
    if (!get(resourceId))
        return {};
    return g_cache[resourceId].size;
}

void releaseAll() {
    for (auto& [id, entry] : g_cache)
        release(entry.view);
    g_cache.clear();
    g_owner = nullptr;
}

}
