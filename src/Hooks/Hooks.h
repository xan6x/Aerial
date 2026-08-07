#pragma once

#include <cstdint>

namespace aerial::hooks {

uint64_t renderFrameCount();
uint64_t playerTickCount();

uint64_t gameUpdateCount();

uint64_t overlayCount();

void* moveInputHandler();

void setOptionScale(uint32_t optionId, float multiplier);

void setGammaOverride(bool enabled, float gamma);

void setFovScale(bool enabled, float scale);

void setItemPhysics(bool enabled, float spin, float lift, float pivot, int thickness, bool smooth,
                    bool preserve, bool flat, bool noShadow);

void setSkybox(bool enabled);

void setSkyCubemap(bool enabled);

void setFogColour(bool enabled, float red, float green, float blue);

bool installAll();

bool requestTeardown(unsigned timeoutMs);

void removeAll();

}
