#include "Module/Modules/FullBright.h"

#include <atomic>

#include "Event/Events.h"
#include "Hooks/HookRegistry.h"
#include "SDK/Offsets.h"
#include "Utils/Hook.h"
#include "Utils/Memory.h"

namespace aerial::modules {
namespace {

namespace func = offsets::func;

Detour<float(__fastcall*)(void*)> g_getGamma;

std::atomic<bool> g_enabled{false};
std::atomic<float> g_gamma{1.0f};

float __fastcall onGetGamma(void* options) {
    const float value = g_getGamma.call(options);
    return g_enabled.load(std::memory_order_relaxed) ? g_gamma.load(std::memory_order_relaxed)
                                                     : value;
}

bool install() {
    g_getGamma.attach("Options::getGamma", memory::rva(func::Options_getGamma), &onGetGamma);
    return true;
}

const hooks::Installer g_installer{"FullBright", &install};

}

FullBright::FullBright() : Module("FullBright", "Lights up caves and night", Category::Visuals) {

    m_gamma = addFloat("Brightness", "Gamma the game is told to use", 1.0f, 0.5f, 5.0f, 0.1f);

    listen<Render2DEvent>(&FullBright::onRender);
}

void FullBright::onRender(Render2DEvent& event) {
    (void)event;
    g_gamma.store(m_gamma->value, std::memory_order_relaxed);
    g_enabled.store(true, std::memory_order_relaxed);
}

void FullBright::onDisable() { g_enabled.store(false, std::memory_order_relaxed); }

}
