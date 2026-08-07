#pragma once

#include "Module/Module.h"

namespace aerial {
struct KeyEvent;
}

namespace aerial::modules {

class QuickSlots final : public Module {
public:
    QuickSlots();

private:
    void onKey(KeyEvent& event);
};

}
