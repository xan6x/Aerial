#pragma once

#include <string>

#include "Module/Module.h"

namespace aerial {
struct Render2DEvent;
}

namespace aerial::modules {

class Zoom final : public Module {
public:
    Zoom();

    std::string suffix() const override;
    bool holdBind() const override;

protected:
    void onDisable() override;

private:
    void onRender(Render2DEvent& event);

    FloatSetting* m_amount;
    BoolSetting* m_hold;
    BoolSetting* m_smooth;
    FloatSetting* m_speed;

    float m_current = 1.0f;
};

}
