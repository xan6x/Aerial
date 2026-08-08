#include <cstdint>
#include <string>

#include "Hooks/HookRegistry.h"
#include "SDK/Offsets.h"
#include "Utils/Hook.h"
#include "Utils/Memory.h"

namespace aerial::hooks {
namespace {

namespace func = offsets::func;

constexpr size_t kStringSize = 0x20;
constexpr size_t kStringLength = 0x10;
constexpr size_t kStringCapacity = 0x18;
constexpr size_t kShortStringCapacity = 0x10;

static_assert(sizeof(std::string) == kStringSize, "the game expects MSVC's std::string layout");

Detour<void(__fastcall*)(void*, const void*, void*)> g_updateIntellisense;

const uint8_t* stringData(const void* text, size_t& length) {
    length = 0;
    if (!memory::isReadable(text, kStringSize))
        return nullptr;

    const auto* bytes = static_cast<const uint8_t*>(text);
    const size_t size = *reinterpret_cast<const size_t*>(bytes + kStringLength);
    const size_t capacity = *reinterpret_cast<const size_t*>(bytes + kStringCapacity);
    if (size == 0 || size > capacity)
        return nullptr;

    const uint8_t* data = capacity < kShortStringCapacity
                              ? bytes
                              : *reinterpret_cast<const uint8_t* const*>(bytes);
    if (!memory::isReadable(data, size))
        return nullptr;

    length = size;
    return data;
}

bool nonAscii(const void* text) {
    size_t length = 0;
    const uint8_t* data = stringData(text, length);
    if (!data)
        return false;

    for (size_t i = 0; i < length; ++i) {
        if (data[i] >= 0x80)
            return true;
    }
    return false;
}

void __fastcall onUpdateIntellisense(void* self, const void* line, void* a3) {
    if (!nonAscii(line)) {
        g_updateIntellisense.call(self, line, a3);
        return;
    }

    static const std::string empty;
    g_updateIntellisense.call(self, &empty, a3);
}

bool install() {
    g_updateIntellisense.attach("IntellisenseHandler::updateIntellisense",
                                memory::rva(func::IntellisenseHandler_updateIntellisense),
                                &onUpdateIntellisense);
    return true;
}

const Installer g_installer{"Chat", &install};

}
}
