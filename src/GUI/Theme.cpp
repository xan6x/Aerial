#include "GUI/Theme.h"

#include <chrono>
#include <cmath>

namespace aerial::gui {

Theme& Theme::get() {
    static Theme instance;
    return instance;
}

float clockSeconds() {
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    return duration<float>(steady_clock::now() - start).count();
}

Colour Theme::menuAccent(int index) const {
    // A gentle walk between the two brand colours keeps cards distinguishable
    // without turning the menu into a colour wheel.
    const float t = index <= 0 ? 0.0f : std::fmod(static_cast<float>(index) * 0.17f, 0.6f);
    return accent.lerp(accentAlt, t);
}

Colour Theme::rainbowAt(int index, float spread) const {
    const float hue = clockSeconds() * rainbowSpeed + static_cast<float>(index) * spread;
    return Colour::hsv(hue, 0.55f, 1.0f, accent.a);
}

} // namespace aerial::gui
