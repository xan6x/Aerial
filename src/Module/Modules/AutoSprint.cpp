#include "Module/Modules/AutoSprint.h"

namespace aerial::modules {

AutoSprint::AutoSprint()
    : PatchModule("AutoSprint", "Always sprints", Category::Input,
                  BytePatch("0F B6 41 ? 40 32 ED",
                            {0xB0, 0x01,
                             0x90, 0x90,
                             0x40, 0x32, 0xED})) {}

}
