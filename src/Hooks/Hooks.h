#pragma once

#include <cstdint>

namespace aerial::hooks {

uint64_t renderFrameCount();
uint64_t playerTickCount();

uint64_t gameUpdateCount();

uint64_t overlayCount();

bool installAll();

bool requestTeardown(unsigned timeoutMs);

void removeAll();

}
