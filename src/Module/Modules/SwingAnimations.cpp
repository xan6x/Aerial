#include "Module/Modules/SwingAnimations.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "Event/Events.h"
#include "Hooks/HookRegistry.h"
#include "SDK/Offsets.h"
#include "Utils/Hook.h"
#include "Utils/Memory.h"
#include "Utils/Obfusc.h"
#include "Utils/Patch.h"

namespace aerial::modules {
namespace {

namespace func = offsets::func;

Detour<int(__fastcall*)(void*)> g_getSwingDuration;

std::atomic<bool> g_enabled{false};
std::atomic<float> g_speed{1.0f};

int __fastcall onGetSwingDuration(void* self) {
    const int original = g_getSwingDuration.call(self);

    if (!g_enabled.load(std::memory_order_relaxed))
        return original;

    const float speed = g_speed.load(std::memory_order_relaxed);
    if (speed <= 0.0f)
        return original;

    return std::max(1, static_cast<int>(std::lround(static_cast<float>(original) / speed)));
}

bool install() {
    g_getSwingDuration.attach("Mob::getCurrentSwingDuration",
                              memory::rva(func::Mob_getCurrentSwingDuration), &onGetSwingDuration);
    return true;
}

const hooks::Installer g_installer{"SwingAnimations", &install};

constexpr float kVanillaSwingAngle = -80.0f;
constexpr int kInstrLength = 9;
constexpr int kDispOffset = 5;

const char* const kSwingSites[] = {
    "F3 44 0F 59 0D 3C 46 C5 00",  // ItemInHandRenderer::render+5f0
    "F3 44 0F 59 0D 6E 45 C5 00",  // ItemInHandRenderer::render+6be
};

class SwingAngleRedirect {
public:
    float* value() { return m_storage; }

    bool applied() const { return m_applied; }

    bool apply() {
        if (m_applied)
            return true;

        uintptr_t sites[std::size(kSwingSites)];
        for (size_t i = 0; i < std::size(kSwingSites); ++i) {
            sites[i] = memory::findPattern(kSwingSites[i]);
            if (!sites[i])
                return false;
        }

        if (!m_storage) {
            m_storage = allocNear(sites[0] + kInstrLength);
            if (!m_storage)
                return false;
            *m_storage = kVanillaSwingAngle;
        }

        for (size_t i = 0; i < std::size(kSwingSites); ++i) {
            const uintptr_t dispField = sites[i] + kDispOffset;
            const uintptr_t instrEnd = sites[i] + kInstrLength;
            const intptr_t rel = reinterpret_cast<intptr_t>(m_storage) - static_cast<intptr_t>(instrEnd);
            if (rel > INT32_MAX || rel < INT32_MIN)
                return false;

            std::memcpy(&m_originalDisp[i], reinterpret_cast<const void*>(dispField), 4);
            const int32_t newDisp = static_cast<int32_t>(rel);
            if (!memory::patch(dispField, &newDisp, 4))
                return false;
            m_sites[i] = dispField;
        }

        m_applied = true;
        return true;
    }

    void revert() {
        if (!m_applied)
            return;
        for (size_t i = 0; i < std::size(kSwingSites); ++i)
            memory::patch(m_sites[i], &m_originalDisp[i], 4);
        m_applied = false;
    }

private:
    static float* allocNear(uintptr_t target) {
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        const uintptr_t gran = si.dwAllocationGranularity;
        const uintptr_t span = 0x70000000;

        for (uintptr_t delta = gran; delta < span; delta += gran) {
            for (const intptr_t dir : {-1, 1}) {
                const uintptr_t candidate = (target + dir * static_cast<intptr_t>(delta)) & ~(gran - 1);
                void* page =
                    VirtualAlloc(reinterpret_cast<void*>(candidate), sizeof(float),
                                 MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
                if (page)
                    return static_cast<float*>(page);
            }
        }
        return nullptr;
    }

    float* m_storage = nullptr;
    uintptr_t m_sites[std::size(kSwingSites)]{};
    int32_t m_originalDisp[std::size(kSwingSites)]{};
    bool m_applied = false;
};

SwingAngleRedirect g_angle;

}

SwingAnimations::SwingAnimations()
    : Module("SwingAnimations", "Speeds up, smooths and reshapes the hand swing animation",
             Category::Visuals),
      m_fluxPatch(AERIAL_STR("41 83 BB E0 10 00 00 00 7D 13"),
                  {0x41, 0x83, 0xBB, 0xE0, 0x10, 0x00, 0x00, 0x00, 0x90, 0x90}) {
    m_speed = addFloat("Speed", "How much faster the swing plays", 1.0f, 0.1f, 3.0f, 0.05f);
    m_flux = addBool("Flux swing", "Restart the swing on every hit for a continuous motion", true);
    m_angle = addFloat("Angle", "Swing rotation, -80 is vanilla", -80.0f, -180.0f, 90.0f, 1.0f);

    listen<Render2DEvent>(&SwingAnimations::onRender);
}

void SwingAnimations::onRender(Render2DEvent& event) {
    (void)event;

    g_speed.store(m_speed->value, std::memory_order_relaxed);
    g_enabled.store(enabled(), std::memory_order_relaxed);

    if (enabled() && m_flux->value) {
        if (!m_fluxPatch.applied())
            m_fluxPatch.apply();
    } else if (m_fluxPatch.applied()) {
        m_fluxPatch.revert();
    }

    if (enabled()) {
        if (!g_angle.applied())
            g_angle.apply();
        if (float* slot = g_angle.value())
            *slot = m_angle->value;
    } else if (g_angle.applied()) {
        g_angle.revert();
    }
}

void SwingAnimations::onDisable() {
    g_enabled.store(false, std::memory_order_relaxed);
    m_fluxPatch.revert();
    g_angle.revert();
}

}
