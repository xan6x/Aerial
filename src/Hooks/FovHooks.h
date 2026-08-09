#pragma once

namespace aerial::hooks {

enum class Fov {
    Sprint,
    Zoom,
    Count,
};

void setFovScale(Fov source, float multiplier);

void setItemFov(float fov);

float currentFov();

float currentPartialTicks();

}
