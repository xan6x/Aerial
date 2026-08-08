#pragma once

#include "Module/Module.h"
#include "Utils/Patch.h"

namespace aerial::modules {

class PackSwitcher final : public Module {
public:
    PackSwitcher();

    std::string suffix() const override;

protected:
    void onEnable() override;
    void onDisable() override;

private:
    BytePatch m_availableTile;
    BytePatch m_selectedTile;
    BytePatch m_commit;
};

}
