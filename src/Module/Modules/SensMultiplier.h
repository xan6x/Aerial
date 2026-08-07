#pragma once

#include "Module/Module.h"

namespace aerial {
struct Render2DEvent;
}

namespace aerial::modules {

// Scales look sensitivity by intercepting the option the game reads it from.
class SensMultiplier final : public Module {
public:
    SensMultiplier();
    std::string suffix() const override;

protected:
    void onEnable() override;
    void onDisable() override;

private:
    void onRender(Render2DEvent& event);

    FloatSetting* m_multiplier;
};

} // namespace aerial::modules
