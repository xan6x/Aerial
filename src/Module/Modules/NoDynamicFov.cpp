#include "Module/Modules/NoDynamicFov.h"

namespace aerial::modules {

NoDynamicFov::NoDynamicFov()
    : PatchModule("NoDynamicFov", "Stops the FOV changing while sprinting", Category::Visuals,
                  BytePatch::nops("F3 0F 11 83 ? ? ? ? 48 8B 83 ? ? ? ? 48 8B 48", 8)) {}

}
