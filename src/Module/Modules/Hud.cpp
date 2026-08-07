#include "Module/Modules/Modules.h"

#include <algorithm>
#include <chrono>
#include <format>

#include "Event/Events.h"
#include "GUI/Theme.h"
#include "Module/ModuleManager.h"
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

} // namespace

// ── Watermark ────────────────────────────────────────────────────────────────

Watermark::Watermark() : Module("Watermark", "Client logo in the corner", Category::Render) {
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

// ── ArrayList ────────────────────────────────────────────────────────────────

ArrayList::ArrayList() : Module("ArrayList", "Lists enabled modules", Category::Render) {
    m_sort = addEnum("Sort", "Row ordering", {"Length", "Alphabetical"}, 0);
    m_rainbow = addBool("Rainbow", "Cycle row colours", true);
    m_background = addBool("Background", "Draw a panel behind the rows", true);

    listen<Render2DEvent>(&ArrayList::onRender);
    setEnabled(true);
}

void ArrayList::onRender(Render2DEvent& event) {
    const auto& theme = gui::Theme::get();

    auto active = ModuleManager::get().activeModules();
    active.erase(std::remove_if(active.begin(), active.end(),
                                [](const Module* module) { return module->category() == Category::Render; }),
                 active.end());
    if (active.empty())
        return;

    std::vector<std::string> labels;
    labels.reserve(active.size());
    for (const Module* module : active) {
        const std::string tag = module->suffix();
        labels.push_back(tag.empty() ? module->name() : module->name() + " " + tag);
    }

    const float s = DrawUtils::uiScale();
    constexpr float kSize = 13.0f;
    const auto weight = DrawUtils::Weight::Medium;

    if (m_sort->is("Length")) {
        std::sort(labels.begin(), labels.end(), [&](const std::string& a, const std::string& b) {
            const float wa = DrawUtils::textWidth(a, kSize * s, weight);
            const float wb = DrawUtils::textWidth(b, kSize * s, weight);
            return wa != wb ? wa > wb : a < b;
        });
    } else {
        std::sort(labels.begin(), labels.end());
    }

    // Keep clear of the screen edge: at 10 units the accent bar sat flush
    // against it and read as clipped.
    const float margin = 16.0f * s;
    const float barWidth = 2.5f * s;
    const float right = event.screenSize.x - margin;
    const float rowHeight = 21.0f * s;
    float y = margin;
    int index = 0;

    for (const std::string& label : labels) {
        const float width = DrawUtils::textWidth(label, kSize * s, weight);
        const Colour colour = m_rainbow->value ? theme.rainbowAt(index, 14.0f) : theme.accent;

        const Rect row{right - width - 18.0f * s, y, right, y + rowHeight};

        if (m_background->value)
            DrawUtils::fill(row, Colour::rgb(0x0B0D12, 0.6f), 5.0f * s);

        DrawUtils::fill({row.right - barWidth, row.top + 4.0f * s, row.right, row.bottom - 4.0f * s},
                        colour, barWidth * 0.5f);

        DrawUtils::text(label,
                        {row.right - barWidth - 7.0f * s,
                         y + (rowHeight - DrawUtils::textHeight(kSize * s)) * 0.5f},
                        colour, kSize * s, weight, DrawUtils::Align::Right);

        y += rowHeight + 4.0f * s;
        ++index;
    }
}

} // namespace aerial::modules
