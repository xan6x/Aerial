#include "Module/Modules/NoHurtCam.h"

namespace aerial::modules {

// LevelRendererPlayer::bobHurt loads the hurt timer at [this+0xE6C] and shakes
// the camera by it. The nine-byte load goes.
//
// The cvtdq2ps that follows then reads whatever xmm8 held - stale rather than
// zero. That is what zutil ships and what is known to work on this build;
// zeroing the register instead would be tidier but changes what the shake is
// computed from, so it is not a swap worth making untested.
NoHurtCam::NoHurtCam()
    : PatchModule("NoHurtCam", "Stops the camera shaking when you take damage", Category::Visuals,
                  BytePatch::nops("66 44 0F 6E 83 ? ? ? ? 45 0F 5B C0 44 0F 29 4C 24", 9)) {}

} // namespace aerial::modules
