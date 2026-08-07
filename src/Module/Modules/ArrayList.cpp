#include "Module/Modules/ArrayList.h"

#include <algorithm>
#include <string>
#include <vector>

#include "Event/Events.h"
#include "GUI/Theme.h"
#include "Module/ModuleManager.h"
#include "Render/DrawUtils.h"

namespace aerial::modules {
namespace {

using render::DrawUtils;

constexpr float kTextSize = 13.0f;
constexpr float kRowHeight = 21.0f;
constexpr float kRowGap = 3.0f;
constexpr float kMargin = 16.0f;
constexpr float kPadding = 9.0f;
constexpr float kBarWidth = 2.5f;
constexpr auto kWeight = DrawUtils::Weight::Medium;

// Ease-out cubic. A row that decelerates into place reads as settling; linear
// motion reads as a jump cut.
float easeOut(float t) {
    const float inverted = 1.0f - std::clamp(t, 0.0f, 1.0f);
    return 1.0f - inverted * inverted * inverted;
}

// Just the name. Modules use suffix() to explain themselves - "no sky in
// pack", "unavailable" - and that belongs in the menu, where it is an answer to
// something the player just did. On the HUD it is a caption nobody asked for
// sitting over the game.
const std::string& labelFor(const Module& module) { return module.name(); }

} // namespace

ArrayList::ArrayList() : Module("ArrayList", "Lists enabled modules", Category::Interface) {
    m_sort = addEnum("Sort", "Row ordering", {"Length", "Alphabetical"}, 0);
    m_accent = addEnum("Accent", "Where the colour goes", {"Bar", "Text", "Both"}, 2);
    m_rainbow = addBool("Rainbow", "Cycle row colours", true);
    m_background = addBool("Background", "Draw a panel behind the rows", true);
    m_speed = addFloat("Animation", "How quickly rows settle", 0.22f, 0.05f, 1.0f, 0.01f);

    listen<Render2DEvent>(&ArrayList::onRender);
    setEnabled(true);
}

void ArrayList::onRender(Render2DEvent& event) {
    const auto& theme = gui::Theme::get();
    const float s = DrawUtils::uiScale();
    const float speed = m_speed->value;

    // ── Work out who should be on screen ─────────────────────────────────────
    for (auto& [module, row] : m_rows)
        row.alive = false;

    std::vector<Module*> active;
    for (Module* module : ModuleManager::get().activeModules()) {
        if (module->listed())
            active.push_back(module);
    }

    // Sorting works on modules rather than on strings so each row keeps its
    // identity - that is what lets it animate to a new position instead of a
    // different row's text appearing in its place.
    if (m_sort->is("Length")) {
        std::sort(active.begin(), active.end(), [&](const Module* a, const Module* b) {
            const float wa = DrawUtils::textWidth(labelFor(*a), kTextSize * s, kWeight);
            const float wb = DrawUtils::textWidth(labelFor(*b), kTextSize * s, kWeight);
            return wa != wb ? wa > wb : a->name() < b->name();
        });
    } else {
        std::sort(active.begin(), active.end(),
                  [](const Module* a, const Module* b) { return a->name() < b->name(); });
    }

    // ── Give every live row a target ─────────────────────────────────────────
    const float rowHeight = kRowHeight * s;
    const float step = rowHeight + kRowGap * s;
    float target = kMargin * s;
    int index = 0;

    for (Module* module : active) {
        Row& row = m_rows[module];
        row.label = labelFor(*module);
        row.index = index++;
        row.alive = true;
        row.slide.set(1.0f);

        if (!row.placed) {
            // First appearance: drop it straight at its target and let the
            // slide-in carry it, rather than having it fly down the screen.
            row.y = gui::Animated{target};
            row.placed = true;
        } else {
            row.y.set(target);
        }

        target += step;
    }

    // Rows whose module was switched off keep their last position and slide
    // out. They are already out of the layout above, so the rows below them
    // close the gap at the same time.
    for (auto& [module, row] : m_rows) {
        if (!row.alive)
            row.slide.set(0.0f);
    }

    // ── Draw ─────────────────────────────────────────────────────────────────
    const float right = event.screenSize.x - kMargin * s;

    for (auto& [module, row] : m_rows) {
        const float slide = easeOut(row.slide.update(speed));
        const float y = row.y.update(speed);

        if (slide <= 0.01f)
            continue;

        const float width = DrawUtils::textWidth(row.label, kTextSize * s, kWeight);
        const float rowWidth = width + kPadding * 2.0f * s;

        // Park off the right edge and travel in; the fade rides the same value
        // so a row never blinks out mid-flight.
        const float hidden = (1.0f - slide) * (rowWidth + kMargin * s);
        const Rect area{right - rowWidth + hidden, y, right + hidden, y + rowHeight};

        const Colour colour = m_rainbow->value ? theme.rainbowAt(row.index, 14.0f) : theme.accent;
        const bool barAccent = !m_accent->is("Text");
        const bool textAccent = !m_accent->is("Bar");

        if (m_background->value) {
            // A gradient rather than a flat fill: the panel fades out towards
            // the text's leading edge so short and long rows sit together
            // instead of looking like ragged blocks.
            DrawUtils::gradient({area.left, area.top, area.right, area.bottom},
                                Colour::rgb(0x0B0D12, 0.30f * slide),
                                Colour::rgb(0x0B0D12, 0.72f * slide), false, 5.0f * s);
        }

        DrawUtils::fill({area.right - kBarWidth * s, area.top + 4.0f * s, area.right,
                         area.bottom - 4.0f * s},
                        (barAccent ? colour : theme.text).withAlpha(slide), kBarWidth * 0.5f * s);

        DrawUtils::text(row.label,
                        {area.right - kBarWidth * s - 7.0f * s,
                         y + (rowHeight - DrawUtils::textHeight(kTextSize * s)) * 0.5f},
                        (textAccent ? colour : theme.textActive).withAlpha(slide), kTextSize * s,
                        kWeight, DrawUtils::Align::Right);
    }

    // Retire rows only once they are fully gone, so the slide-out is never cut
    // short by the module being forgotten.
    std::erase_if(m_rows, [](const auto& entry) {
        return !entry.second.alive && entry.second.slide.value() <= 0.001f;
    });
}

} // namespace aerial::modules
