#pragma once

#include "Module/Module.h"

namespace aerial::modules {

class Direct2D final : public Module {
public:
    Direct2D();
    std::string suffix() const override;

protected:
    void onEnable() override;
    void onDisable() override;
};

}
