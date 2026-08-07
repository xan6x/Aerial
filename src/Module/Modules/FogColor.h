#pragma once

#include "Module/Module.h"

namespace aerial {
struct Render2DEvent;
}

namespace aerial::modules {

class FogColor final : public Module {
public:
    FogColor();

protected:
    void onDisable() override;

private:
    void onRender(Render2DEvent& event);

    EnumSetting* m_preset;
    FloatSetting* m_red;
    FloatSetting* m_green;
    FloatSetting* m_blue;
    BoolSetting* m_rainbow;
    FloatSetting* m_rainbowSpeed;
};

}
