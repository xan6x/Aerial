#pragma once

#include "Module/Module.h"

namespace aerial::modules {

class Overlay final : public Module {
public:
    Overlay();
    std::string suffix() const override;

protected:
    void onEnable() override;
    void onDisable() override;
};

}
