#pragma once

#include <string>

#include "Module/Module.h"
#include "Utils/Patch.h"

namespace aerial::modules {

// Draws a custom sky from the player's resource pack.
//
// The game has two sky renderers and only one of them is textured: the End's,
// which is a cube. That is the whole reason an end_sky dropped into a pack
// shows up in the End and nowhere else. This module points every dimension at
// that renderer.
//
// It takes no settings by design - it looks at what the active packs actually
// contain and either has something to draw or leaves the sky alone.
class Skybox final : public Module {
public:
    Skybox();

    std::string suffix() const override;

protected:
    void onEnable() override;
    void onDisable() override;

private:
    // NOPs the `jne` that sends every dimension but the End to the untextured
    // sky. Anchored on the instructions that follow it, so the six bytes the
    // patch covers are exactly the branch.
    BytePatch m_patch{"0F 85 ? ? ? ? 48 8D 54 24 30 E8 ? ? ? ? 90 48 8B 5C 24 30",
                      {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}};

    // What the last scan found, so the menu can say why nothing happened.
    bool m_hasTexture = false;
};

} // namespace aerial::modules
