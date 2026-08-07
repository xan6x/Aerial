#pragma once

#include <cstdint>

namespace aerial::hooks {

// Number of times the render hook has run. A watchdog logs the delta so it is
// obvious from the log whether the hook is alive at all - the difference
// between "input is broken" and "the hook never fires".
uint64_t renderFrameCount();
uint64_t playerTickCount();

// MinecraftGame::update runs in every game state, so a non-zero delta here with
// zero frames means the client is alive but not in a world.
uint64_t gameUpdateCount();

// Times the 2D overlay actually reached the game's UI pass.
uint64_t overlayCount();

// MoveInputHandler captured from its own hook, so modules can read the movement
// amounts without another pointer chain. Null before the first input tick.
void* moveInputHandler();

// Creates every detour and enables them in a single MinHook transaction.
// Returns false if any critical hook could not be created; non-critical hooks
// are logged and skipped.
bool installAll();

void removeAll();

} // namespace aerial::hooks
