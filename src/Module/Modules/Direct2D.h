#pragma once

#include "Module/Module.h"

namespace aerial::modules {

// Switches the interface between the Direct2D overlay and the game's own
// renderer at runtime. Kept as a module so a rendering problem can be pinned on
// the overlay in one click instead of a rebuild.
class Direct2D final : public Module {
public:
    Direct2D();
    std::string suffix() const override;

protected:
    void onEnable() override;
    void onDisable() override;
};

} // namespace aerial::modules
