#pragma once

#include <string>

namespace aerial::hooks {

void clearMovementInput();

void* containerController();

void requestFreshSelect();

void freshSelectNow();

int hoveredSlot(std::string& collection);

void replayDeferredGrab();

void holdMouseReleased();

void healMouseGrab();

}
