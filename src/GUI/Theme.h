#pragma once

#include "Utils/Math.h"

namespace aerial::gui {

// Central palette. Everything the GUI and HUD draw pulls from here, so a new
// look is one struct away.
struct Theme {
    static Theme& get();

    // Tuned for a glass surface sitting over the game: the background is dark
    // and mostly transparent, and contrast comes from the text rather than from
    // heavy panel fills.
    Colour background = Colour::rgb(0x0D1015, 0.72f);
    Colour panel      = Colour::rgb(0x141821, 0.90f);
    Colour panelAlt   = Colour::rgb(0x1B202B, 0.92f);
    Colour header     = Colour::rgb(0x161A23, 0.92f);
    Colour outline    = Colour::rgb(0xFFFFFF, 0.09f);

    Colour accent     = Colour::rgb(0x6C8CFF);
    Colour accentAlt  = Colour::rgb(0xA779FF);

    Colour text       = Colour::rgb(0xD6DCE8);
    Colour textDim    = Colour::rgb(0x8A93A6);
    Colour textActive = Colour::rgb(0xFFFFFF);

    float cornerRadius = 10.0f;
    float animationSpeed = 0.20f;   // 0..1 lerp factor applied per frame

    float rainbowSpeed = 40.0f;     // degrees of hue per second

    // Hue-cycled colour for element `index`, advanced by the frame clock.
    // Callers decide whether they want it: a shared "rainbow is on" flag meant
    // the array list silently overrode the watermark's own colour setting.
    Colour rainbowAt(int index, float spread = 12.0f) const;

    // Stable accent for the menu. The rainbow belongs to the array list; letting
    // it drive the menu made the selection colour drift while you were reading
    // it, which is exactly the kind of motion a settings surface should not have.
    Colour menuAccent(int index = 0) const;
};

// Simple exponential-smoothing animator for GUI values.
class Animated {
public:
    Animated() = default;
    explicit Animated(float initial) : m_value(initial), m_target(initial) {}

    void set(float target) { m_target = target; }
    float target() const { return m_target; }

    float update(float speed) {
        m_value += (m_target - m_value) * speed;
        if (std::fabs(m_target - m_value) < 0.001f)
            m_value = m_target;
        return m_value;
    }

    float value() const { return m_value; }

private:
    float m_value = 0.0f;
    float m_target = 0.0f;
};

// Seconds since the client started; drives rainbow and animations.
float clockSeconds();

} // namespace aerial::gui
