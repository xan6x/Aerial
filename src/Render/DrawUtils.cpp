#include "Render/DrawUtils.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <vector>

#include "Render/Overlay.h"
#include "Render/D3DRenderer.h"
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

std::vector<bool> g_clipStack;

std::vector<Rect> g_softClip;

Rect intersect(const Rect& a, const Rect& b) {
    return {std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right),
            std::min(a.bottom, b.bottom)};
}

const Rect* softClip() { return g_softClip.empty() ? nullptr : &g_softClip.back(); }

using ScreenRendererSingletonFn = void*(__fastcall*)();
using ScreenRendererFillFn = void(__fastcall*)(void*, float, float, float, float, const sdk::Color*);

void legacyFill(const Rect& area, const Colour& colour) {
    static auto singleton =
        reinterpret_cast<ScreenRendererSingletonFn>(memory::rva(offsets::func::ScreenRenderer_singleton));
    static auto fill =
        reinterpret_cast<ScreenRendererFillFn>(memory::rva(offsets::func::ScreenRenderer_fill));

    Rect visible = area;
    if (const Rect* clip = softClip()) {
        visible = intersect(area, *clip);
        if (visible.width() <= 0.0f || visible.height() <= 0.0f) {
            g_fillsSkipped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }

    void* renderer = singleton();
    if (!renderer) {
        g_fillsSkipped.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    fill(renderer, visible.left, visible.top, visible.right, visible.bottom, &colour);
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

D3DRenderer::Weight mapWeight(DrawUtils::Weight weight) {
    switch (weight) {
    case DrawUtils::Weight::Medium:   return D3DRenderer::Weight::Medium;
    case DrawUtils::Weight::SemiBold: return D3DRenderer::Weight::SemiBold;
    case DrawUtils::Weight::Bold:     return D3DRenderer::Weight::Bold;
    case DrawUtils::Weight::Regular:  break;
    }
    return D3DRenderer::Weight::Regular;
}

}

bool DrawUtils::usingD2D() { return Overlay::get().ready(); }

const char* DrawUtils::backendName() { return usingD2D() ? "Direct3D 11" : "game renderer"; }

void DrawUtils::beginFrame() {
    g_clipStack.clear();
    g_softClip.clear();

    if (usingD2D()) {
        g_screenSize = Overlay::get().size();
    } else {
        const SIZE client = platform::clientSize();
        const float scale = legacyScale();
        const float width = static_cast<float>(client.cx);
        const float height = static_cast<float>(client.cy);
        g_screenSize = {width > 0.0f ? width / scale : 640.0f, height > 0.0f ? height / scale : 360.0f};
    }

    g_uiScale = std::max(0.35f, g_screenSize.y / 1000.0f);

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

    if (!usingD2D()) {
        legacyFill(area, colour);
        return;
    }

    D3DRenderer::get().fillRect(area, colour, radius);
    g_fills.fetch_add(1, std::memory_order_relaxed);
}

void DrawUtils::outline(const Rect& area, const Colour& colour, float thickness, float radius) {
    if (colour.a <= 0.001f || thickness <= 0.0f)
        return;

    if (!usingD2D()) {
        legacyFill({area.left, area.top, area.right, area.top + thickness}, colour);
        legacyFill({area.left, area.bottom - thickness, area.right, area.bottom}, colour);
        legacyFill({area.left, area.top + thickness, area.left + thickness, area.bottom - thickness},
                   colour);
        legacyFill({area.right - thickness, area.top + thickness, area.right, area.bottom - thickness},
                   colour);
        return;
    }

    D3DRenderer::get().outlineRect(area, colour, thickness, radius);
    g_fills.fetch_add(1, std::memory_order_relaxed);
}

void DrawUtils::gradient(const Rect& area, const Colour& from, const Colour& to, bool vertical,
                         float radius) {
    if (!usingD2D()) {
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

    D3DRenderer::get().gradientRect(area, from, to, vertical, radius);
    g_fills.fetch_add(1, std::memory_order_relaxed);
}

void DrawUtils::shadow(const Rect& area, const Colour& colour, float blur, float radius,
                       const Vec2& offset) {
    if (!usingD2D()) {
        legacyFill(area.offset(offset), colour.withAlpha(colour.a * 0.5f));
        return;
    }

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
    fill(area, Colour::rgb(0x0E1117, 0.88f), radius);
}

void DrawUtils::text(const std::string& value, const Vec2& position, const Colour& colour, float size,
                     Weight weight, Align align) {
    if (value.empty() || colour.a <= 0.001f)
        return;

    if (!usingD2D()) {
        auto* font = sdk::Context::get().font();
        if (!font) {
            g_textsSkipped.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        float x = position.x;
        const float width = font->width(value, 1.0f);
        if (align != Align::Left)
            x -= align == Align::Centre ? width * 0.5f : width;

        if (const Rect* clip = softClip()) {
            const float line = textHeight(size);
            if (position.y < clip->top || position.y + line > clip->bottom ||
                x + width < clip->left || x > clip->right) {
                g_textsSkipped.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }

        font->draw(value, x, position.y, colour, true);
        g_texts.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    auto& renderer = D3DRenderer::get();
    const D3DRenderer::Weight w = mapWeight(weight);

    float x = position.x;
    if (align != Align::Left) {
        const float width = renderer.measure(value, size, w);
        x -= align == Align::Centre ? width * 0.5f : width;
    }

    renderer.text(value, x, position.y, colour, size, w);
    g_texts.fetch_add(1, std::memory_order_relaxed);
}

float DrawUtils::textWidth(const std::string& value, float size, Weight weight) {
    if (value.empty())
        return 0.0f;

    if (!usingD2D()) {
        auto* font = sdk::Context::get().font();
        return font ? font->width(value, 1.0f) : 0.0f;
    }

    return D3DRenderer::get().measure(value, size, mapWeight(weight));
}

float DrawUtils::textHeight(float size) { return usingD2D() ? size * 1.35f : 10.0f; }

std::string DrawUtils::fit(const std::string& value, float maxWidth, float size, Weight weight) {
    if (value.empty() || maxWidth <= 0.0f)
        return {};
    if (textWidth(value, size, weight) <= maxWidth)
        return value;

    const std::string ellipsis = usingD2D() ? "\xE2\x80\xA6" : "...";
    const float ellipsisWidth = textWidth(ellipsis, size, weight);
    if (ellipsisWidth >= maxWidth)
        return {};

    size_t length = value.size();
    while (length > 0) {
        while (length > 0 && (static_cast<unsigned char>(value[length - 1]) & 0xC0) == 0x80)
            --length;
        if (length == 0)
            break;
        --length;

        const std::string candidate = value.substr(0, length);
        if (textWidth(candidate, size, weight) + ellipsisWidth <= maxWidth) {
            const size_t end = candidate.find_last_not_of(' ');
            return (end == std::string::npos ? candidate : candidate.substr(0, end + 1)) + ellipsis;
        }
    }
    return ellipsis;
}

void DrawUtils::image(int resourceId, const Rect& dest, float opacity) {
    if (!usingD2D() || opacity <= 0.001f)
        return;

    auto* view = images::get(resourceId);
    if (!view)
        return;

    D3DRenderer::get().image(view, dest, opacity);
    g_fills.fetch_add(1, std::memory_order_relaxed);
}

float DrawUtils::imageAspect(int resourceId) {
    const Vec2 size = images::size(resourceId);
    return size.y > 0.0f ? size.x / size.y : 0.0f;
}

void DrawUtils::polygon(const Vec2* points, size_t count, const Colour& colour) {
    if (!points || count < 3 || colour.a <= 0.001f)
        return;

    if (!usingD2D()) {
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

    D3DRenderer::get().polygon(points, count, colour);
    g_fills.fetch_add(1, std::memory_order_relaxed);
}

void DrawUtils::pushClip(const Rect& area, float radius) {
    (void)radius;

    g_softClip.push_back(g_softClip.empty() ? area : intersect(area, g_softClip.back()));

    if (usingD2D()) {
        D3DRenderer::get().pushScissor(area);
        g_clipStack.push_back(true);
    } else {
        g_clipStack.push_back(false);
    }
}

void DrawUtils::popClip() {
    if (!g_softClip.empty())
        g_softClip.pop_back();

    if (g_clipStack.empty())
        return;

    const bool hadScissor = g_clipStack.back();
    g_clipStack.pop_back();

    if (hadScissor)
        D3DRenderer::get().popScissor();
}

void DrawUtils::releaseResources() {
    g_clipStack.clear();
    g_softClip.clear();
}

}
