#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aerial {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kDeg2Rad = kPi / 180.0f;
inline constexpr float kRad2Deg = 180.0f / kPi;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float x, float y) : x(x), y(y) {}

    constexpr Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
    constexpr Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    constexpr Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    constexpr bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }

    float length() const { return std::sqrt(x * x + y * y); }
    Vec2 normalised() const { const float l = length(); return l > 0.0f ? *this / l : Vec2{}; }

    Vec2 normaliseAngles() const {
        Vec2 result = *this;
        while (result.x > 180.0f) result.x -= 360.0f;
        while (result.x < -180.0f) result.x += 360.0f;
        while (result.y > 180.0f) result.y -= 360.0f;
        while (result.y < -180.0f) result.y += 360.0f;
        return result;
    }
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    constexpr Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    constexpr Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    constexpr bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }

    float lengthSquared() const { return x * x + y * y + z * z; }
    float length() const { return std::sqrt(lengthSquared()); }
    float distance(const Vec3& o) const { return (*this - o).length(); }
    float distanceSquared(const Vec3& o) const { return (*this - o).lengthSquared(); }
    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }

    Vec3 normalised() const { const float l = length(); return l > 0.0f ? *this / l : Vec3{}; }

    Vec2 anglesTo(const Vec3& target) const {
        const Vec3 delta = target - *this;
        const float horizontal = std::sqrt(delta.x * delta.x + delta.z * delta.z);
        const float pitch = -std::atan2(delta.y, horizontal) * kRad2Deg;
        const float yaw = std::atan2(delta.z, delta.x) * kRad2Deg - 90.0f;
        return Vec2{pitch, yaw}.normaliseAngles();
    }

    static Vec3 fromAngles(const Vec2& angles) {
        const float pitch = angles.x * kDeg2Rad;
        const float yaw = angles.y * kDeg2Rad;
        const float cosPitch = std::cos(pitch);
        return {-cosPitch * std::sin(yaw), -std::sin(pitch), cosPitch * std::cos(yaw)};
    }
};

struct AABB {
    Vec3 min;
    Vec3 max;

    constexpr AABB() = default;
    constexpr AABB(const Vec3& min, const Vec3& max) : min(min), max(max) {}

    Vec3 centre() const { return (min + max) * 0.5f; }
    Vec3 size() const { return max - min; }

    bool contains(const Vec3& p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
    }

    Vec3 closestPoint(const Vec3& p) const {
        return {std::clamp(p.x, min.x, max.x), std::clamp(p.y, min.y, max.y), std::clamp(p.z, min.z, max.z)};
    }
};

struct BlockPos {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;

    constexpr BlockPos() = default;
    constexpr BlockPos(int32_t x, int32_t y, int32_t z) : x(x), y(y), z(z) {}
    explicit BlockPos(const Vec3& v)
        : x(static_cast<int32_t>(std::floor(v.x))),
          y(static_cast<int32_t>(std::floor(v.y))),
          z(static_cast<int32_t>(std::floor(v.z))) {}

    constexpr bool operator==(const BlockPos& o) const { return x == o.x && y == o.y && z == o.z; }
    Vec3 centre() const { return {x + 0.5f, y + 0.5f, z + 0.5f}; }
};

struct Colour {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;

    constexpr Colour() = default;
    constexpr Colour(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}

    static constexpr Colour rgb(uint32_t hex, float alpha = 1.0f) {
        return {static_cast<float>((hex >> 16) & 0xFF) / 255.0f,
                static_cast<float>((hex >> 8) & 0xFF) / 255.0f,
                static_cast<float>(hex & 0xFF) / 255.0f, alpha};
    }

    static Colour hsv(float h, float s, float v, float alpha = 1.0f) {
        h = std::fmod(h, 360.0f);
        if (h < 0.0f) h += 360.0f;
        const float c = v * s;
        const float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
        const float m = v - c;
        float r = 0.0f, g = 0.0f, b = 0.0f;
        if (h < 60.0f)       { r = c; g = x; }
        else if (h < 120.0f) { r = x; g = c; }
        else if (h < 180.0f) { g = c; b = x; }
        else if (h < 240.0f) { g = x; b = c; }
        else if (h < 300.0f) { r = x; b = c; }
        else                 { r = c; b = x; }
        return {r + m, g + m, b + m, alpha};
    }

    constexpr Colour withAlpha(float alpha) const { return {r, g, b, alpha}; }

    Colour lerp(const Colour& to, float t) const {
        return {r + (to.r - r) * t, g + (to.g - g) * t, b + (to.b - b) * t, a + (to.a - a) * t};
    }
};

struct Rect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    constexpr Rect() = default;
    constexpr Rect(float left, float top, float right, float bottom)
        : left(left), top(top), right(right), bottom(bottom) {}

    static constexpr Rect xywh(float x, float y, float w, float h) { return {x, y, x + w, y + h}; }

    constexpr float width() const { return right - left; }
    constexpr float height() const { return bottom - top; }

    constexpr bool contains(const Vec2& p) const {
        return p.x >= left && p.x <= right && p.y >= top && p.y <= bottom;
    }

    constexpr Rect inset(float amount) const {
        return {left + amount, top + amount, right - amount, bottom - amount};
    }

    constexpr Rect offset(const Vec2& d) const {
        return {left + d.x, top + d.y, right + d.x, bottom + d.y};
    }
};

template <typename T>
constexpr T lerp(T from, T to, float t) { return static_cast<T>(from + (to - from) * t); }

}
