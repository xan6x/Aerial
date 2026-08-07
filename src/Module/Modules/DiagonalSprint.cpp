#include "Module/Modules/DiagonalSprint.h"

namespace aerial::modules {

DiagonalSprint::DiagonalSprint()
    : PatchModule("DiagonalSprint", "Sprint starts while strafing, not only straight forward",
                  Category::Input,
                  BytePatch("F2 41 0F 10 4E 08 0F 28 C1 0F C6 C0 55 0F 28 F8",
                            {0xF2, 0x41, 0x0F, 0x10, 0x4E, 0x08,
                             0xF3, 0x0F, 0x16, 0xC1,
                             0x0F, 0x58, 0xC0,
                             0x0F, 0x28, 0xF8})) {}

} // namespace aerial::modules
