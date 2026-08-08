#pragma once

#include "Module/Module.h"

namespace aerial {
struct Render2DEvent;
}

namespace aerial::modules {

class SensMultiplier final : public Module {
public:
    SensMultiplier();

protected:
    void onEnable() override;
    void onDisable() override;

private:
    void onRender(Render2DEvent& event);

    FloatSetting* m_multiplier;
    BoolSetting* m_mouseKeyboard;
    BoolSetting* m_controller;
    BoolSetting* m_touch;
};

}
