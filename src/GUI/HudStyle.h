#pragma once

#include "GUI/Theme.h"
#include "Render/DrawUtils.h"
#include "Utils/Math.h"

namespace aerial::hud {

inline void panel(const Rect& r, float radius, float s) {
    using render::DrawUtils;
    DrawUtils::shadow(r, Colour::rgb(0x000000, 0.55f), 22.0f * s, radius, {0.0f, 4.0f * s});
    DrawUtils::blurBehind(r, radius, 18.0f);
    DrawUtils::fill(r, gui::Theme::get().background, radius);
    DrawUtils::outline(r, Colour::rgb(0xFFFFFF, 0.09f), 1.0f * s, radius);
}

inline void cell(const Rect& r, float radius, float t, const Colour& accent, float s) {
    using render::DrawUtils;

    if (t > 0.02f)
        DrawUtils::fill(r.inset(-2.5f * s), accent.withAlpha(0.22f * t), radius + 2.5f * s);

    DrawUtils::fill(r, Colour::rgb(0x2A303C, 0.5f).lerp(accent, t), radius);
    DrawUtils::outline(r, Colour::rgb(0xFFFFFF, 0.06f + 0.16f * t), 1.0f * s, radius);
}

}
