#include "Module/Modules/AutoSprint.h"

namespace aerial::modules {

// MoveInputHandler::tick+0x14 reads the sprint byte at [this+0x63] and stores it
// to [this+0x41]. Forcing the load to 1 makes the game believe the sprint input
// is always held.
//
// Only AL is consumed downstream - the value goes straight into a byte store -
// so a two-byte immediate load plus padding fits the seven bytes the original
// movzx and xor pair occupied. The xor is kept because the code after it reads
// bpl as a zero.
AutoSprint::AutoSprint()
    : PatchModule("AutoSprint", "Always sprints", Category::Movement,
                  BytePatch("0F B6 41 ? 40 32 ED",
                            {0xB0, 0x01,                 // mov al, 1
                             0x90, 0x90,                 // padding
                             0x40, 0x32, 0xED})) {}      // xor bpl, bpl (unchanged)

} // namespace aerial::modules
