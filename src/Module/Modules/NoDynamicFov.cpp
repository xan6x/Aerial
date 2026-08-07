#include "Module/Modules/NoDynamicFov.h"

namespace aerial::modules {

// LevelRenderer::tick writes the computed field of view to [this+0x1278] every
// frame. Without that eight-byte store the value stays where it was, so
// sprinting and drawing a bow no longer zoom the camera.
//
// The FOV freezes at whatever it happened to be when the module was switched
// on; switching it off hands the field back to the game, which corrects it on
// the next tick.
NoDynamicFov::NoDynamicFov()
    : PatchModule("NoDynamicFov", "Stops the FOV changing while sprinting", Category::Render,
                  BytePatch::nops("F3 0F 11 83 ? ? ? ? 48 8B 83 ? ? ? ? 48 8B 48", 8)) {}

} // namespace aerial::modules
