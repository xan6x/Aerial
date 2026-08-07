#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace aerial::render {

// Draws a resource pack's textures/environment/overworld_cubemap as the sky.
//
// This build of the game has never heard of that path - the string appears
// nowhere in the binary - so nothing loads those images on their own. Its
// texture system is driven by path rather than by a fixed list, though, so
// asking for one by name works whether or not the game would ever have asked.
//
// The geometry is the game's own: the menu panorama draws a six-face cubemap as
// six quads through the tessellator, one Tessellator::draw2 per face with the
// material and the texture passed in. That is copied here, with the world sky's
// material and the pack's faces, and run from inside the renderSky hook so it
// lands in the same matrix and depth state the End cube would have.
class SkyCubemap {
public:
    static SkyCubemap& get();

    // Resolves the six faces through the game's texture group. Needs a live
    // ClientInstance, so it is called from the render thread rather than from
    // the module toggle. Cheap and idempotent once loaded.
    bool load();

    // Drops the handles. The textures themselves stay in the game's group -
    // see the note in the source about why they are not released.
    void unload();

    bool ready() const { return m_ready; }

    // Draws the cube. `camera` is the LevelRendererCamera renderSky was called
    // on, which owns both the sky material and the matrix state.
    void draw(void* camera);

    // Why nothing is being drawn, for the log and the module's menu entry.
    const char* status() const { return m_status; }

private:
    SkyCubemap() = default;

    bool m_ready = false;
    bool m_failed = false;
    bool m_warnedBatch = false;
    bool m_drewOnce = false;
    const char* m_status = "not loaded";

    // Six mce::TexturePtr, raw. The type is opaque to us: it is constructed by
    // the game into this storage and only ever handed straight back.
    std::vector<uint8_t> m_faces;
};

} // namespace aerial::render
