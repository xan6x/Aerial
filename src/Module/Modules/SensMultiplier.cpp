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

constexpr uint32_t kMouseKeyboardId = 1;
constexpr uint32_t kTouchId = 2;
constexpr uint32_t kControllerId = 3;

Detour<float(__fastcall*)(void*, uint32_t)> g_getFloatOption;

std::atomic<bool> g_scaleMouseKeyboard{false};
std::atomic<bool> g_scaleController{false};
std::atomic<bool> g_scaleTouch{false};
std::atomic<float> g_multiplier{1.0f};

bool scales(uint32_t id) {
    switch (id) {
    case kMouseKeyboardId: return g_scaleMouseKeyboard.load(std::memory_order_relaxed);
    case kTouchId:         return g_scaleTouch.load(std::memory_order_relaxed);
    case kControllerId:    return g_scaleController.load(std::memory_order_relaxed);
    default:               return false;
    }
}

float __fastcall onGetFloatOption(void* options, uint32_t id) {
    const float value = g_getFloatOption.call(options, id);

    if (scales(id))
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
    m_mouseKeyboard = addBool("Mouse & keyboard", "Scale mouse and keyboard look", true);
    m_controller = addBool("Controller", "Scale controller look", false);
    m_touch = addBool("Touch", "Scale touch look", false);

    listenAlways<Render2DEvent>(&SensMultiplier::onRender);
}

void SensMultiplier::onRender(Render2DEvent& event) {
    (void)event;

    const bool on = enabled();
    g_multiplier.store(m_multiplier->value, std::memory_order_relaxed);
    g_scaleMouseKeyboard.store(on && m_mouseKeyboard->value, std::memory_order_relaxed);
    g_scaleController.store(on && m_controller->value, std::memory_order_relaxed);
    g_scaleTouch.store(on && m_touch->value, std::memory_order_relaxed);
}

void SensMultiplier::onEnable() {}

void SensMultiplier::onDisable() {
    g_scaleMouseKeyboard.store(false, std::memory_order_relaxed);
    g_scaleController.store(false, std::memory_order_relaxed);
    g_scaleTouch.store(false, std::memory_order_relaxed);
}

}
