#include "GUI/Theme.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace aerial::gui {

Theme& Theme::get() {
    static Theme instance;
    return instance;
}

namespace {

// Two frames' worth at 30 fps. Past that the client was almost certainly not
// drawing at all - a world load, a resolution change, an alt-tab - and letting
// the real gap through would teleport every animation to its target.
constexpr float kMaxFrame = 1.0f / 15.0f;
constexpr float kMinFrame = 1.0f / 1000.0f;

float g_frameDelta = 1.0f / 60.0f;

} // namespace

float clockSeconds() {
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    return duration<float>(steady_clock::now() - start).count();
}

void advanceFrame() {
    static float last = clockSeconds();
    const float now = clockSeconds();
    g_frameDelta = std::clamp(now - last, kMinFrame, kMaxFrame);
    last = now;
}

float frameDelta() { return g_frameDelta; }

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
