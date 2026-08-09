#include "Module/Modules/Skybox.h"

#include <atomic>
#include <cstdint>

#include "Event/Events.h"
#include "Hooks/HookRegistry.h"
#include "Hooks/ResourceHooks.h"
#include "Render/SkyCubemap.h"
#include "SDK/Offsets.h"
#include "Utils/Hook.h"
#include "Utils/Logger.h"
#include "Utils/Memory.h"
#include "Utils/Obfusc.h"
#include "Utils/Patch.h"
#include "Utils/ResourcePacks.h"

namespace aerial::modules {
namespace {

namespace func = offsets::func;

Detour<void(__fastcall*)(void*, float, float)> g_renderSky;

std::atomic<bool> g_moduleOn{false};
std::atomic<bool> g_skybox{false};
std::atomic<bool> g_cubemap{false};
std::atomic<int> g_found{0};
std::atomic<int> g_repackTicks{0};

BytePatch g_skyPatch(AERIAL_STR("0F 85 ? ? ? ? 48 8D 54 24 30 E8 ? ? ? ? 90 48 8B 5C 24 30"),
                     {0x90, 0x90, 0x90, 0x90, 0x90, 0x90});

void hideVanillaSky(bool hide) {
    if (hide) {
        if (!g_skyPatch.applied())
            g_skyPatch.apply();
    } else if (g_skyPatch.applied()) {
        g_skyPatch.revert();
    }
}

void refreshFromPacks() {
    const packs::SkyAssets assets = packs::scanActive();
    render::SkyCubemap::get().unload();

    if (assets.faces) {
        g_found.store(1, std::memory_order_relaxed);
        g_cubemap.store(true, std::memory_order_relaxed);
        g_skybox.store(true, std::memory_order_relaxed);
    } else if (assets.endSky) {
        g_found.store(2, std::memory_order_relaxed);
        g_cubemap.store(false, std::memory_order_relaxed);
        g_skybox.store(true, std::memory_order_relaxed);
    } else {
        g_found.store(0, std::memory_order_relaxed);
        g_cubemap.store(false, std::memory_order_relaxed);
        g_skybox.store(false, std::memory_order_relaxed);
    }
}

void setFog(void* self, const float value[4], float saved[4]) {
    constexpr ptrdiff_t kOffset = offsets::field::levelRendererCamera::fogColour;
    if (!memory::isReadable(self, kOffset + sizeof(float) * 4))
        return;
    auto* colour = reinterpret_cast<float*>(static_cast<uint8_t*>(self) + kOffset);
    for (int i = 0; i < 4; ++i) {
        saved[i] = colour[i];
        colour[i] = value[i];
    }
}

void restoreFog(void* self, const float saved[4]) {
    constexpr ptrdiff_t kOffset = offsets::field::levelRendererCamera::fogColour;
    if (!memory::isReadable(self, kOffset + sizeof(float) * 4))
        return;
    auto* colour = reinterpret_cast<float*>(static_cast<uint8_t*>(self) + kOffset);
    for (int i = 0; i < 4; ++i)
        colour[i] = saved[i];
}

void __fastcall onRenderSky(void* self, float a, float b) {
    if (!g_skybox.load(std::memory_order_relaxed)) {
        hideVanillaSky(false);
        g_renderSky.call(self, a, b);
        return;
    }

    const bool cubemap = g_cubemap.load(std::memory_order_relaxed);

    auto& sky = render::SkyCubemap::get();
    const bool drawCube = cubemap && sky.refresh();

    if (cubemap && !drawCube) {
        hideVanillaSky(false);
        g_renderSky.call(self, a, b);
        return;
    }

    hideVanillaSky(true);

    const float black[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const float grey[4] = {0.5f, 0.5f, 0.5f, 0.5f};
    float saved[4]{};
    setFog(self, cubemap ? black : grey, saved);

    g_renderSky.call(self, a, b);

    restoreFog(self, saved);

    using SunOrMoon = void(__fastcall*)(void*, float, bool);
    using Stars = void(__fastcall*)(void*, float, float);
    auto sunOrMoon =
        reinterpret_cast<SunOrMoon>(memory::rva(func::LevelRendererCamera_renderSunOrMoon));
    auto stars = reinterpret_cast<Stars>(memory::rva(func::LevelRendererCamera_renderStars));

    sunOrMoon(self, b, true);
    sunOrMoon(self, b, false);
    stars(self, b, a);

    if (drawCube) {
        const float bright[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float savedFog[4]{};
        setFog(self, bright, savedFog);

        auto* skyColour = reinterpret_cast<float*>(memory::rva(func::g_skyColour));
        float savedSky[4]{};
        const bool haveSky = memory::isReadable(skyColour, sizeof(float) * 4);
        if (haveSky) {
            for (int i = 0; i < 4; ++i) {
                savedSky[i] = skyColour[i];
                skyColour[i] = 1.0f;
            }
        }

        sky.draw(self);

        if (haveSky)
            for (int i = 0; i < 4; ++i)
                skyColour[i] = savedSky[i];

        restoreFog(self, savedFog);
    }
}

bool install() {
    g_renderSky.attach("LevelRendererCamera::renderSky",
                       memory::rva(func::LevelRendererCamera_renderSky), &onRenderSky);
    return true;
}

const hooks::Installer g_installer{"Skybox", &install};

}

Skybox::Skybox()
    : Module("Skybox", "Draws the sky from your resource pack instead of the vanilla gradient",
             Category::Visuals) {
    listen<TickEvent>(&Skybox::onTick);
}

void Skybox::onTick(TickEvent&) {
    const uint32_t gen = hooks::resourcesGeneration();
    if (gen != m_lastGeneration) {
        m_lastGeneration = gen;
        g_repackTicks.store(80, std::memory_order_relaxed);
    }

    int ticks = g_repackTicks.load(std::memory_order_relaxed);
    if (ticks <= 0)
        return;

    if (ticks % 20 == 0)
        refreshFromPacks();

    g_repackTicks.store(ticks - 1, std::memory_order_relaxed);
}

std::string Skybox::suffix() const {
    if (!enabled())
        return {};

    switch (g_found.load(std::memory_order_relaxed)) {
    case 1:
        return "cubemap";
    case 2:
        return {};
    default:
        return "no sky in pack";
    }
}

void Skybox::onEnable() {
    g_moduleOn.store(true, std::memory_order_relaxed);
    m_lastGeneration = hooks::resourcesGeneration();
    refreshFromPacks();

    switch (g_found.load(std::memory_order_relaxed)) {
    case 1:
        m_found = Found::Cubemap;
        LOG_INFO("Skybox", "drawing the pack's overworld_cubemap");
        break;
    case 2:
        m_found = Found::EndSky;
        LOG_INFO("Skybox", "using the pack's end_sky");
        break;
    default:
        m_found = Found::Nothing;
        LOG_INFO("Skybox", "no active pack replaces the sky");
        break;
    }
}

void Skybox::onDisable() {
    g_moduleOn.store(false, std::memory_order_relaxed);
    g_cubemap.store(false, std::memory_order_relaxed);
    g_skybox.store(false, std::memory_order_relaxed);
    render::SkyCubemap::get().unload();
    g_skyPatch.revert();
}

}
