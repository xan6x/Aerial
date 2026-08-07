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

// ── Float options ────────────────────────────────────────────────────────────
// Options::getFloat is shared by every float setting, so a multiplier has to be
// aimed at one option id. `optionId` of 0 disables the scaling entirely, which
// is the state until the sensitivity id has been identified.
void setOptionScale(uint32_t optionId, float multiplier);

// Logs each distinct option id once, and every later change to one. Moving a
// slider in the game's settings then names the id that slider writes.
void setOptionLogging(bool enabled);

// ── Item physics ─────────────────────────────────────────────────────────────
// Spins dropped items instead of leaving them as camera-facing sprites.
//   spin  - degrees per second
//   tilt  - degrees to lay the item away from upright
//   lift  - world-space height offset
void setItemPhysics(bool enabled, float spin, float tilt, float lift);

// ── Fog ──────────────────────────────────────────────────────────────────────
// Replaces the fog colour the game computed for this frame. Only the RGB
// components are taken; the fourth float setupFog writes is left alone.
void setFogColour(bool enabled, float red, float green, float blue);

// Creates every detour and enables them in a single MinHook transaction.
// Returns false if any critical hook could not be created; non-critical hooks
// are logged and skipped.
bool installAll();

void removeAll();

} // namespace aerial::hooks
