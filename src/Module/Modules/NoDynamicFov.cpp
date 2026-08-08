#include "Module/Modules/NoDynamicFov.h"

#include <atomic>
#include <cstdint>

#include "Hooks/HookRegistry.h"
#include "SDK/Offsets.h"
#include "Utils/Hook.h"
#include "Utils/Memory.h"

namespace aerial::modules {
namespace {

namespace func = offsets::func;
namespace field = offsets::field::levelRendererCamera;

constexpr float kNeutral = 1.0f;

Detour<void(__fastcall*)(void*)> g_levelRendererTick;

std::atomic<bool> g_enabled{false};

void __fastcall onLevelRendererTick(void* self) {
    g_levelRendererTick.call(self);

    if (!g_enabled.load(std::memory_order_relaxed) || !self ||
        !memory::isReadable(self, field::fovModifierOld + sizeof(float)))
        return;

    auto* bytes = static_cast<uint8_t*>(self);
    *reinterpret_cast<float*>(bytes + field::fovModifier) = kNeutral;
    *reinterpret_cast<float*>(bytes + field::fovModifierOld) = kNeutral;
}

bool install() {
    g_levelRendererTick.attach("LevelRenderer::tick", memory::rva(func::LevelRenderer_tick),
                               &onLevelRendererTick);
    return true;
}

const hooks::Installer g_installer{"NoDynamicFov", &install};

}

NoDynamicFov::NoDynamicFov()
    : Module("NoDynamicFov", "Holds the field of view at your setting, whatever speed you are",
             Category::Visuals) {}

void NoDynamicFov::onEnable() { g_enabled.store(true, std::memory_order_relaxed); }

void NoDynamicFov::onDisable() { g_enabled.store(false, std::memory_order_relaxed); }

}
