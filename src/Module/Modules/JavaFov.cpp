#include "Module/Modules/JavaFov.h"

#include <atomic>
#include <cmath>

#include "Event/Events.h"
#include "Hooks/HookRegistry.h"
#include "SDK/Context.h"
#include "SDK/Entity.h"
#include "SDK/Offsets.h"
#include "Utils/Hook.h"
#include "Utils/Memory.h"

namespace aerial::modules {
namespace {

namespace func = offsets::func;

Detour<float(__fastcall*)(void*, float, bool)> g_getFov;

std::atomic<bool> g_enabled{false};
std::atomic<float> g_scale{1.0f};

float __fastcall onGetFov(void* self, float partialTicks, bool a3) {
    const float value = g_getFov.call(self, partialTicks, a3);
    return g_enabled.load(std::memory_order_relaxed)
               ? value * g_scale.load(std::memory_order_relaxed)
               : value;
}

bool install() {
    g_getFov.attach("LevelRendererPlayer::getFov", memory::rva(func::LevelRendererPlayer_getFov),
                    &onGetFov);
    return true;
}

const hooks::Installer g_installer{"JavaFov", &install};

}

JavaFov::JavaFov() : Module("JavaFov", "Java's sprint field-of-view easing", Category::Visuals) {

    m_sprintFov = addFloat("Sprint FOV", "Multiplier while sprinting", 1.15f, 1.0f, 1.5f, 0.01f);
    m_smoothing = addFloat("Smoothing", "How quickly it eases", 0.15f, 0.02f, 1.0f, 0.01f);

    listen<Render2DEvent>(&JavaFov::onRender);
}

void JavaFov::onRender(Render2DEvent& event) {
    (void)event;

    auto* player = sdk::Context::get().localPlayer;
    const bool sprinting = player && player->isSprinting();
    const float target = sprinting ? m_sprintFov->value : 1.0f;

    m_current += (target - m_current) * m_smoothing->value;
    if (std::fabs(target - m_current) < 0.0005f)
        m_current = target;

    g_scale.store(m_current, std::memory_order_relaxed);
    g_enabled.store(true, std::memory_order_relaxed);
}

void JavaFov::onDisable() {
    m_current = 1.0f;
    g_enabled.store(false, std::memory_order_relaxed);
}

}
