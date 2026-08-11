#include "Module/Modules/Hitboxes.h"

#include "Event/Events.h"
#include "Hooks/FovHooks.h"
#include "Render/DrawUtils.h"
#include "Render/Projection.h"
#include "SDK/Context.h"
#include "SDK/Entity.h"
#include "SDK/Level.h"
#include "Utils/Math.h"
#include "Utils/Memory.h"

namespace aerial::modules {
namespace {

constexpr float kEyeHeightFraction = 0.85f;

}

Hitboxes::Hitboxes()
    : Module("Hitboxes", "Draws the bounding boxes of nearby players", Category::Visuals) {
    m_colour = addColour("Hitbox", "Box colour", Colour::rgb(0xFFFFFF));
    m_thickness = addFloat("Thickness", "Line width in pixels", 1.4f, 0.5f, 5.0f, 0.1f);
    m_expand = addFloat("Expand", "Grow the box outward, in blocks", 0.0f, 0.0f, 1.0f, 0.05f);
    m_showSelf = addBool("Show self", "Draw your own box (third person)", false);

    m_eyeLine = addBool("Eye line", "Ring at eye level", false);
    m_eyeColour = addColour("Eye line colour", "", Colour::rgb(0xFF0000));
    m_eyeColour->onlyIf([this] { return m_eyeLine->value; });

    m_lookLine = addBool("Look line", "Line showing where they look", false);
    m_lookColour = addColour("Look line colour", "", Colour::rgb(0x0000FF));
    m_lookColour->onlyIf([this] { return m_lookLine->value; });
    m_lookLength = addFloat("Look length", "How far the look line reaches", 2.0f, 0.5f, 10.0f, 0.5f);
    m_lookLength->onlyIf([this] { return m_lookLine->value; });

    listen<Render2DEvent>(&Hitboxes::onRender);
}

void Hitboxes::onRender(Render2DEvent& event) {
    (void)event;

    auto& ctx = sdk::Context::get();
    if (!ctx.worldInteractive() || !ctx.localPlayer || !ctx.level)
        return;

    render::Camera cam;
    if (!cam.build(render::DrawUtils::screenSize()))
        return;

    const float thickness = m_thickness->value;
    const float expand = m_expand->value;
    const Colour colour = m_colour->value;
    const float partial = hooks::currentPartialTicks();
    auto* self = static_cast<sdk::Player*>(ctx.localPlayer);

    for (sdk::Player* player : ctx.level->players()) {
        if (!player)
            continue;
        if (player == self && !m_showSelf->value)
            continue;
        if (!memory::isReadable(player, 0x180))
            continue;

        AABB box = player->worldAABB();
        const Vec3 size = box.max - box.min;
        if (size.x <= 0.0f || size.y <= 0.0f || size.z <= 0.0f || size.x > 4.0f || size.y > 4.0f ||
            size.z > 4.0f)
            continue;

        const Vec3 shift = player->renderPos(partial) - player->pos();
        if (shift.lengthSquared() < 64.0f) {
            box.min += shift;
            box.max += shift;
        }

        if (expand > 0.0f) {
            box.min -= Vec3{expand, expand, expand};
            box.max += Vec3{expand, expand, expand};
        }

        bool anyVisible = false;
        for (int i = 0; i < 8 && !anyVisible; ++i) {
            const Vec3 corner{(i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y,
                              (i & 4) ? box.max.z : box.min.z};
            anyVisible = cam.isVisible(corner, 1.3f);
        }
        if (!anyVisible)
            continue;

        render::drawBox3D(cam, box, colour, thickness);

        const float height = box.max.y - box.min.y;
        const float eyeY = box.min.y + height * kEyeHeightFraction;
        const float centreX = (box.min.x + box.max.x) * 0.5f;
        const float centreZ = (box.min.z + box.max.z) * 0.5f;

        if (m_eyeLine->value) {
            const Colour eye = m_eyeColour->value;
            render::drawLine3D(cam, {box.min.x, eyeY, box.min.z}, {box.max.x, eyeY, box.min.z}, eye,
                               thickness);
            render::drawLine3D(cam, {box.max.x, eyeY, box.min.z}, {box.max.x, eyeY, box.max.z}, eye,
                               thickness);
            render::drawLine3D(cam, {box.max.x, eyeY, box.max.z}, {box.min.x, eyeY, box.max.z}, eye,
                               thickness);
            render::drawLine3D(cam, {box.min.x, eyeY, box.max.z}, {box.min.x, eyeY, box.min.z}, eye,
                               thickness);
        }

        if (m_lookLine->value) {
            const Vec3 eyePos{centreX, eyeY, centreZ};
            const Vec3 direction = Vec3::fromAngles(player->rot());
            const Vec3 end = eyePos + direction * m_lookLength->value;
            render::drawLine3D(cam, eyePos, end, m_lookColour->value, thickness);
        }
    }
}

}
