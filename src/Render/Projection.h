#pragma once

#include "Utils/Math.h"

namespace aerial::render {

struct Camera {
    Vec3 pos;
    Vec3 forward;
    Vec3 right;
    Vec3 up;
    float tanHalfFov = 1.0f;
    float aspect = 1.0f;
    Vec2 screen;

    bool build(const Vec2& screen);

    bool projectSegment(Vec3 a, Vec3 b, Vec2& sa, Vec2& sb) const;

    bool isVisible(const Vec3& world, float ndcMargin = 1.0f) const;
};

void drawLine3D(const Camera& cam, const Vec3& a, const Vec3& b, const Colour& colour,
                float thickness = 1.5f);

void drawBox3D(const Camera& cam, const AABB& box, const Colour& colour, float thickness = 1.5f);

}
