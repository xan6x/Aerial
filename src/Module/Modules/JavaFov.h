#pragma once

#include "Module/Module.h"

namespace aerial {
struct Render2DEvent;
}

namespace aerial::modules {

class JavaFov final : public Module {
public:
    JavaFov();

protected:
    void onDisable() override;

private:
    void onRender(Render2DEvent& event);

    FloatSetting* m_sprintFov;
    FloatSetting* m_smoothing;

    float m_current = 1.0f;
};

}
