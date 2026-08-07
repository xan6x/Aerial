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

// ── Brightness ───────────────────────────────────────────────────────────────
// Replaces the gamma the game reads from its options.
void setGammaOverride(bool enabled, float gamma);

// ── Field of view ────────────────────────────────────────────────────────────
// Scales the computed field of view. The smoothing that makes it feel like
// Java's belongs to the module - the hook only multiplies.
void setFovScale(bool enabled, float scale);

// ── Item physics ─────────────────────────────────────────────────────────────
// Dropped items spin while they fall and settle when they land, instead of
// staying camera-facing sprites. Which item is which comes from hooking
// ItemRenderer::render for the actor pointer; the orientation itself is applied
// in Matrix::translate.
//   spin      - degrees per second while airborne
//   lift      - world-space height offset, non-block items only
//   pivot     - shift along the item's own axis after rotation, 0 for none
//   thickness - how many times to redraw the item, each a hair higher than the
//               last, so a flat sprite reads as a slab instead of a decal
//   smooth    - ease into the resting angle rather than snapping to it
//   preserve  - keep whatever angle the item landed at
//   flat      - never animate; draw every item resting, from the moment it drops
//   noShadow  - drop the blob shadow, which an item lying on the ground sits in
void setItemPhysics(bool enabled, float spin, float lift, float pivot, int thickness, bool smooth,
                    bool preserve, bool flat, bool noShadow);

// ── Skybox ───────────────────────────────────────────────────────────────────
// Companion to the byte patch that makes every dimension take the End's
// textured-cube sky path. The patch alone leaves the cube tinted by the biome
// fog colour and drops the sun, moon and stars, because the End branch returns
// before drawing them; this puts both back.
void setSkybox(bool enabled);

// Draws a pack's textures/environment/overworld_cubemap over the sky, as six
// separate faces. Independent of setSkybox: the cube the branch patch reveals
// carries one texture on all six sides, so a pack that ships a real cubemap
// needs its own geometry rather than a different picture on that cube.
void setSkyCubemap(bool enabled);

// ── Fog ──────────────────────────────────────────────────────────────────────
// Replaces the fog colour the game computed for this frame. Only the RGB
// components are taken; the fourth float setupFog writes is left alone.
void setFogColour(bool enabled, float red, float green, float blue);

// Creates every detour and enables them in a single MinHook transaction.
// Returns false if any critical hook could not be created; non-critical hooks
// are logged and skipped.
bool installAll();

// ── Teardown ─────────────────────────────────────────────────────────────────
// Closing the menu hands the cursor back through ClientInstance::grabMouse, and
// that has to happen on the thread that owns the game's state, not on the one
// that noticed the eject key. This asks MinecraftGame::update to do it and
// blocks for up to `timeoutMs`; false means the game never came round, in which
// case the caller should carry on regardless rather than hang.
bool requestTeardown(unsigned timeoutMs);

void removeAll();

} // namespace aerial::hooks
