#include "Module/Modules/SensMultiplier.h"

#include "Event/Events.h"
#include "Hooks/Hooks.h"

namespace aerial::modules {
namespace {

constexpr uint32_t kSensitivityOptionId = 1;

}

SensMultiplier::SensMultiplier()
    : Module("SensMultiplier", "Scales look sensitivity beyond the game's own limit",
             Category::Input) {
    m_multiplier = addFloat("Multiplier", "Sensitivity is multiplied by this", 1.0f, 0.1f, 3.0f,
                            0.05f);

    listenAlways<Render2DEvent>(&SensMultiplier::onRender);
}

std::string SensMultiplier::suffix() const {
    if (!enabled())
        return {};
    return kSensitivityOptionId == 0 ? "id unknown" : std::string{};
}

void SensMultiplier::onRender(Render2DEvent& event) {
    (void)event;

    hooks::setOptionScale(enabled() ? kSensitivityOptionId : 0, m_multiplier->value);
}

void SensMultiplier::onEnable() {}

void SensMultiplier::onDisable() { hooks::setOptionScale(0, 1.0f); }

}
