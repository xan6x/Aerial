#pragma once

#include <string>

namespace aerial::packs {

// What the player's active resource packs offer for the sky.
//
// The game ships an end_sky and a menu panorama itself, so "is the texture
// loadable" answers nothing - every install has both. The only useful question
// is whether a pack the player turned on *replaces* one, and that is a question
// about files on disk, not about the renderer.
//
// Three layouts turn up in sky packs in the wild, and they are not equivalent:
//
//   textures/environment/end_sky
//       What the End's cube samples. On its own the same square lands on all
//       six faces - fine for a starfield, wrong for a photographic sky.
//
//   ...plus materials/sky.material
//       The pack redefines the end_sky material to point at its own shader,
//       which samples the texture as a six-face strip. This is the layout that
//       gives a real skybox, and it needs nothing from us but the branch patch.
//
//   textures/environment/overworld_cubemap/cubemap_0..5
//       Six separate face images, and what a pack means by "the overworld sky".
//       1.1.5 has never heard of this path - the string does not appear in the
//       binary - so the game will not load it whatever we do to the sky branch.
//
// Precedence runs bottom-up: a pack with a cubemap wants the cubemap. Packs
// routinely ship both, because end_sky is what the End wants and the cubemap is
// what the overworld wants, and they are different pictures - Legacy Mash pairs
// a 128-pixel starfield with six 2048-pixel space faces. Preferring end_sky
// because it is the one we can draw puts the wrong sky overhead.
struct SkyAssets {
    bool endSky = false;         // drawable as soon as the branch is patched
    bool cubemapShader = false;  // the pack also overrides the end_sky material
    bool faces = false;          // six overworld_cubemap images, not usable yet
    std::wstring facesDir;       // pack folder holding them, for the converter
};

// Reads the active pack list out of the game's own LocalState and looks for
// each layout, in the order the game applies them. Cheap enough to call on a
// module toggle; not cheap enough to call per frame.
SkyAssets scanActive();

} // namespace aerial::packs
