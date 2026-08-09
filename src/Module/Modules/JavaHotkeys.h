#pragma once

#include <cstdint>
#include <string>

#include "Module/Module.h"

namespace aerial {
struct KeyEvent;
struct TickEvent;
}

namespace aerial::modules {

class JavaHotkeys final : public Module {
public:
    JavaHotkeys();

private:
    void onKey(KeyEvent& event);
    void onTick(TickEvent& event);

    struct Pending {
        bool active = false;
        int step = 0;
        int cooldown = 0;
        int hovered = 0;
        int target = 0;
        uint16_t button = 0;
        std::string collection;
    };

    Pending m_swap;
};

}
