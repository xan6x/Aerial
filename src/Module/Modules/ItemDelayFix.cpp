#include "Module/Modules/ItemDelayFix.h"

namespace aerial::modules {

// ClientInputCallbacks::handleBuildAction stores a timestamp that gates the next
// use. Dropping the store - a single seven-byte `mov [rsi+disp32], rax` -
// removes the 200 ms wait after an attack.
ItemDelayFix::ItemDelayFix()
    : PatchModule("ItemDelayFix", "Removes the 200 ms delay after attacking", Category::Combat,
                  BytePatch::nops("48 89 86 ? ? ? ? 48 83 7E ? 00", 7)) {}

} // namespace aerial::modules
