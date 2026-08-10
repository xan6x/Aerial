#include "Module/Modules/Keystrokes.h"

#include <string>

#include "Event/Events.h"
#include "GUI/HudStyle.h"
#include "Input/InputManager.h"
#include "Render/DrawUtils.h"

namespace aerial::modules {
namespace {

using render::DrawUtils;

constexpr int kW = 0x57, kA = 0x41, kS = 0x53, kD = 0x44;
constexpr int kSpace = 0x20, kLmb = 0x01, kRmb = 0x02;

}

Keystrokes::Keystrokes() : Module("Keystrokes", "Shows your movement keys", Category::Interface) {
    m_mouse = addBool("Mouse", "Show the mouse buttons", true);
    m_space = addBool("Space", "Show the spacebar", true);
    m_showCps = addBool("CPS", "Show clicks-per-second on the mouse keys", true);
    m_showCps->onlyIf([this] { return m_mouse->value; });
    m_background = addBool("Background", "Draw a panel behind the keys", true);
    m_rainbow = addBool("Rainbow", "Cycle the press colour", false);
    m_colour = addColour("Colour", "Key press colour", Colour::rgb(0x6C8CFF));
    m_colour->onlyIf([this] { return !m_rainbow->value; });
    m_rounding = addFloat("Rounding", "Corner radius", 7.0f, 0.0f, 14.0f, 0.5f);

    m_posX = addFloat("PosX", "", -1.0f, -1.0f, 1.0f, 0.0f);
    m_posY = addFloat("PosY", "", -1.0f, -1.0f, 1.0f, 0.0f);
    m_posX->onlyIf([] { return false; });
    m_posY->onlyIf([] { return false; });
    m_drag.bind(m_posX, m_posY);

    listen<Render2DEvent>(&Keystrokes::onRender);
    setEnabled(false);
}

void Keystrokes::onRender(Render2DEvent& event) {
    const auto& theme = gui::Theme::get();
    const float s = DrawUtils::uiScale();
    auto& im = input::InputManager::get();

    m_lmb.update(im.isDown(kLmb));
    m_rmb.update(im.isDown(kRmb));

    constexpr float speed = 0.4f;
    const float tw = (m_w.set(im.isDown(kW) ? 1.0f : 0.0f), m_w.update(speed));
    const float ta = (m_a.set(im.isDown(kA) ? 1.0f : 0.0f), m_a.update(speed));
    const float ts = (m_s.set(im.isDown(kS) ? 1.0f : 0.0f), m_s.update(speed));
    const float td = (m_d.set(im.isDown(kD) ? 1.0f : 0.0f), m_d.update(speed));
    const float tspace = (m_spaceA.set(im.isDown(kSpace) ? 1.0f : 0.0f), m_spaceA.update(speed));
    const float tl = (m_lmbA.set(im.isDown(kLmb) ? 1.0f : 0.0f), m_lmbA.update(speed));
    const float tr = (m_rmbA.set(im.isDown(kRmb) ? 1.0f : 0.0f), m_rmbA.update(speed));

    const float cell = 27.0f * s;
    const float gap = 4.0f * s;
    const float pad = 8.0f * s;
    const float gridW = cell * 3.0f + gap * 2.0f;
    const float spaceH = 13.0f * s;
    const float mouseH = 22.0f * s;

    float gridH = cell * 2.0f + gap;
    if (m_space->value)
        gridH += gap + spaceH;
    if (m_mouse->value)
        gridH += gap + mouseH;

    const Vec2 anchor = m_drag.place({gridW + pad * 2.0f, gridH + pad * 2.0f}, event.screenSize);
    const float ox = anchor.x + pad;
    const float oy = anchor.y + pad;

    const float radius = m_rounding->value * s;
    const Colour accent = m_rainbow->value ? theme.rainbowAt(0) : m_colour->value;

    if (m_background->value)
        hud::panel({anchor.x, anchor.y, anchor.x + gridW + pad * 2.0f, anchor.y + gridH + pad * 2.0f},
                   radius + 3.0f * s, s);

    const auto key = [&](const Rect& r, const std::string& label, float t, float size) {
        hud::cell(r, radius, t, accent, s);
        if (label.empty())
            return;
        const Colour tc = theme.text.lerp(theme.textActive, t);
        DrawUtils::text(label, {(r.left + r.right) * 0.5f,
                                (r.top + r.bottom) * 0.5f - DrawUtils::textHeight(size) * 0.5f},
                        tc, size, DrawUtils::Weight::SemiBold, DrawUtils::Align::Centre);
    };

    const float ks = 12.5f * s;
    key(Rect::xywh(ox + cell + gap, oy, cell, cell), "W", tw, ks);
    key(Rect::xywh(ox, oy + cell + gap, cell, cell), "A", ta, ks);
    key(Rect::xywh(ox + cell + gap, oy + cell + gap, cell, cell), "S", ts, ks);
    key(Rect::xywh(ox + (cell + gap) * 2.0f, oy + cell + gap, cell, cell), "D", td, ks);

    float rowY = oy + cell * 2.0f + gap;

    if (m_space->value) {
        rowY += gap;
        key(Rect::xywh(ox, rowY, gridW, spaceH), "", tspace, ks);
        rowY += spaceH;
    }

    if (m_mouse->value) {
        rowY += gap;
        const float halfW = (gridW - gap) * 0.5f;
        const std::string left = m_showCps->value ? std::to_string(m_lmb.cps()) : "LMB";
        const std::string right = m_showCps->value ? std::to_string(m_rmb.cps()) : "RMB";
        key(Rect::xywh(ox, rowY, halfW, mouseH), left, tl, 11.5f * s);
        key(Rect::xywh(ox + halfW + gap, rowY, halfW, mouseH), right, tr, 11.5f * s);
    }
}

}
