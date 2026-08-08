#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "Utils/Math.h"
#include "Utils/Memory.h"

namespace aerial::sdk {

static_assert(sizeof(std::string) == 0x20, "unexpected std::string layout — rebuild with MSVC x64");

inline std::string_view gameString(const void* text) {
    constexpr size_t kLength = 0x10;
    constexpr size_t kCapacity = 0x18;
    constexpr size_t kShort = 0x10;

    if (!memory::isReadable(text, sizeof(std::string)))
        return {};

    const auto* bytes = static_cast<const uint8_t*>(text);
    const size_t size = *reinterpret_cast<const size_t*>(bytes + kLength);
    const size_t capacity = *reinterpret_cast<const size_t*>(bytes + kCapacity);
    if (size == 0 || size > capacity)
        return {};

    const char* data = capacity < kShort ? reinterpret_cast<const char*>(bytes)
                                         : *reinterpret_cast<const char* const*>(bytes);
    if (!memory::isReadable(data, size))
        return {};

    return {data, size};
}

using Color = Colour;
static_assert(sizeof(Color) == 16, "mce::Color must be four floats");

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

template <typename Ret, typename... Args>
inline Ret callVirtual(const void* self, int index, Args... args) {
    using Fn = Ret(__fastcall*)(const void*, Args...);
    Fn* const vtable = *reinterpret_cast<Fn* const*>(self);
    return vtable[index](self, args...);
}

template <typename T>
inline T& fieldAt(void* self, ptrdiff_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(self) + offset);
}

template <typename T>
inline const T& fieldAt(const void* self, ptrdiff_t offset) {
    return *reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(self) + offset);
}

}
