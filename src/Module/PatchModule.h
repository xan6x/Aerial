#pragma once

#include "Module/Module.h"
#include "Utils/Patch.h"

namespace aerial {

class PatchModule : public Module {
public:
    PatchModule(std::string name, std::string description, Category category, BytePatch patch);

    std::string suffix() const override;

protected:
    void onEnable() override;
    void onDisable() override;

private:
    BytePatch m_patch;
};

}
