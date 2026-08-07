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

float combined() {
    float scale = 1.0f;
    for (auto& source : g_scales)
        scale *= source.load(std::memory_order_relaxed);
    return scale;
}

float __fastcall onGetFov(void* self, float partialTicks, bool a3) {
    return g_getFov.call(self, partialTicks, a3) * combined();
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

}
