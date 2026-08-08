#pragma once

#include <string>

#include "Module/Module.h"
#include "Utils/Patch.h"

namespace aerial {
struct Render2DEvent;
}

namespace aerial::modules {

class SelfNameTag final : public Module {
public:
    SelfNameTag();

    std::string suffix() const override;

protected:
    void onEnable() override;
    void onDisable() override;

private:
    void onRender(Render2DEvent& event);

    BytePatch m_patch;

    BoolSetting* m_frontView;
};

}
