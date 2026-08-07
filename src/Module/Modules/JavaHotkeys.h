#pragma once

#include "Module/Module.h"

namespace aerial {
struct KeyEvent;
}

namespace aerial::modules {

class JavaHotkeys final : public Module {
public:
    JavaHotkeys();

private:
    void onKey(KeyEvent& event);
};

}
