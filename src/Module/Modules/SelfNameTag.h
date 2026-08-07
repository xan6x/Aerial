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

    BytePatch m_patch = BytePatch::nops("48 3B DF 0F 84 4E 01 00 00", 9);

    BoolSetting* m_frontView;
};

}
