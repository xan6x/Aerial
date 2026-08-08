#include "Render/Images.h"

#include <Windows.h>
#include <d2d1_1.h>
#include <wincodec.h>

#include <unordered_map>

#include "Render/D2DOverlay.h"
#include "Utils/Logger.h"

namespace aerial::render::images {
namespace {

struct Entry {
    ID2D1Bitmap* bitmap = nullptr;
    Vec2 size;
};

std::unordered_map<int, Entry> g_cache;
ID2D1DeviceContext* g_owner = nullptr;
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

Entry decode(int resourceId, ID2D1DeviceContext* context) {
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
    if (SUCCEEDED(hr)) {

        hr = converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                   nullptr, 0.0, WICBitmapPaletteTypeMedianCut);
    }
    if (SUCCEEDED(hr))
        hr = context->CreateBitmapFromWicBitmap(converter, nullptr, &entry.bitmap);

    release(converter);
    release(frame);
    release(decoder);
    release(stream);

    if (FAILED(hr)) {
        LOG_ERROR("Images", "resource {} failed to decode ({:#x})", resourceId,
                  static_cast<uint32_t>(hr));
        return entry;
    }

    const auto size = entry.bitmap->GetSize();
    entry.size = {size.width, size.height};
    LOG_INFO("Images", "resource {} loaded, {}x{}", resourceId, static_cast<int>(size.width),
             static_cast<int>(size.height));
    return entry;
}

}

ID2D1Bitmap* get(int resourceId) {
    auto* context = D2DOverlay::get().context();
    if (!D2DOverlay::get().ready() || !context)
        return nullptr;

    if (g_owner != context || g_generation != D2DOverlay::get().generation()) {
        releaseAll();
        g_owner = context;
        g_generation = D2DOverlay::get().generation();
    }

    const auto it = g_cache.find(resourceId);
    if (it != g_cache.end())
        return it->second.bitmap;

    const Entry entry = decode(resourceId, context);
    g_cache[resourceId] = entry;
    return entry.bitmap;
}

Vec2 size(int resourceId) {
    if (!get(resourceId))
        return {};
    return g_cache[resourceId].size;
}

void releaseAll() {
    for (auto& [id, entry] : g_cache)
        release(entry.bitmap);
    g_cache.clear();
    g_owner = nullptr;
}

}
