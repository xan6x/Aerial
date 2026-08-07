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

constexpr float kMaxFrame = 1.0f / 15.0f;
constexpr float kMinFrame = 1.0f / 1000.0f;

float g_frameDelta = 1.0f / 60.0f;

}

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

    const float t = index <= 0 ? 0.0f : std::fmod(static_cast<float>(index) * 0.17f, 0.6f);
    return accent.lerp(accentAlt, t);
}

Colour Theme::rainbowAt(int index, float spread) const {
    const float hue = clockSeconds() * rainbowSpeed + static_cast<float>(index) * spread;
    return Colour::hsv(hue, 0.55f, 1.0f, accent.a);
}

}
