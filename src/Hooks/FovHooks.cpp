#include "Hooks/FovHooks.h"

#include <array>
#include <atomic>
#include <cstddef>

#include "Hooks/HookRegistry.h"
#include "SDK/Offsets.h"
#include "Utils/Hook.h"
#include "Utils/Memory.h"

namespace aerial::hooks {
namespace {

namespace func = offsets::func;

constexpr size_t kSources = static_cast<size_t>(Fov::Count);

Detour<float(__fastcall*)(void*, float, bool)> g_getFov;

std::array<std::atomic<float>, kSources> g_scales = {};

std::atomic<float> g_lastFov{70.0f};
std::atomic<float> g_lastPartialTicks{0.0f};
std::atomic<float> g_itemFov{0.0f};

float combined() {
    float scale = 1.0f;
    for (auto& source : g_scales)
        scale *= source.load(std::memory_order_relaxed);
    return scale;
}

float __fastcall onGetFov(void* self, float partialTicks, bool a3) {
    float base = g_getFov.call(self, partialTicks, a3);

    if (!a3) {
        const float itemFov = g_itemFov.load(std::memory_order_relaxed);
        if (itemFov > 0.0f && base > 69.0f && base < 71.0f)
            return itemFov;
    }

    const float fov = base * combined();
    g_lastFov.store(fov, std::memory_order_relaxed);
    g_lastPartialTicks.store(partialTicks, std::memory_order_relaxed);
    return fov;
}

bool install() {
    for (auto& source : g_scales)
        source.store(1.0f, std::memory_order_relaxed);

    g_getFov.attach("LevelRendererPlayer::getFov", memory::rva(func::LevelRendererPlayer_getFov),
                    &onGetFov);
    return true;
}

const Installer g_installer{"Fov", &install};

}

void setFovScale(Fov source, float multiplier) {
    g_scales[static_cast<size_t>(source)].store(multiplier, std::memory_order_relaxed);
}

void setItemFov(float fov) { g_itemFov.store(fov, std::memory_order_relaxed); }

float currentFov() { return g_lastFov.load(std::memory_order_relaxed); }

float currentPartialTicks() { return g_lastPartialTicks.load(std::memory_order_relaxed); }

}
