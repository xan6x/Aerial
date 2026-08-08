#pragma once

#include <string>

#include "Module/Module.h"

namespace aerial {
struct Render2DEvent;
}

namespace aerial::modules {

class Aliases final : public Module {
public:
    Aliases();

protected:
    void onEnable() override;
    void onDisable() override;

private:
    void onRender(Render2DEvent& event);

    ListSetting* m_rules;
    BoolSetting* m_layout;
};

}
