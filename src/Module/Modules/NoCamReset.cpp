#include "Module/Modules/NoCamReset.h"

namespace aerial::modules {

// A teleport calls the entity's setRot, which snaps the camera. The call is
// `call qword ptr [rax+0x88]`, and 0x88 / 8 is 17 - the setRot slot in the
// entity vtable, which is what identifies it.
//
// zutil nops two bytes here. That would cut a six-byte call in half and leave
// its tail decoding as a memory write through rax, so the whole instruction
// goes instead.
NoCamReset::NoCamReset()
    : PatchModule("NoCamReset", "Keeps your view when the server moves you", Category::Player,
                  BytePatch::nops("FF 90 ? ? ? ? ? ? ? 48 8B D6 44 8B 4C 24", 6)) {}

} // namespace aerial::modules
