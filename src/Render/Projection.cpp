#include "Render/Projection.h"

#include <cmath>

#include "Hooks/FovHooks.h"
#include "Render/DrawUtils.h"
#include "SDK/Context.h"
#include "SDK/Entity.h"
#include "SDK/Offsets.h"
#include "Utils/Memory.h"

namespace aerial::render {
namespace {

namespace func = offsets::func;

constexpr float kNearPlane = 0.05f;

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

bool readCameraPos(Vec3& out) {
    void* address = reinterpret_cast<void*>(memory::rva(func::g_cameraPos));
    if (!memory::isReadable(address, sizeof(float) * 3))
        return false;
    const float* p = static_cast<const float*>(address);
    out = {p[0], p[1], p[2]};
    return true;
}

Vec2 lerpAngles(const Vec2& oldRot, const Vec2& rot, float t) {
    float dPitch = rot.x - oldRot.x;
    float dYaw = rot.y - oldRot.y;
    while (dYaw > 180.0f) dYaw -= 360.0f;
    while (dYaw < -180.0f) dYaw += 360.0f;
    return {oldRot.x + dPitch * t, oldRot.y + dYaw * t};
}

Vec2 project(const Camera& cam, const Vec3& world) {
    const Vec3 d = world - cam.pos;
    const float vz = d.dot(cam.forward);
    const float vx = d.dot(cam.right);
    const float vy = d.dot(cam.up);
    const float ndcX = vx / (vz * cam.tanHalfFov * cam.aspect);
    const float ndcY = vy / (vz * cam.tanHalfFov);
    return {(ndcX + 1.0f) * 0.5f * cam.screen.x, (1.0f - ndcY) * 0.5f * cam.screen.y};
}

float viewDepth(const Camera& cam, const Vec3& world) {
    return (world - cam.pos).dot(cam.forward);
}

bool offScreen(const Vec2& a, const Vec2& b, const Vec2& screen) {
    constexpr float margin = 8.0f;
    if (a.x < -margin && b.x < -margin) return true;
    if (a.y < -margin && b.y < -margin) return true;
    if (a.x > screen.x + margin && b.x > screen.x + margin) return true;
    if (a.y > screen.y + margin && b.y > screen.y + margin) return true;
    return false;
}

}

bool Camera::build(const Vec2& s) {
    if (s.x <= 0.0f || s.y <= 0.0f)
        return false;
    if (!readCameraPos(pos))
        return false;

    auto* player = sdk::Context::get().localPlayer;
    if (!player)
        return false;

    const Vec2 rot = lerpAngles(player->rotOld(), player->rot(), hooks::currentPartialTicks());

    forward = Vec3::fromAngles(rot);
    right = cross(forward, Vec3{0.0f, 1.0f, 0.0f}).normalised();
    up = cross(right, forward).normalised();

    tanHalfFov = std::tan(hooks::currentFov() * kDeg2Rad * 0.5f);
    screen = s;
    aspect = s.x / s.y;
    return true;
}

bool Camera::projectSegment(Vec3 a, Vec3 b, Vec2& sa, Vec2& sb) const {
    const float za = viewDepth(*this, a);
    const float zb = viewDepth(*this, b);
    if (za < kNearPlane && zb < kNearPlane)
        return false;
    if (za < kNearPlane)
        a = a + (b - a) * ((kNearPlane - za) / (zb - za));
    else if (zb < kNearPlane)
        b = b + (a - b) * ((kNearPlane - zb) / (za - zb));

    sa = project(*this, a);
    sb = project(*this, b);
    return true;
}

void drawLine3D(const Camera& cam, const Vec3& a, const Vec3& b, const Colour& colour,
                float thickness) {
    Vec2 sa, sb;
    if (!cam.projectSegment(a, b, sa, sb))
        return;
    if (offScreen(sa, sb, cam.screen))
        return;

    Vec2 dir = sb - sa;
    const float len = dir.length();
    if (len < 0.01f)
        return;
    dir = dir / len;
    const Vec2 normal = Vec2{-dir.y, dir.x} * (thickness * 0.5f);

    const Vec2 quad[4] = {sa + normal, sb + normal, sb - normal, sa - normal};
    DrawUtils::polygon(quad, 4, colour);
}

void drawBox3D(const Camera& cam, const AABB& box, const Colour& colour, float thickness) {
    const Vec3 c[8] = {
        {box.min.x, box.min.y, box.min.z}, {box.max.x, box.min.y, box.min.z},
        {box.max.x, box.min.y, box.max.z}, {box.min.x, box.min.y, box.max.z},
        {box.min.x, box.max.y, box.min.z}, {box.max.x, box.max.y, box.min.z},
        {box.max.x, box.max.y, box.max.z}, {box.min.x, box.max.y, box.max.z},
    };

    static constexpr int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                         {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};

    for (const auto& edge : edges)
        drawLine3D(cam, c[edge[0]], c[edge[1]], colour, thickness);
}

}
