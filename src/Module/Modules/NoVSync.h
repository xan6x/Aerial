#pragma once

#include "Module/Module.h"

namespace aerial {
struct Render2DEvent;
}

namespace aerial::modules {

class NoVSync final : public Module {
public:
    NoVSync();

protected:
    void onDisable() override;

private:
    void onRender(Render2DEvent& event);

    bool m_warned = false;
    bool m_hadVsync = true;
};

}
