#include "Module/Modules/QuickSlots.h"

#include <atomic>
#include <cstdint>

#include "Event/Events.h"
#include "GUI/ClickGui.h"
#include "Hooks/HookRegistry.h"
#include "Hooks/InputHooks.h"
#include "SDK/ClientInstance.h"
#include "SDK/Context.h"
#include "SDK/Entity.h"
#include "SDK/Offsets.h"
#include "Utils/Hook.h"
#include "Utils/Memory.h"

namespace aerial::modules {
namespace {

constexpr int kSlots = 9;

using SelectSlotFn = void(__fastcall*)(void*, int, uint8_t);
Detour<SelectSlotFn> g_selectSlot;

std::atomic<uint8_t> g_hotbarContainer{0};
std::atomic<bool> g_ourCall{false};
std::atomic<bool> g_haveContainer{false};

void __fastcall onSelectSlot(void* proxy, int slot, uint8_t container) {
    if (!g_ourCall.load(std::memory_order_relaxed)) {
        g_hotbarContainer.store(container, std::memory_order_relaxed);
        g_haveContainer.store(true, std::memory_order_relaxed);
    }
    g_selectSlot.call(proxy, slot, container);
}

bool install() {
    g_selectSlot.attach("PlayerInventoryProxy::selectSlot",
                        memory::rva(offsets::func::PlayerInventoryProxy_selectSlot), &onSelectSlot);
    return true;
}

const hooks::Installer g_installer{"QuickSlots", &install};

void* inventoryProxy(void* player) {
    namespace field = offsets::field::localPlayer;
    if (!player || !memory::isReadable(player, field::inventoryProxy + sizeof(void*)))
        return nullptr;
    return *reinterpret_cast<void**>(static_cast<uint8_t*>(player) + field::inventoryProxy);
}

}

QuickSlots::QuickSlots()
    : Module("QuickSlots", "Hotbar slots switch the moment the key goes down", Category::Input) {
    listen<KeyEvent>(&QuickSlots::onKey);
}

void QuickSlots::onKey(KeyEvent& event) {
    if (!event.down || event.repeat)
        return;

    const int slot = event.key - '1';
    if (slot < 0 || slot >= kSlots)
        return;

    if (gui::ClickGui::get().isOpen())
        return;

    auto& context = sdk::Context::get();
    if (!context.inGame() || !context.client || !context.client->mouseGrabbed())
        return;

    void* proxy = inventoryProxy(context.localPlayer);
    namespace field = offsets::field::playerInventoryProxy;
    if (!proxy || !memory::isReadable(proxy, field::container + 1))
        return;

    const uint8_t container =
        g_haveContainer.load(std::memory_order_relaxed)
            ? g_hotbarContainer.load(std::memory_order_relaxed)
            : *reinterpret_cast<const uint8_t*>(static_cast<uint8_t*>(proxy) + field::container);

    hooks::freshSelectNow();

    g_ourCall.store(true, std::memory_order_relaxed);
    g_selectSlot.call(proxy, slot, container);
    g_ourCall.store(false, std::memory_order_relaxed);
}

}
