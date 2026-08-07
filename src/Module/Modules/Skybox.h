#pragma once

#include <string>

#include "Module/Module.h"
#include "Utils/Patch.h"

namespace aerial::modules {

class Skybox final : public Module {
public:
    Skybox();

    std::string suffix() const override;

protected:
    void onEnable() override;
    void onDisable() override;

private:

    BytePatch m_patch{"0F 85 ? ? ? ? 48 8D 54 24 30 E8 ? ? ? ? 90 48 8B 5C 24 30",
                      {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}};

    enum class Found { Nothing, Cubemap, EndSky };
    Found m_found = Found::Nothing;
};

}
