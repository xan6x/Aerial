#include "GUI/Theme.h"

#include <chrono>

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

Colour Theme::accentFor(int index, float spread) const {
    if (!rainbow)
        return accent.lerp(accentAlt, index > 0 ? 0.5f : 0.0f);

    const float hue = clockSeconds() * rainbowSpeed + static_cast<float>(index) * spread;
    return Colour::hsv(hue, 0.55f, 1.0f, accent.a);
}

} // namespace aerial::gui
