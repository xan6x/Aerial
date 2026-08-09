#pragma once

#include "Module/Module.h"

namespace aerial {
struct Render2DEvent;
}

namespace aerial::modules {

class FastRefill final : public Module {
public:
    FastRefill();

protected:
    void onEnable() override;
    void onDisable() override;

private:
    void onRender(Render2DEvent& event);

    BoolSetting* m_requireShift;
};

}
