#include "Module/Modules/Mousestrokes.h"

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

Mousestrokes::Mousestrokes()
    : Module("Mousestrokes", "Shows your mouse buttons and CPS", Category::Interface) {
    m_showCps = addBool("CPS", "Show clicks-per-second", true);
    m_labels = addBool("Labels", "Show the L / R labels", true);
    m_background = addBool("Background", "Draw a panel behind the buttons", true);
    m_rainbow = addBool("Rainbow", "Cycle the press colour", false);
    m_colour = addColour("Colour", "Press colour", Colour::rgb(0x6C8CFF));
    m_colour->onlyIf([this] { return !m_rainbow->value; });
    m_rounding = addFloat("Rounding", "Corner radius", 9.0f, 0.0f, 16.0f, 0.5f);

    m_posX = addFloat("PosX", "", -1.0f, -1.0f, 1.0f, 0.0f);
    m_posY = addFloat("PosY", "", -1.0f, -1.0f, 1.0f, 0.0f);
    m_posX->onlyIf([] { return false; });
    m_posY->onlyIf([] { return false; });
    m_drag.bind(m_posX, m_posY);

    listen<Render2DEvent>(&Mousestrokes::onRender);
    setEnabled(false);
}

void Mousestrokes::onRender(Render2DEvent& event) {
    const auto& theme = gui::Theme::get();
    const float s = DrawUtils::uiScale();
    auto& im = input::InputManager::get();

    m_lmb.update(im.isDown(kLmb));
    m_rmb.update(im.isDown(kRmb));

    constexpr float speed = 0.4f;
    const float tl = (m_lmbA.set(im.isDown(kLmb) ? 1.0f : 0.0f), m_lmbA.update(speed));
    const float tr = (m_rmbA.set(im.isDown(kRmb) ? 1.0f : 0.0f), m_rmbA.update(speed));

    const float btnW = 44.0f * s;
    const float btnH = 48.0f * s;
    const float gap = 5.0f * s;
    const float pad = 8.0f * s;
    const float blockW = btnW * 2.0f + gap;
    const float blockH = btnH;

    const Vec2 anchor = m_drag.place({blockW + pad * 2.0f, blockH + pad * 2.0f}, event.screenSize);
    const float ox = anchor.x + pad;
    const float oy = anchor.y + pad;

    const float radius = m_rounding->value * s;
    const Colour accent = m_rainbow->value ? theme.rainbowAt(0) : m_colour->value;

    if (m_background->value)
        hud::panel({anchor.x, anchor.y, anchor.x + blockW + pad * 2.0f, anchor.y + blockH + pad * 2.0f},
                   radius + 3.0f * s, s);

    const auto button = [&](const Rect& r, const char* label, int cps, float t) {
        hud::cell(r, radius, t, accent, s);
        const Colour tc = theme.text.lerp(theme.textActive, t);
        const float cx = (r.left + r.right) * 0.5f;

        if (m_showCps->value) {
            const std::string number = std::to_string(cps);
            const float numSize = 17.0f * s;
            const float labelSize = 9.0f * s;
            const float blockTextH =
                DrawUtils::textHeight(numSize) + (m_labels->value ? labelSize + 2.0f * s : 0.0f);
            float ty = (r.top + r.bottom) * 0.5f - blockTextH * 0.5f;
            if (m_labels->value) {
                DrawUtils::text(label, {cx, ty}, tc.withAlpha(tc.a * 0.75f), labelSize,
                                DrawUtils::Weight::Medium, DrawUtils::Align::Centre);
                ty += labelSize + 2.0f * s;
            }
            DrawUtils::text(number, {cx, ty}, tc, numSize, DrawUtils::Weight::Bold,
                            DrawUtils::Align::Centre);
        } else {
            const float labelSize = 13.0f * s;
            DrawUtils::text(label,
                            {cx, (r.top + r.bottom) * 0.5f - DrawUtils::textHeight(labelSize) * 0.5f},
                            tc, labelSize, DrawUtils::Weight::SemiBold, DrawUtils::Align::Centre);
        }
    };

    button(Rect::xywh(ox, oy, btnW, btnH), "LMB", m_lmb.cps(), tl);
    button(Rect::xywh(ox + btnW + gap, oy, btnW, btnH), "RMB", m_rmb.cps(), tr);
}

}
