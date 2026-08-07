#include "Render/DrawUtils.h"

#include <Windows.h>
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <dwrite.h>

#include <algorithm>
#include <atomic>
#include <unordered_map>
#include <vector>

#include "Render/D2DOverlay.h"
#include "Render/Images.h"
#include "SDK/ClientInstance.h"
#include "SDK/Context.h"
#include "SDK/Offsets.h"
#include "Utils/Logger.h"
#include "Utils/Memory.h"
#include "Utils/Platform.h"

namespace aerial::render {
namespace {

std::atomic<uint32_t> g_fills{0};
std::atomic<uint32_t> g_fillsSkipped{0};
std::atomic<uint32_t> g_texts{0};
std::atomic<uint32_t> g_textsSkipped{0};

float g_scaleOverride = 0.0f;
Vec2 g_screenSize{};
float g_uiScale = 1.0f;
// Remembers, per push, whether a layer or an axis-aligned clip was used, so the
// matching pop calls the right thing.
std::vector<bool> g_clipStack;

// ── Game renderer backend ────────────────────────────────────────────────────

using ScreenRendererSingletonFn = void*(__fastcall*)();
using ScreenRendererFillFn = void(__fastcall*)(void*, float, float, float, float, const sdk::Color*);

void legacyFill(const Rect& area, const Colour& colour) {
    static auto singleton =
        reinterpret_cast<ScreenRendererSingletonFn>(memory::rva(offsets::func::ScreenRenderer_singleton));
    static auto fill =
        reinterpret_cast<ScreenRendererFillFn>(memory::rva(offsets::func::ScreenRenderer_fill));

    void* renderer = singleton();
    if (!renderer) {
        g_fillsSkipped.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    fill(renderer, area.left, area.top, area.right, area.bottom, &colour);
    g_fills.fetch_add(1, std::memory_order_relaxed);
}

float legacyScale() {
    if (g_scaleOverride > 0.0f)
        return g_scaleOverride;

    const auto* value = memory::at<float>(offsets::data::GuiData_GuiScale);
    if (memory::isReadable(value, sizeof(float))) {
        const float scale = *value;
        if (scale >= 0.5f && scale <= 8.0f)
            return scale;
    }
    return 2.0f;
}

// ── Direct2D backend ─────────────────────────────────────────────────────────

D2D1_COLOR_F toD2D(const Colour& colour) {
    return D2D1::ColorF(colour.r, colour.g, colour.b, colour.a);
}

D2D1_RECT_F toD2D(const Rect& area) {
    return D2D1::RectF(area.left, area.top, area.right, area.bottom);
}

ID2D1SolidColorBrush* brush(const Colour& colour) {
    static ID2D1SolidColorBrush* cached = nullptr;
    static ID2D1DeviceContext* owner = nullptr;

    auto* context = D2DOverlay::get().context();
    if (!context)
        return nullptr;

    if (owner != context) {
        if (cached)
            cached->Release();
        cached = nullptr;
        owner = context;
    }
    if (!cached && FAILED(context->CreateSolidColorBrush(toD2D(colour), &cached)))
        return nullptr;

    cached->SetColor(toD2D(colour));
    cached->SetOpacity(1.0f);
    return cached;
}

DWRITE_FONT_WEIGHT toDWrite(DrawUtils::Weight weight) {
    switch (weight) {
    case DrawUtils::Weight::Medium:   return DWRITE_FONT_WEIGHT_MEDIUM;
    case DrawUtils::Weight::SemiBold: return DWRITE_FONT_WEIGHT_SEMI_BOLD;
    case DrawUtils::Weight::Bold:     return DWRITE_FONT_WEIGHT_BOLD;
    case DrawUtils::Weight::Regular:  break;
    }
    return DWRITE_FONT_WEIGHT_NORMAL;
}

// Text formats are immutable, so one per size/weight pair is cached for the
// lifetime of the overlay rather than rebuilt per draw.
IDWriteTextFormat* textFormat(float size, DrawUtils::Weight weight) {
    static std::unordered_map<uint64_t, IDWriteTextFormat*> cache;

    auto* dwrite = D2DOverlay::get().dwrite();
    if (!dwrite)
        return nullptr;

    const auto key = (static_cast<uint64_t>(size * 16.0f) << 8) | static_cast<uint64_t>(weight);
    if (const auto it = cache.find(key); it != cache.end())
        return it->second;

    IDWriteTextFormat* format = nullptr;
    if (FAILED(dwrite->CreateTextFormat(L"Segoe UI", nullptr, toDWrite(weight),
                                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
                                        L"en-us", &format)))
        return nullptr;

    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    cache[key] = format;
    return format;
}

std::wstring widen(const std::string& value) {
    if (value.empty())
        return {};
    const int length =
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), wide.data(), length);
    return wide;
}

IDWriteTextLayout* layoutFor(const std::string& value, float size, DrawUtils::Weight weight) {
    auto* dwrite = D2DOverlay::get().dwrite();
    auto* format = textFormat(size, weight);
    if (!dwrite || !format)
        return nullptr;

    const std::wstring wide = widen(value);
    IDWriteTextLayout* layout = nullptr;
    if (FAILED(dwrite->CreateTextLayout(wide.c_str(), static_cast<UINT32>(wide.size()), format,
                                        100000.0f, 100000.0f, &layout)))
        return nullptr;
    return layout;
}

D2D1_ROUNDED_RECT rounded(const Rect& area, float radius) {
    const float limit = std::min(area.width(), area.height()) * 0.5f;
    return D2D1::RoundedRect(toD2D(area), std::min(radius, limit), std::min(radius, limit));
}


} // namespace

bool DrawUtils::usingD2D() { return D2DOverlay::get().ready(); }

const char* DrawUtils::backendName() { return usingD2D() ? "Direct2D" : "game renderer"; }

void DrawUtils::beginFrame() {
    if (usingD2D()) {
        g_screenSize = D2DOverlay::get().size();
    } else {
        const SIZE client = platform::clientSize();
        const float scale = legacyScale();
        const float width = static_cast<float>(client.cx);
        const float height = static_cast<float>(client.cy);
        g_screenSize = {width > 0.0f ? width / scale : 640.0f, height > 0.0f ? height / scale : 360.0f};
    }

    // Layout is authored against a 1000-unit-tall screen.
    g_uiScale = std::max(0.35f, g_screenSize.y / 1000.0f);

    // Log again whenever the backend changes: the first line is always written
    // before the Direct2D overlay has had a frame to attach, so a single log
    // would permanently claim the fallback is in use.
    static bool lastBackendWasD2D = false;
    static bool logged = false;
    if (g_screenSize.y > 0.0f && (!logged || lastBackendWasD2D != usingD2D())) {
        logged = true;
        lastBackendWasD2D = usingD2D();
        LOG_INFO("Draw", "backend {}, {}x{}, ui scale {:.2f}", backendName(),
                 static_cast<int>(g_screenSize.x), static_cast<int>(g_screenSize.y), g_uiScale);
    }
}

Vec2 DrawUtils::screenSize() {
    if (g_screenSize.x <= 0.0f)
        beginFrame();
    return g_screenSize;
}

float DrawUtils::uiScale() { return g_uiScale; }

void DrawUtils::setScaleOverride(float scale) { g_scaleOverride = scale; }

float DrawUtils::scale() { return usingD2D() ? 1.0f : legacyScale(); }

DrawUtils::Stats DrawUtils::stats() {
    return {g_fills.load(std::memory_order_relaxed), g_fillsSkipped.load(std::memory_order_relaxed),
            g_texts.load(std::memory_order_relaxed), g_textsSkipped.load(std::memory_order_relaxed)};
}

void DrawUtils::fill(const Rect& area, const Colour& colour, float radius) {
    if (colour.a <= 0.001f || area.width() <= 0.0f || area.height() <= 0.0f)
        return;

    auto* context = D2DOverlay::get().context();
    if (!usingD2D() || !context) {
        legacyFill(area, colour);
        return;
    }

    auto* paint = brush(colour);
    if (!paint) {
        g_fillsSkipped.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    if (radius > 0.5f)
        context->FillRoundedRectangle(rounded(area, radius), paint);
    else
        context->FillRectangle(toD2D(area), paint);

    g_fills.fetch_add(1, std::memory_order_relaxed);
}

void DrawUtils::outline(const Rect& area, const Colour& colour, float thickness, float radius) {
    if (colour.a <= 0.001f || thickness <= 0.0f)
        return;

    auto* context = D2DOverlay::get().context();
    if (!usingD2D() || !context) {
        legacyFill({area.left, area.top, area.right, area.top + thickness}, colour);
        legacyFill({area.left, area.bottom - thickness, area.right, area.bottom}, colour);
        legacyFill({area.left, area.top + thickness, area.left + thickness, area.bottom - thickness},
                   colour);
        legacyFill({area.right - thickness, area.top + thickness, area.right, area.bottom - thickness},
                   colour);
        return;
    }

    auto* paint = brush(colour);
    if (!paint)
        return;

    // Inset by half the stroke so the outline stays inside `area`.
    const Rect inner = area.inset(thickness * 0.5f);
    if (radius > 0.5f)
        context->DrawRoundedRectangle(rounded(inner, radius - thickness * 0.5f), paint, thickness);
    else
        context->DrawRectangle(toD2D(inner), paint, thickness);

    g_fills.fetch_add(1, std::memory_order_relaxed);
}

void DrawUtils::gradient(const Rect& area, const Colour& from, const Colour& to, bool vertical,
                         float radius) {
    auto* context = D2DOverlay::get().context();
    if (!usingD2D() || !context) {
        // Approximate with strips.
        constexpr int kSteps = 16;
        const float step = (vertical ? area.height() : area.width()) / kSteps;
        for (int i = 0; i < kSteps; ++i) {
            const float t = (static_cast<float>(i) + 0.5f) / kSteps;
            const float offset = step * static_cast<float>(i);
            const Rect strip = vertical
                                   ? Rect{area.left, area.top + offset, area.right,
                                          area.top + offset + step + 0.5f}
                                   : Rect{area.left + offset, area.top,
                                          area.left + offset + step + 0.5f, area.bottom};
            legacyFill(strip, from.lerp(to, t));
        }
        return;
    }

    const D2D1_GRADIENT_STOP stops[] = {{0.0f, toD2D(from)}, {1.0f, toD2D(to)}};

    ID2D1GradientStopCollection* collection = nullptr;
    if (FAILED(context->CreateGradientStopCollection(stops, 2, &collection)))
        return;

    ID2D1LinearGradientBrush* paint = nullptr;
    const auto properties = D2D1::LinearGradientBrushProperties(
        D2D1::Point2F(area.left, area.top),
        vertical ? D2D1::Point2F(area.left, area.bottom) : D2D1::Point2F(area.right, area.top));

    if (SUCCEEDED(context->CreateLinearGradientBrush(properties, collection, &paint))) {
        if (radius > 0.5f)
            context->FillRoundedRectangle(rounded(area, radius), paint);
        else
            context->FillRectangle(toD2D(area), paint);
        paint->Release();
        g_fills.fetch_add(1, std::memory_order_relaxed);
    }
    collection->Release();
}

void DrawUtils::shadow(const Rect& area, const Colour& colour, float blur, float radius,
                       const Vec2& offset) {
    auto* context = D2DOverlay::get().context();
    if (!usingD2D() || !context) {
        legacyFill(area.offset(offset), colour.withAlpha(colour.a * 0.5f));
        return;
    }

    // Concentric rounded rectangles with falling alpha: cheaper than a real
    // shadow effect and, at these sizes, indistinguishable.
    const int rings = std::max(2, static_cast<int>(blur));
    for (int i = rings; i > 0; --i) {
        const float t = static_cast<float>(i) / rings;
        const float spread = blur * t;
        const Rect ring = area.offset(offset).inset(-spread);
        fill(ring, colour.withAlpha(colour.a * (1.0f - t) * 0.35f), radius + spread);
    }
}

void DrawUtils::blurBehind(const Rect& area, float radius, float strength) {
    (void)strength;

    // Real backdrop blur needs the game's frame as a source. Since the overlay
    // moved onto its own device, the Direct2D target holds only our own
    // (transparent) surface, so there is nothing here to blur - sampling it
    // would cost a full-screen copy per frame and show nothing. Until the
    // game's back buffer is shared back the other way, this draws the tinted
    // panel the frosted look falls back to.
    fill(area, Colour::rgb(0x0E1117, 0.88f), radius);
}


void DrawUtils::text(const std::string& value, const Vec2& position, const Colour& colour, float size,
                     Weight weight, Align align) {
    if (value.empty() || colour.a <= 0.001f)
        return;

    auto* context = D2DOverlay::get().context();
    if (!usingD2D() || !context) {
        auto* font = sdk::Context::get().font();
        if (!font) {
            g_textsSkipped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        // The bitmap font has one size; only alignment can be honoured.
        float x = position.x;
        if (align != Align::Left) {
            const float width = font->width(value, 1.0f);
            x -= align == Align::Centre ? width * 0.5f : width;
        }
        font->draw(value, x, position.y, colour, true);
        g_texts.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    auto* layout = layoutFor(value, size, weight);
    auto* paint = brush(colour);
    if (!layout || !paint) {
        g_textsSkipped.fetch_add(1, std::memory_order_relaxed);
        if (layout)
            layout->Release();
        return;
    }

    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);

    float x = position.x;
    if (align == Align::Centre)
        x -= metrics.width * 0.5f;
    else if (align == Align::Right)
        x -= metrics.width;

    context->DrawTextLayout(D2D1::Point2F(x, position.y), layout, paint,
                            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    layout->Release();
    g_texts.fetch_add(1, std::memory_order_relaxed);
}

float DrawUtils::textWidth(const std::string& value, float size, Weight weight) {
    if (value.empty())
        return 0.0f;

    if (!usingD2D()) {
        auto* font = sdk::Context::get().font();
        return font ? font->width(value, 1.0f) : 0.0f;
    }

    auto* layout = layoutFor(value, size, weight);
    if (!layout)
        return 0.0f;

    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    layout->Release();
    return metrics.width;
}

float DrawUtils::textHeight(float size) { return usingD2D() ? size * 1.35f : 10.0f; }

std::string DrawUtils::fit(const std::string& value, float maxWidth, float size, Weight weight) {
    if (value.empty() || maxWidth <= 0.0f)
        return {};
    if (textWidth(value, size, weight) <= maxWidth)
        return value;

    // The bitmap font has no ellipsis glyph, so spell it out there.
    const std::string ellipsis = usingD2D() ? "\xE2\x80\xA6" : "...";
    const float ellipsisWidth = textWidth(ellipsis, size, weight);
    if (ellipsisWidth >= maxWidth)
        return {};

    size_t length = value.size();
    while (length > 0) {
        // Never cut inside a UTF-8 sequence.
        while (length > 0 && (static_cast<unsigned char>(value[length - 1]) & 0xC0) == 0x80)
            --length;
        if (length == 0)
            break;
        --length;

        const std::string candidate = value.substr(0, length);
        if (textWidth(candidate, size, weight) + ellipsisWidth <= maxWidth) {
            // Trim a trailing space so the ellipsis hugs the last word.
            const size_t end = candidate.find_last_not_of(' ');
            return (end == std::string::npos ? candidate : candidate.substr(0, end + 1)) + ellipsis;
        }
    }
    return ellipsis;
}

void DrawUtils::image(int resourceId, const Rect& dest, float opacity) {
    auto* context = D2DOverlay::get().context();
    if (!usingD2D() || !context || opacity <= 0.001f)
        return;

    auto* bitmap = images::get(resourceId);
    if (!bitmap)
        return;

    context->DrawBitmap(bitmap, toD2D(dest), opacity,
                        D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
    g_fills.fetch_add(1, std::memory_order_relaxed);
}

void DrawUtils::polygon(const Vec2* points, size_t count, const Colour& colour) {
    if (!points || count < 3 || colour.a <= 0.001f)
        return;

    auto* context = D2DOverlay::get().context();
    if (!usingD2D() || !context) {
        Rect bounds{points[0].x, points[0].y, points[0].x, points[0].y};
        for (size_t i = 1; i < count; ++i) {
            bounds.left = std::min(bounds.left, points[i].x);
            bounds.top = std::min(bounds.top, points[i].y);
            bounds.right = std::max(bounds.right, points[i].x);
            bounds.bottom = std::max(bounds.bottom, points[i].y);
        }
        legacyFill(bounds, colour);
        return;
    }

    ID2D1Factory* factory = nullptr;
    context->GetFactory(&factory);
    if (!factory)
        return;

    ID2D1PathGeometry* path = nullptr;
    HRESULT hr = factory->CreatePathGeometry(&path);
    factory->Release();
    if (FAILED(hr))
        return;

    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(path->Open(&sink))) {
        sink->BeginFigure(D2D1::Point2F(points[0].x, points[0].y), D2D1_FIGURE_BEGIN_FILLED);
        for (size_t i = 1; i < count; ++i)
            sink->AddLine(D2D1::Point2F(points[i].x, points[i].y));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        sink->Release();

        if (auto* paint = brush(colour)) {
            context->FillGeometry(path, paint);
            g_fills.fetch_add(1, std::memory_order_relaxed);
        }
    }
    path->Release();
}

float DrawUtils::imageAspect(int resourceId) {
    const Vec2 size = images::size(resourceId);
    return size.y > 0.0f ? size.x / size.y : 0.0f;
}

void DrawUtils::pushClip(const Rect& area, float radius) {
    auto* context = D2DOverlay::get().context();
    if (!usingD2D() || !context) {
        g_clipStack.push_back(false);
        return;
    }

    if (radius <= 0.5f) {
        context->PushAxisAlignedClip(toD2D(area), D2D1_ANTIALIAS_MODE_ALIASED);
        g_clipStack.push_back(false);
        return;
    }

    // A rounded clip needs a layer with a geometric mask; an axis-aligned clip
    // would let artwork square off the window's corners.
    ID2D1Factory* factory = nullptr;
    context->GetFactory(&factory);

    ID2D1RoundedRectangleGeometry* geometry = nullptr;
    if (factory) {
        ID2D1Factory1* factory1 = nullptr;
        if (SUCCEEDED(factory->QueryInterface(__uuidof(ID2D1Factory1),
                                              reinterpret_cast<void**>(&factory1)))) {
            factory1->CreateRoundedRectangleGeometry(rounded(area, radius), &geometry);
            factory1->Release();
        }
        factory->Release();
    }

    if (!geometry) {
        context->PushAxisAlignedClip(toD2D(area), D2D1_ANTIALIAS_MODE_ALIASED);
        g_clipStack.push_back(false);
        return;
    }

    context->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), geometry), nullptr);
    geometry->Release();
    g_clipStack.push_back(true);
}

void DrawUtils::popClip() {
    if (g_clipStack.empty())
        return;

    const bool wasLayer = g_clipStack.back();
    g_clipStack.pop_back();

    auto* context = D2DOverlay::get().context();
    if (!usingD2D() || !context)
        return;

    if (wasLayer)
        context->PopLayer();
    else
        context->PopAxisAlignedClip();
}

} // namespace aerial::render
