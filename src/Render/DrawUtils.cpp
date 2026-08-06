#include "Render/DrawUtils.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>

#include "SDK/ClientInstance.h"
#include "SDK/Context.h"
#include "SDK/Offsets.h"
#include "Utils/Logger.h"
#include "Utils/Memory.h"
#include "Utils/Platform.h"

namespace aerial::render {
namespace {

using ScreenRendererSingletonFn = void*(__fastcall*)();
using ScreenRendererFillFn = void(__fastcall*)(void*, float, float, float, float, const sdk::Color*);

std::atomic<uint32_t> g_fills{0};
std::atomic<uint32_t> g_fillsSkipped{0};
std::atomic<uint32_t> g_texts{0};
std::atomic<uint32_t> g_textsSkipped{0};
bool g_probeEnabled = false;

void* screenRenderer() {
    static auto singleton =
        reinterpret_cast<ScreenRendererSingletonFn>(memory::rva(offsets::func::ScreenRenderer_singleton));

    void* renderer = singleton();

    static bool logged = false;
    if (!logged) {
        logged = true;
        if (renderer)
            LOG_INFO("Draw", "ScreenRenderer::singleton() = {}", renderer);
        else
            LOG_ERROR("Draw", "ScreenRenderer::singleton() returned null - every fill is a no-op");
    }
    return renderer;
}

void rawFill(float x0, float y0, float x1, float y1, const Colour& colour) {
    static auto fill =
        reinterpret_cast<ScreenRendererFillFn>(memory::rva(offsets::func::ScreenRenderer_fill));

    void* renderer = screenRenderer();
    if (!renderer) {
        g_fillsSkipped.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    fill(renderer, x0, y0, x1, y1, &colour);
    g_fills.fetch_add(1, std::memory_order_relaxed);
}

float g_scaleOverride = 0.0f;
Vec2 g_screenSize{};
bool g_logged = false;

// The game stores its current GUI scale in a global; the UI viewport is the
// window's client size divided by it.
float detectScale() {
    if (g_scaleOverride > 0.0f)
        return g_scaleOverride;

    const auto* value = memory::at<float>(offsets::data::GuiData_GuiScale);
    if (memory::isReadable(value, sizeof(float))) {
        const float scale = *value;
        if (scale >= 0.5f && scale <= 8.0f)
            return scale;
    }
    return 2.0f; // conservative default; override with setScaleOverride()
}

} // namespace

void DrawUtils::beginFrame() {
    const float scale = detectScale();

    const SIZE client = platform::clientSize();
    const float width = static_cast<float>(client.cx);
    const float height = static_cast<float>(client.cy);
    g_screenSize = {width > 0.0f ? width / scale : 640.0f, height > 0.0f ? height / scale : 360.0f};

    if (!g_logged) {
        g_logged = true;
        LOG_INFO("Draw", "viewport {}x{} px, gui scale {} -> {}x{} units",
                 static_cast<int>(width), static_cast<int>(height), scale,
                 static_cast<int>(g_screenSize.x), static_cast<int>(g_screenSize.y));
    }
}

Vec2 DrawUtils::screenSize() {
    if (g_screenSize.x <= 0.0f)
        beginFrame();
    return g_screenSize;
}

void DrawUtils::setScaleOverride(float scale) {
    g_scaleOverride = scale;
    g_logged = false;
}

float DrawUtils::scale() { return detectScale(); }

bool DrawUtils::ready() { return sdk::Context::get().font() != nullptr; }

void DrawUtils::fill(const Rect& area, const Colour& colour) {
    if (colour.a <= 0.0f)
        return;
    rawFill(area.left, area.top, area.right, area.bottom, colour);
}

void DrawUtils::outline(const Rect& area, const Colour& colour, float thickness) {
    if (colour.a <= 0.0f || thickness <= 0.0f)
        return;

    fill({area.left, area.top, area.right, area.top + thickness}, colour);
    fill({area.left, area.bottom - thickness, area.right, area.bottom}, colour);
    fill({area.left, area.top + thickness, area.left + thickness, area.bottom - thickness}, colour);
    fill({area.right - thickness, area.top + thickness, area.right, area.bottom - thickness}, colour);
}

void DrawUtils::gradientVertical(const Rect& area, const Colour& top, const Colour& bottom, int steps) {
    steps = std::max(steps, 1);
    const float stripHeight = area.height() / static_cast<float>(steps);
    for (int i = 0; i < steps; ++i) {
        const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(steps);
        const float y = area.top + stripHeight * static_cast<float>(i);
        fill({area.left, y, area.right, y + stripHeight + 0.5f}, top.lerp(bottom, t));
    }
}

void DrawUtils::gradientHorizontal(const Rect& area, const Colour& left, const Colour& right, int steps) {
    steps = std::max(steps, 1);
    const float stripWidth = area.width() / static_cast<float>(steps);
    for (int i = 0; i < steps; ++i) {
        const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(steps);
        const float x = area.left + stripWidth * static_cast<float>(i);
        fill({x, area.top, x + stripWidth + 0.5f, area.bottom}, left.lerp(right, t));
    }
}

void DrawUtils::roundedFill(const Rect& area, const Colour& colour, float radius, int segments) {
    radius = std::min({radius, area.width() * 0.5f, area.height() * 0.5f});
    if (radius <= 0.5f) {
        fill(area, colour);
        return;
    }

    // Middle block, then the two rounded caps as horizontal strips.
    fill({area.left, area.top + radius, area.right, area.bottom - radius}, colour);

    segments = std::max(segments, 2);
    const float step = radius / static_cast<float>(segments);
    for (int i = 0; i < segments; ++i) {
        const float offset = step * static_cast<float>(i);
        const float y = radius - offset;
        // Horizontal inset of the circle at this height.
        const float inset = radius - std::sqrt(std::max(0.0f, radius * radius - y * y));

        fill({area.left + inset, area.top + offset, area.right - inset, area.top + offset + step + 0.5f},
             colour);
        fill({area.left + inset, area.bottom - offset - step - 0.5f, area.right - inset, area.bottom - offset},
             colour);
    }
}

void DrawUtils::probe(ProbeSite site) {
    if (!g_probeEnabled)
        return;

    static const Colour colours[] = {Colour::rgb(0xFF3B30), Colour::rgb(0x34C759), Colour::rgb(0x0A84FF)};
    static const char* labels[] = {"probe A: before game render", "probe B: after game render",
                                   "probe C: after screen render"};

    const int index = static_cast<int>(site);
    const float y = 40.0f + static_cast<float>(index) * 26.0f;

    rawFill(40.0f, y, 360.0f, y + 20.0f, colours[index]);
    text(labels[index], {46.0f, y + 6.0f}, Colour::rgb(0x000000));
}

void DrawUtils::setProbeEnabled(bool enabled) { g_probeEnabled = enabled; }

bool DrawUtils::probeEnabled() { return g_probeEnabled; }

DrawUtils::Stats DrawUtils::stats() {
    return {g_fills.load(std::memory_order_relaxed), g_fillsSkipped.load(std::memory_order_relaxed),
            g_texts.load(std::memory_order_relaxed), g_textsSkipped.load(std::memory_order_relaxed)};
}

void DrawUtils::text(const std::string& value, const Vec2& position, const Colour& colour, float scale,
                     bool shadow) {
    auto* font = sdk::Context::get().font();
    if (!font || value.empty()) {
        if (!font) {
            g_textsSkipped.fetch_add(1, std::memory_order_relaxed);

            // A few frames with no client are expected right after injection,
            // before the tick hook has captured one. Only complain once the
            // client exists but the font still cannot be reached.
            auto& context = sdk::Context::get();
            if (context.client) {
                static bool logged = false;
                if (!logged) {
                    logged = true;
                    LOG_ERROR("Draw", "client {} has no Font (game={}) - text draws are a no-op",
                              static_cast<void*>(context.client),
                              static_cast<void*>(context.client->game()));
                }
            }
        }
        return;
    }
    g_texts.fetch_add(1, std::memory_order_relaxed);

    // Font::drawCached has no scale parameter in this build; scaling would need
    // Font::drawTransformed with a matrix, so `scale` only affects layout maths
    // until that path is mapped.
    (void)scale;
    font->draw(value, position.x, position.y, colour, shadow);
}

void DrawUtils::textCentred(const std::string& value, const Vec2& centre, const Colour& colour, float scale,
                            bool shadow) {
    text(value, {centre.x - textWidth(value, scale) * 0.5f, centre.y}, colour, scale, shadow);
}

void DrawUtils::textRight(const std::string& value, const Vec2& rightAnchor, const Colour& colour,
                          float scale, bool shadow) {
    text(value, {rightAnchor.x - textWidth(value, scale), rightAnchor.y}, colour, scale, shadow);
}

float DrawUtils::textWidth(const std::string& value, float scale) {
    auto* font = sdk::Context::get().font();
    if (!font || value.empty())
        return 0.0f;
    return font->width(value, scale);
}

float DrawUtils::textHeight(float scale) { return 10.0f * scale; }

} // namespace aerial::render
