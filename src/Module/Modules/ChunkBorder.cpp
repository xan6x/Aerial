#include "Module/Modules/ChunkBorder.h"

#include <cmath>

#include "Event/Events.h"
#include "Render/DrawUtils.h"
#include "Render/Projection.h"
#include "SDK/Context.h"
#include "SDK/Entity.h"
#include "Utils/Math.h"

namespace aerial::modules {
namespace {

constexpr float kChunkSize = 16.0f;
constexpr float kWorldBottom = 0.0f;
constexpr float kWorldTop = 256.0f;
constexpr float kLineThickness = 1.5f;

}

ChunkBorder::ChunkBorder()
    : Module("ChunkBorder", "Draws the boundaries of the chunk you are standing in",
             Category::Visuals) {
    m_gridSpacing = addInt("Grid spacing", "Blocks between the inner grid lines", 2, 1, 16);
    m_heightSpacing =
        addFloat("Height spacing", "Blocks between the horizontal rings", 4.0f, 1.0f, 32.0f, 1.0f);
    m_mid = addColour("Main lines", "The chunk's own grid", Colour::rgb(0xFFFF00));
    m_adjacent = addColour("Adjacent chunks", "The surrounding chunk corners", Colour::rgb(0xFF0000));
    m_corner = addColour("Corners", "The four corner columns", Colour::rgb(0x0000FF));

    listen<Render2DEvent>(&ChunkBorder::onRender);
}

void ChunkBorder::onRender(Render2DEvent& event) {
    (void)event;

    auto& ctx = sdk::Context::get();
    if (!ctx.worldInteractive() || !ctx.localPlayer)
        return;

    render::Camera cam;
    if (!cam.build(render::DrawUtils::screenSize()))
        return;

    const Vec3 playerPos = ctx.localPlayer->pos();
    const Vec3 anchor = {std::floor(playerPos.x / kChunkSize) * kChunkSize, 0.0f,
                         std::floor(playerPos.z / kChunkSize) * kChunkSize};

    const Colour mid = m_mid->value;
    const Colour adj = m_adjacent->value;
    const Colour corner = m_corner->value;

    const int grid = m_gridSpacing->value;
    const int divisions = 16 / grid;
    const float heightStep = m_heightSpacing->value;

    for (int i = 0; i <= divisions; ++i) {
        const float offset = static_cast<float>(i * grid);
        const Colour col = (i % divisions == 0) ? corner : mid;

        render::drawLine3D(cam, {anchor.x + offset, kWorldBottom, anchor.z},
                           {anchor.x + offset, kWorldTop, anchor.z}, col, kLineThickness);
        render::drawLine3D(cam, {anchor.x + offset, kWorldBottom, anchor.z + kChunkSize},
                           {anchor.x + offset, kWorldTop, anchor.z + kChunkSize}, col, kLineThickness);
        render::drawLine3D(cam, {anchor.x, kWorldBottom, anchor.z + offset},
                           {anchor.x, kWorldTop, anchor.z + offset}, col, kLineThickness);
        render::drawLine3D(cam, {anchor.x + kChunkSize, kWorldBottom, anchor.z + offset},
                           {anchor.x + kChunkSize, kWorldTop, anchor.z + offset}, col, kLineThickness);
    }

    for (float y = kWorldBottom; y <= kWorldTop; y += heightStep) {
        render::drawLine3D(cam, {anchor.x, y, anchor.z}, {anchor.x + kChunkSize, y, anchor.z}, mid,
                           kLineThickness);
        render::drawLine3D(cam, {anchor.x, y, anchor.z}, {anchor.x, y, anchor.z + kChunkSize}, mid,
                           kLineThickness);
        render::drawLine3D(cam, {anchor.x + kChunkSize, y, anchor.z},
                           {anchor.x + kChunkSize, y, anchor.z + kChunkSize}, mid, kLineThickness);
        render::drawLine3D(cam, {anchor.x, y, anchor.z + kChunkSize},
                           {anchor.x + kChunkSize, y, anchor.z + kChunkSize}, mid, kLineThickness);
    }

    for (int x = -1; x <= 2; ++x) {
        for (int z = -1; z <= 2; ++z) {
            if (x >= 0 && x <= 1 && z >= 0 && z <= 1)
                continue;
            render::drawLine3D(cam,
                               {anchor.x + static_cast<float>(x) * kChunkSize, kWorldBottom,
                                anchor.z + static_cast<float>(z) * kChunkSize},
                               {anchor.x + static_cast<float>(x) * kChunkSize, kWorldTop,
                                anchor.z + static_cast<float>(z) * kChunkSize},
                               adj, kLineThickness);
        }
    }
}

}
