#include "Module/Modules/SensMultiplier.h"

#include <atomic>
#include <cstdint>

#include "Event/Events.h"
#include "Hooks/HookRegistry.h"
#include "SDK/Offsets.h"
#include "Utils/Hook.h"
#include "Utils/Memory.h"

namespace aerial::modules {
namespace {

namespace func = offsets::func;

constexpr uint32_t kSensitivityOptionId = 1;

Detour<float(__fastcall*)(void*, uint32_t)> g_getFloatOption;

std::atomic<uint32_t> g_scaledOptionId{0};
std::atomic<float> g_multiplier{1.0f};

float __fastcall onGetFloatOption(void* options, uint32_t id) {
    const float value = g_getFloatOption.call(options, id);

    const uint32_t scaled = g_scaledOptionId.load(std::memory_order_relaxed);
    if (scaled != 0 && id == scaled)
        return value * g_multiplier.load(std::memory_order_relaxed);

    return value;
}

bool install() {
    g_getFloatOption.attach("Options::getFloat", memory::rva(func::Options_getFloat),
                            &onGetFloatOption);
    return true;
}

const hooks::Installer g_installer{"SensMultiplier", &install};

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

    g_multiplier.store(m_multiplier->value, std::memory_order_relaxed);
    g_scaledOptionId.store(enabled() ? kSensitivityOptionId : 0, std::memory_order_relaxed);
}

void SensMultiplier::onEnable() {}

void SensMultiplier::onDisable() { g_scaledOptionId.store(0, std::memory_order_relaxed); }

}
