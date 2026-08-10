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

float easeOut(float t) {
    const float inverted = 1.0f - std::clamp(t, 0.0f, 1.0f);
    return 1.0f - inverted * inverted * inverted;
}

const std::string& labelFor(const Module& module) { return module.name(); }

}

ArrayList::ArrayList() : Module("ArrayList", "Lists enabled modules", Category::Interface) {
    m_sort = addEnum("Sort", "Row ordering", {"Length", "Alphabetical"}, 0);
    m_accent = addEnum("Accent", "Where the colour goes", {"Bar", "Text", "Both"}, 2);
    m_rainbow = addBool("Rainbow", "Cycle row colours", true);
    m_background = addBool("Background", "Draw a panel behind the rows", true);
    m_speed = addFloat("Animation", "How quickly rows settle", 0.22f, 0.05f, 1.0f, 0.01f);

    m_posX = addFloat("PosX", "", -1.0f, -1.0f, 1.0f, 0.0f);
    m_posY = addFloat("PosY", "", -1.0f, -1.0f, 1.0f, 0.0f);
    m_posX->onlyIf([] { return false; });
    m_posY->onlyIf([] { return false; });
    m_drag.bind(m_posX, m_posY);

    listen<Render2DEvent>(&ArrayList::onRender);
    setEnabled(true);
}

void ArrayList::onRender(Render2DEvent& event) {
    const auto& theme = gui::Theme::get();
    const float s = DrawUtils::uiScale();
    const float speed = m_speed->value;

    for (auto& [module, row] : m_rows)
        row.alive = false;

    std::vector<Module*> active;
    for (Module* module : ModuleManager::get().activeModules()) {
        if (module->listed())
            active.push_back(module);
    }

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

    const float rowHeight = kRowHeight * s;
    const float step = rowHeight + kRowGap * s;
    float target = 0.0f;
    int index = 0;

    for (Module* module : active) {
        Row& row = m_rows[module];
        row.label = labelFor(*module);
        row.index = index++;
        row.alive = true;
        row.slide.set(1.0f);

        if (!row.placed) {

            row.y = gui::Animated{target};
            row.placed = true;
        } else {
            row.y.set(target);
        }

        target += step;
    }

    for (auto& [module, row] : m_rows) {
        if (!row.alive)
            row.slide.set(0.0f);
    }

    float maxWidth = 0.0f;
    for (auto& [module, row] : m_rows) {
        if (!row.alive && row.slide.value() <= 0.01f)
            continue;
        const float w =
            DrawUtils::textWidth(row.label, kTextSize * s, kWeight) + kPadding * 2.0f * s;
        maxWidth = std::max(maxWidth, w);
    }
    const float blockHeight = std::max(target, rowHeight);

    if (maxWidth <= 0.0f) {
        std::erase_if(m_rows, [](const auto& entry) {
            return !entry.second.alive && entry.second.slide.value() <= 0.001f;
        });
        return;
    }

    const Vec2 anchor = m_drag.place({maxWidth, blockHeight}, event.screenSize);
    const float left = anchor.x;
    const float right = anchor.x + maxWidth;
    const bool rightSide = (anchor.x + maxWidth * 0.5f) >= event.screenSize.x * 0.5f;
    const bool bottomSide = (anchor.y + blockHeight * 0.5f) >= event.screenSize.y * 0.5f;

    for (auto& [module, row] : m_rows) {
        const float slide = easeOut(row.slide.update(speed));
        const float yRel = row.y.update(speed);
        const float y = bottomSide ? anchor.y + blockHeight - rowHeight - yRel : anchor.y + yRel;

        if (slide <= 0.01f)
            continue;

        const float width = DrawUtils::textWidth(row.label, kTextSize * s, kWeight);
        const float rowWidth = width + kPadding * 2.0f * s;

        const float hidden = (1.0f - slide) * (rowWidth + kMargin * s);
        const Rect area = rightSide
                              ? Rect{right - rowWidth + hidden, y, right + hidden, y + rowHeight}
                              : Rect{left - hidden, y, left + rowWidth - hidden, y + rowHeight};

        const Colour colour = m_rainbow->value ? theme.rainbowAt(row.index, 14.0f) : theme.accent;
        const bool barAccent = !m_accent->is("Text");
        const bool textAccent = !m_accent->is("Bar");

        if (m_background->value) {

            DrawUtils::gradient({area.left, area.top, area.right, area.bottom},
                                Colour::rgb(0x0B0D12, 0.30f * slide),
                                Colour::rgb(0x0B0D12, 0.72f * slide), false, 5.0f * s);
        }

        const Rect bar = rightSide
                             ? Rect{area.right - kBarWidth * s, area.top + 4.0f * s, area.right,
                                    area.bottom - 4.0f * s}
                             : Rect{area.left, area.top + 4.0f * s, area.left + kBarWidth * s,
                                    area.bottom - 4.0f * s};
        DrawUtils::fill(bar, (barAccent ? colour : theme.text).withAlpha(slide), kBarWidth * 0.5f * s);

        const float textY = y + (rowHeight - DrawUtils::textHeight(kTextSize * s)) * 0.5f;
        const Colour textColour = (textAccent ? colour : theme.textActive).withAlpha(slide);
        if (rightSide)
            DrawUtils::text(row.label, {area.right - kBarWidth * s - 7.0f * s, textY}, textColour,
                            kTextSize * s, kWeight, DrawUtils::Align::Right);
        else
            DrawUtils::text(row.label, {area.left + kBarWidth * s + 7.0f * s, textY}, textColour,
                            kTextSize * s, kWeight, DrawUtils::Align::Left);
    }

    std::erase_if(m_rows, [](const auto& entry) {
        return !entry.second.alive && entry.second.slide.value() <= 0.001f;
    });
}

}
