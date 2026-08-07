#pragma once

#include "Module/Module.h"

namespace aerial {
struct TickEvent;
}

namespace aerial::modules {

// Keeps walking working while a game screen - the inventory, for instance - is
// open.
class InventoryMove final : public Module {
public:
    InventoryMove();

private:
    void onTick(TickEvent& event);

    FloatSetting* m_speed;
};

} // namespace aerial::modules
