#pragma once

#include "Module/Module.h"

namespace aerial::modules {

class NoDynamicFov final : public Module {
public:
    NoDynamicFov();

protected:
    void onEnable() override;
    void onDisable() override;
};

}
