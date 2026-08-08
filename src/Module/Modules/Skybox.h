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

    BytePatch m_patch;

    enum class Found { Nothing, Cubemap, EndSky };
    Found m_found = Found::Nothing;
};

}
