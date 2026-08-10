#include "Module/Modules/CpsCounter.h"

#include <string>

#include "Event/Events.h"
#include "GUI/HudStyle.h"
#include "Input/InputManager.h"
#include "Render/DrawUtils.h"

namespace aerial::modules {
namespace {

using render::DrawUtils;

constexpr int kLmb = 0x01, kRmb = 0x02;

}

CpsCounter::CpsCounter() : Module("CPS Counter", "Shows your clicks per second", Category::Interface) {
    m_button = addEnum("Button", "Which button to count", {"Left", "Right", "Both"}, 0);
    m_background = addBool("Background", "Draw a panel behind the text", true);
    m_rainbow = addBool("Rainbow", "Cycle the number colour", false);
    m_colour = addColour("Colour", "Number colour", Colour::rgb(0x6C8CFF));
    m_colour->onlyIf([this] { return !m_rainbow->value; });
    m_rounding = addFloat("Rounding", "Corner radius", 8.0f, 0.0f, 14.0f, 0.5f);

    m_posX = addFloat("PosX", "", -1.0f, -1.0f, 1.0f, 0.0f);
    m_posY = addFloat("PosY", "", -1.0f, -1.0f, 1.0f, 0.0f);
    m_posX->onlyIf([] { return false; });
    m_posY->onlyIf([] { return false; });
    m_drag.bind(m_posX, m_posY);

    listen<Render2DEvent>(&CpsCounter::onRender);
    setEnabled(false);
}

void CpsCounter::onRender(Render2DEvent& event) {
    const auto& theme = gui::Theme::get();
    const float s = DrawUtils::uiScale();
    auto& im = input::InputManager::get();

    m_lmb.update(im.isDown(kLmb));
    m_rmb.update(im.isDown(kRmb));

    std::string number;
    if (m_button->is("Left"))
        number = std::to_string(m_lmb.cps());
    else if (m_button->is("Right"))
        number = std::to_string(m_rmb.cps());
    else
        number = std::to_string(m_lmb.cps()) + " | " + std::to_string(m_rmb.cps());

    const std::string suffix = "CPS";
    const float numSize = 15.0f * s;
    const float labelSize = 10.0f * s;
    const float innerGap = 5.0f * s;
    const float pad = 7.0f * s;

    const float numW = DrawUtils::textWidth(number, numSize, DrawUtils::Weight::Bold);
    const float labelW = DrawUtils::textWidth(suffix, labelSize, DrawUtils::Weight::Medium);
    const float contentW = numW + innerGap + labelW;
    const float contentH = DrawUtils::textHeight(numSize);

    const Vec2 anchor = m_drag.place({contentW + pad * 2.0f, contentH + pad * 2.0f}, event.screenSize);
    const float radius = m_rounding->value * s;
    const Colour accent = m_rainbow->value ? theme.rainbowAt(0) : m_colour->value;

    if (m_background->value)
        hud::panel({anchor.x, anchor.y, anchor.x + contentW + pad * 2.0f,
                    anchor.y + contentH + pad * 2.0f},
                   radius + 2.0f * s, s);

    const float x = anchor.x + pad;
    const float y = anchor.y + pad;
    DrawUtils::text(number, {x, y}, accent, numSize, DrawUtils::Weight::Bold, DrawUtils::Align::Left);
    DrawUtils::text(suffix, {x + numW + innerGap, y + (numSize - labelSize) * 0.5f + 1.0f * s},
                    theme.textDim, labelSize, DrawUtils::Weight::Medium, DrawUtils::Align::Left);
}

}
