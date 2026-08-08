#include "Module/Modules/NoCamReset.h"

#include "Utils/Obfusc.h"

namespace aerial::modules {

NoCamReset::NoCamReset()
    : PatchModule("NoCamReset", "Keeps your view when the server moves you", Category::Input,
                  BytePatch::nops(AERIAL_STR("FF 90 ? ? ? ? ? ? ? 48 8B D6 44 8B 4C 24"), 6)) {}

}
