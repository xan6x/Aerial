#pragma once

#include "Utils/Math.h"

namespace aerial::gui {

// Central palette. Everything the GUI and HUD draw pulls from here, so a new
// look is one struct away.
struct Theme {
    static Theme& get();

    Colour background = Colour::rgb(0x14161C, 0.92f);
    Colour panel      = Colour::rgb(0x1B1E26, 0.95f);
    Colour panelAlt   = Colour::rgb(0x232733, 0.95f);
    Colour header     = Colour::rgb(0x2A2F3D, 0.98f);
    Colour outline    = Colour::rgb(0x000000, 0.45f);

    Colour accent     = Colour::rgb(0x5B8DEF);
    Colour accentAlt  = Colour::rgb(0x9B6BEF);

    Colour text       = Colour::rgb(0xE7EAF0);
    Colour textDim    = Colour::rgb(0x9AA3B4);
    Colour textActive = Colour::rgb(0xFFFFFF);

    float cornerRadius = 3.0f;
    float animationSpeed = 0.22f;   // 0..1 lerp factor applied per frame

    // Rainbow sweep used by the array list and accents when enabled.
    bool rainbow = false;
    float rainbowSpeed = 40.0f;     // degrees of hue per second

    // Hue offset for element `index`, advanced by the frame clock.
    Colour accentFor(int index, float spread = 12.0f) const;
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
