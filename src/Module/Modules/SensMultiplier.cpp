#include "Module/Modules/SensMultiplier.h"

#include "Event/Events.h"
#include "Hooks/Hooks.h"

namespace aerial::modules {
namespace {

// Option id the sensitivity slider writes, found by logging every id passed to
// Options::getFloat and dragging the slider: id 1 was the one that moved.
//
// Two different options objects read this id, and their values track each other
// as the slider moves - one per input device. Scaling the id therefore covers
// mouse and controller alike, which is what makes this a single setting rather
// than one per device.
constexpr uint32_t kSensitivityOptionId = 1;

} // namespace

SensMultiplier::SensMultiplier()
    : Module("SensMultiplier", "Scales look sensitivity beyond the game's own limit",
             Category::Input) {
    m_multiplier = addFloat("Multiplier", "Sensitivity is multiplied by this", 1.0f, 0.1f, 3.0f,
                            0.05f);

    // Pushed every frame because a setting has no change callback; the write is
    // a relaxed atomic store.
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

} // namespace aerial::modules
