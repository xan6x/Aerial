#include "Module/Modules/AspectRatio.h"

#include <intrin.h>
#include <xmmintrin.h>

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

struct Mat4 {
    __m128 c0, c1, c2, c3;
};

using PerspFn = Mat4(__vectorcall*)(float, float, float, float);

Detour<PerspFn> g_perspective;

std::atomic<bool> g_enabled{false};
std::atomic<float> g_ratio{1.0f};

Mat4 __vectorcall onPerspective(float fovy, float aspect, float nearPlane, float farPlane) {
    if (g_enabled.load(std::memory_order_relaxed)) {
        const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
        if (caller == memory::rva(func::LevelRendererPlayer_setupCameraPerspReturn) ||
            caller == memory::rva(func::InGamePlayScreen_itemInHandPerspReturn))
            aspect *= g_ratio.load(std::memory_order_relaxed);
    }

    return g_perspective.call(fovy, aspect, nearPlane, farPlane);
}

bool install() {
    g_perspective.attach("Matrix::perspective", memory::rva(func::Matrix_perspective),
                         &onPerspective);
    return true;
}

const hooks::Installer g_installer{"AspectRatio", &install};

}

AspectRatio::AspectRatio()
    : Module("AspectRatio", "Stretches the view by overriding its aspect ratio",
             Category::Visuals) {
    m_ratio = addFloat("Ratio", "Multiplies the view's width-to-height ratio", 1.0f, 0.25f, 4.0f,
                       0.05f);

    listen<TickEvent>(&AspectRatio::onTick);
}

void AspectRatio::onTick(TickEvent&) {
    g_ratio.store(m_ratio->value, std::memory_order_relaxed);
}

void AspectRatio::onEnable() {
    g_ratio.store(m_ratio->value, std::memory_order_relaxed);
    g_enabled.store(true, std::memory_order_relaxed);
}

void AspectRatio::onDisable() { g_enabled.store(false, std::memory_order_relaxed); }

}
