#pragma once

#include "Module/PatchModule.h"

namespace aerial::modules {

class NoDynamicFov final : public PatchModule {
public:
    NoDynamicFov();
};

} // namespace aerial::modules
