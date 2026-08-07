#pragma once

#include "Module/Module.h"
#include "Utils/Patch.h"

namespace aerial {

// Base for a module that is nothing but a reversible patch to the game's code.
// The signature and the replacement bytes live with the module that owns them,
// one per file, so a patch can be read without hunting through a shared one.
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

} // namespace aerial
