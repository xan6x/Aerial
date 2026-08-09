#pragma once

#include "Module/Module.h"

namespace aerial {
struct Render2DEvent;
}

namespace aerial::modules {

class HurtColor final : public Module {
public:
    HurtColor();

protected:
    void onDisable() override;

private:
    void onRender(Render2DEvent& event);

    ColourSetting* m_colour;
};

}
