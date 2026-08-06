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
    m_colour = addColour("Colour", "Accent colour", Colour::rgb(0x5B8DEF));

    listen<Render2DEvent>(&Watermark::onRender);
    setEnabled(true);
}

void Watermark::onRender(Render2DEvent& event) {
    const auto& theme = gui::Theme::get();
    const Colour accent = theme.rainbow ? theme.accentFor(0) : m_colour->value;

    std::string label = "Aerial";
    std::string detail = " 1.1.5";
    if (m_showFps->value)
        detail += std::format(" | {} fps", static_cast<int>(framesPerSecond()));

    const float x = 4.0f;
    const float y = 4.0f;
    const float width = DrawUtils::textWidth(label + detail) + 8.0f;

    if (m_style->is("Modern")) {
        DrawUtils::fill({x, y, x + width, y + 13.0f}, theme.panel);
        DrawUtils::gradientHorizontal({x, y + 11.5f, x + width, y + 13.0f}, accent,
                                      theme.rainbow ? theme.accentFor(6) : theme.accentAlt);
    }

    DrawUtils::text(label, {x + 4.0f, y + 3.0f}, accent);
    DrawUtils::text(detail, {x + 4.0f + DrawUtils::textWidth(label), y + 3.0f}, theme.text);

    (void)event;
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
    auto& theme = gui::Theme::get();
    theme.rainbow = m_rainbow->value;

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

    if (m_sort->is("Length")) {
        std::sort(labels.begin(), labels.end(), [](const std::string& a, const std::string& b) {
            const float wa = DrawUtils::textWidth(a);
            const float wb = DrawUtils::textWidth(b);
            return wa != wb ? wa > wb : a < b;
        });
    } else {
        std::sort(labels.begin(), labels.end());
    }

    const float right = event.screenSize.x - 3.0f;
    float y = 3.0f;
    int index = 0;

    for (const std::string& label : labels) {
        const float width = DrawUtils::textWidth(label);
        const Colour colour = m_rainbow->value ? theme.accentFor(index, 14.0f) : theme.accent;

        if (m_background->value)
            DrawUtils::fill({right - width - 3.0f, y - 1.0f, right + 1.0f, y + 9.0f},
                            Colour::rgb(0x0B0D12, 0.55f));

        DrawUtils::fill({right + 1.0f, y - 1.0f, right + 2.5f, y + 9.0f}, colour);
        DrawUtils::text(label, {right - width, y}, colour);

        y += 10.0f;
        ++index;
    }
}

} // namespace aerial::modules
