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

    // Once it is drawing, the only thing worth saying is which of the two skies
    // a pack turned out to have.
    return m_found == Found::Cubemap ? "cubemap" : std::string{};
}

void Skybox::onEnable() {
    const packs::SkyAssets assets = packs::scanActive();

    // The cubemap wins whenever a pack has one, even if it also ships an
    // end_sky - and packs that ship both are common, because end_sky is what
    // the End wants and the cubemap is what the overworld wants. Checking
    // end_sky first is why this module put the End's 128-pixel starfield in the
    // overworld of a pack whose real sky was six 2048-pixel faces sitting right
    // next to it. That is not a fallback, it is the wrong picture.
    if (assets.faces) {
        m_found = Found::Cubemap;
        LOG_INFO("Skybox", "drawing the pack's overworld_cubemap");

        // Both, and in this order. The patch is what stops the procedural sky
        // drawing its gradient over everything; the cubemap is then drawn last,
        // inside the End cube, so the cube's single texture never shows.
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

} // namespace aerial::modules
