#include "Module/Modules/Watermark.h"

#include <chrono>
#include <format>

#include "Event/Events.h"
#include "GUI/Theme.h"
#include "Render/DrawUtils.h"

namespace aerial::modules {
namespace {

using render::DrawUtils;

float framesPerSecond() {
    using namespace std::chrono;
    static auto previous = steady_clock::now();
    static float smoothed = 60.0f;

    const auto now = steady_clock::now();
    const float delta = duration<float>(now - previous).count();
    previous = now;

    if (delta > 0.0f && delta < 1.0f)
        smoothed += (1.0f / delta - smoothed) * 0.08f;
    return smoothed;
}

}

Watermark::Watermark() : Module("Watermark", "Client logo in the corner", Category::Interface) {
    m_style = addEnum("Style", "Watermark appearance", {"Modern", "Simple"}, 0);
    m_showFps = addBool("FPS", "Append the frame rate", true);
    m_rainbow = addBool("Rainbow", "Cycle the accent colour", false);
    m_colour = addColour("Colour", "Accent colour", Colour::rgb(0x6C8CFF));
    m_colour->onlyIf([this] { return !m_rainbow->value; });

    listen<Render2DEvent>(&Watermark::onRender);
    setEnabled(true);
}

void Watermark::onRender(Render2DEvent& event) {
    (void)event;

    const auto& theme = gui::Theme::get();
    const float s = DrawUtils::uiScale();
    const Colour accent = m_rainbow->value ? theme.rainbowAt(0) : m_colour->value;

    const std::string label = "Aerial";
    std::string detail = "1.1.5";
    if (m_showFps->value)
        detail += std::format("  {} fps", static_cast<int>(framesPerSecond()));

    constexpr float kSize = 15.0f;
    const float labelWidth = DrawUtils::textWidth(label, kSize * s, DrawUtils::Weight::SemiBold);
    const float detailWidth = DrawUtils::textWidth(detail, 12.0f * s, DrawUtils::Weight::Medium);

    const float padding = 11.0f * s;
    const float height = 28.0f * s;
    const Rect box{10.0f * s, 10.0f * s,
                   10.0f * s + padding * 2.0f + labelWidth + detailWidth + 9.0f * s,
                   10.0f * s + height};

    if (m_style->is("Modern")) {
        DrawUtils::shadow(box, Colour::rgb(0x000000, 0.45f), 10.0f * s, 8.0f * s, {0.0f, 3.0f * s});
        DrawUtils::blurBehind(box, 8.0f * s, 12.0f);
        DrawUtils::fill(box, theme.panel, 8.0f * s);
        DrawUtils::outline(box, theme.outline, 1.0f * s, 8.0f * s);
    }

    const float baseline = box.top + (height - DrawUtils::textHeight(kSize * s)) * 0.5f;
    DrawUtils::text(label, {box.left + padding, baseline}, accent, kSize * s,
                    DrawUtils::Weight::SemiBold);
    DrawUtils::text(detail, {box.left + padding + labelWidth + 9.0f * s, baseline + 1.5f * s},
                    theme.textDim, 12.0f * s, DrawUtils::Weight::Medium);
}

}
