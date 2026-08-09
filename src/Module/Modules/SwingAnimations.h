#pragma once

#include "Module/Module.h"
#include "Utils/Patch.h"

namespace aerial {
struct Render2DEvent;
}

namespace aerial::modules {

class SwingAnimations final : public Module {
public:
    SwingAnimations();

protected:
    void onDisable() override;

private:
    void onRender(Render2DEvent& event);

    FloatSetting* m_speed;
    BoolSetting* m_flux;
    FloatSetting* m_angle;
    BytePatch m_fluxPatch;
};

}
