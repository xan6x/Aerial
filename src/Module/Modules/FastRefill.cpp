#include "Module/Modules/FastRefill.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>

#include "Event/Events.h"
#include "Hooks/HookRegistry.h"
#include "SDK/Offsets.h"
#include "Utils/Hook.h"
#include "Utils/Memory.h"

namespace aerial::modules {
namespace {

namespace func = offsets::func;
namespace field = offsets::field::containerScreenController;

constexpr ptrdiff_t kAutoPlaceMarker = 0x48C;

Detour<void*(__fastcall*)(void*, void*, uint32_t, uint32_t)> g_onSlotSelected;

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_requireShift{true};

bool shiftDown() { return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0; }

void shiftMove(void* controller, uint32_t slot) {
    using Fn = char(__fastcall*)(void*, uint32_t, void*, uint32_t);
    const uint16_t marker =
        *reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(controller) + kAutoPlaceMarker);
    void* collection = static_cast<uint8_t*>(controller) + field::hoveredCollection;
    reinterpret_cast<Fn>(memory::rva(func::ContainerScreenController_handleTakePlace))(
        controller, marker, collection, slot);
}

void* __fastcall onSlotSelected(void* self, void* arg, uint32_t slot, uint32_t flags) {
    void* result = g_onSlotSelected.call(self, arg, slot, flags);

    if (g_enabled.load(std::memory_order_relaxed) && self && slot != 0xFFFFFFFF &&
        (!g_requireShift.load(std::memory_order_relaxed) || shiftDown()) &&
        memory::isReadable(self, field::hoveredSlot + sizeof(int))) {
        shiftMove(self, slot);
    }

    return result;
}

bool install() {
    g_onSlotSelected.attach("ContainerScreenController::onSlotSelected",
                            memory::rva(func::ContainerScreenController_onSlotSelected),
                            &onSlotSelected);
    return true;
}

const hooks::Installer g_installer{"FastRefill", &install};

}

FastRefill::FastRefill()
    : Module("FastRefill", "Shift-drag over slots to transfer every item you pass across",
             Category::Input) {
    m_requireShift = addBool("Only with Shift", "Fast-move only while Shift is held", true);

    listen<Render2DEvent>(&FastRefill::onRender);
}

void FastRefill::onRender(Render2DEvent&) {
    g_requireShift.store(m_requireShift->value, std::memory_order_relaxed);
}

void FastRefill::onEnable() {
    g_requireShift.store(m_requireShift->value, std::memory_order_relaxed);
    g_enabled.store(true, std::memory_order_relaxed);
}

void FastRefill::onDisable() { g_enabled.store(false, std::memory_order_relaxed); }

}
