#include "Module/Modules/Skybox.h"

#include "Hooks/Hooks.h"
#include "Utils/Logger.h"
#include "Utils/ResourcePacks.h"

namespace aerial::modules {

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
        hooks::setSkybox(true);
        hooks::setSkyCubemap(true);
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

    hooks::setSkybox(true);
}

void Skybox::onDisable() {
    hooks::setSkyCubemap(false);
    hooks::setSkybox(false);
    m_patch.revert();
}

}
