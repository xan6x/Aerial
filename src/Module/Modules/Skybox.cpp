#include "Module/Modules/Skybox.h"

#include <atomic>
#include <cstdint>

#include "Hooks/HookRegistry.h"
#include "Render/SkyCubemap.h"
#include "SDK/Offsets.h"
#include "Utils/Hook.h"
#include "Utils/Logger.h"
#include "Utils/Memory.h"
#include "Utils/ResourcePacks.h"

namespace aerial::modules {
namespace {

namespace func = offsets::func;

Detour<void(__fastcall*)(void*, float, float)> g_renderSky;

std::atomic<bool> g_skybox{false};
std::atomic<bool> g_cubemap{false};

void __fastcall onRenderSky(void* self, float a, float b) {
    if (!g_skybox.load(std::memory_order_relaxed)) {
        g_renderSky.call(self, a, b);
        return;
    }

    const bool cubemap = g_cubemap.load(std::memory_order_relaxed);

    constexpr ptrdiff_t kOffset = offsets::field::levelRendererCamera::fogColour;
    float* colour = nullptr;
    float saved[4]{};

    if (memory::isReadable(self, kOffset + sizeof(float) * 4)) {
        colour = reinterpret_cast<float*>(static_cast<uint8_t*>(self) + kOffset);
        for (int i = 0; i < 4; ++i) {
            saved[i] = colour[i];
            colour[i] = cubemap ? 0.0f : 0.5f;
        }
    }

    g_renderSky.call(self, a, b);

    if (colour)
        for (int i = 0; i < 4; ++i)
            colour[i] = saved[i];

    using SunOrMoon = void(__fastcall*)(void*, float, bool);
    using Stars = void(__fastcall*)(void*, float, float);
    auto sunOrMoon =
        reinterpret_cast<SunOrMoon>(memory::rva(func::LevelRendererCamera_renderSunOrMoon));
    auto stars = reinterpret_cast<Stars>(memory::rva(func::LevelRendererCamera_renderStars));

    sunOrMoon(self, b, true);
    sunOrMoon(self, b, false);
    stars(self, b, a);

    if (cubemap) {
        auto& sky = render::SkyCubemap::get();
        if (sky.ready() || sky.load())
            sky.draw(self);
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
             Category::Visuals) {}

std::string Skybox::suffix() const {
    if (!enabled())
        return {};

    if (!m_patch.applied())
        return m_found == Found::Nothing ? "no sky in pack" : "unavailable";

    return m_found == Found::Cubemap ? "cubemap" : std::string{};
}

void Skybox::onEnable() {
    const packs::SkyAssets assets = packs::scanActive();

    if (assets.faces) {
        m_found = Found::Cubemap;
        LOG_INFO("Skybox", "drawing the pack's overworld_cubemap");

        if (!m_patch.apply()) {
            LOG_ERROR("Skybox", "the sky branch patch could not be applied");
            return;
        }
        g_skybox.store(true, std::memory_order_relaxed);
        g_cubemap.store(true, std::memory_order_relaxed);
        return;
    }

    if (!assets.endSky) {
        m_found = Found::Nothing;
        LOG_INFO("Skybox", "no active pack replaces the sky");
        return;
    }

    m_found = Found::EndSky;
    LOG_INFO("Skybox", "using the pack's end_sky{}",
             assets.cubemapShader ? " with its own cubemap shader" : "");

    if (!m_patch.apply()) {
        LOG_ERROR("Skybox", "the sky branch patch could not be applied");
        return;
    }

    g_skybox.store(true, std::memory_order_relaxed);
}

void Skybox::onDisable() {
    g_cubemap.store(false, std::memory_order_relaxed);
    g_skybox.store(false, std::memory_order_relaxed);
    render::SkyCubemap::get().unload();
    m_patch.revert();
}

}
