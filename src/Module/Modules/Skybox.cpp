#include "Module/Modules/Skybox.h"

#include "Hooks/Hooks.h"
#include "Utils/Logger.h"
#include "Utils/ResourcePacks.h"

namespace aerial::modules {

Skybox::Skybox()
    : Module("Skybox", "Draws the sky from your resource pack instead of the vanilla gradient",
             Category::Render) {}

std::string Skybox::suffix() const {
    if (!enabled())
        return {};
    if (!m_hasTexture)
        return "no sky in pack";
    return m_patch.applied() ? std::string{} : "unavailable";
}

void Skybox::onEnable() {
    const packs::SkyAssets assets = packs::scanActive();
    m_hasTexture = assets.endSky;

    // The six-file layout is not drawable yet. 1.1.5 has never heard of that
    // path, so the game does not load those images on its own - but its texture
    // system is driven by path, not by a fixed list, so asking for them by name
    // does work. That is what the runtime cubemap path will do; until it is
    // written, say so and leave the sky alone.
    if (assets.faces && !assets.endSky) {
        LOG_WARN("Skybox", "the active pack uses textures/environment/overworld_cubemap, which is "
                           "not drawn yet; leaving the sky alone");
        return;
    }

    if (!m_hasTexture) {
        LOG_INFO("Skybox", "no active pack replaces textures/environment/end_sky");
        return;
    }

    LOG_INFO("Skybox", "using the pack's end_sky{}", assets.cubemapShader ? " with its own cubemap shader" : "");

    if (!m_patch.apply()) {
        LOG_ERROR("Skybox", "the sky branch patch could not be applied");
        return;
    }

    hooks::setSkybox(true);
}

void Skybox::onDisable() {
    hooks::setSkybox(false);
    m_patch.revert();
}

} // namespace aerial::modules
