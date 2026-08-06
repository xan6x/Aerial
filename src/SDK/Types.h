#pragma once

#include <cstdint>
#include <string>

#include "Utils/Math.h"

namespace aerial::sdk {

// The game is MSVC-built, so its std::string is layout-identical to ours:
// 16-byte SSO buffer, size at +0x10, capacity at +0x18. Font::getLineLength
// reads exactly those offsets, which confirms it for this build.
static_assert(sizeof(std::string) == 0x20, "unexpected std::string layout — rebuild with MSVC x64");

// mce::Color — four floats, RGBA. Aerial's Colour has the same layout, so the
// two are interchangeable at the ABI boundary.
using Color = Colour;
static_assert(sizeof(Color) == 16, "mce::Color must be four floats");

// RectangleArea as the UI code stores it: x0, x1, y0, y1 (note the ordering —
// MinecraftUIRenderContext::fillRectangle reads +0x0/+0x8 as x0/y0).
struct RectangleArea {
    float x0 = 0.0f;
    float x1 = 0.0f;
    float y0 = 0.0f;
    float y1 = 0.0f;

    RectangleArea() = default;
    RectangleArea(float x0, float y0, float x1, float y1) : x0(x0), x1(x1), y0(y0), y1(y1) {}
    explicit RectangleArea(const Rect& r) : x0(r.left), x1(r.right), y0(r.top), y1(r.bottom) {}
};
static_assert(sizeof(RectangleArea) == 16, "RectangleArea must be four floats");

// Opaque game types we only ever hold pointers to.
class ClientInstance;
class Entity;
class GameMode;
class Level;
class LocalPlayer;
class MinecraftGame;
class Mob;
class Packet;
class Player;
class ScreenContext;
class ScreenRenderer;

// Calls a virtual by index on an object whose layout we do not model.
// On x64 MSVC the this-call convention is __fastcall with `this` in RCX.
template <typename Ret, typename... Args>
inline Ret callVirtual(const void* self, int index, Args... args) {
    using Fn = Ret(__fastcall*)(const void*, Args...);
    Fn* const vtable = *reinterpret_cast<Fn* const*>(self);
    return vtable[index](self, args...);
}

// Typed access to a field at a byte offset from an opaque object.
template <typename T>
inline T& fieldAt(void* self, ptrdiff_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(self) + offset);
}

template <typename T>
inline const T& fieldAt(const void* self, ptrdiff_t offset) {
    return *reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(self) + offset);
}

} // namespace aerial::sdk
