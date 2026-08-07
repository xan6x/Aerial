#include "Module/Modules/QuickSlots.h"

#include <cstdint>

#include "Event/Events.h"
#include "GUI/ClickGui.h"
#include "SDK/ClientInstance.h"
#include "SDK/Context.h"
#include "SDK/Entity.h"
#include "SDK/Offsets.h"
#include "Utils/Memory.h"

namespace aerial::modules {
namespace {

constexpr int kSlots = 9;

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
        *reinterpret_cast<const uint8_t*>(static_cast<uint8_t*>(proxy) + field::container);

    using SelectSlot = void(__fastcall*)(void*, int, uint8_t);
    auto select = reinterpret_cast<SelectSlot>(
        memory::rva(offsets::func::PlayerInventoryProxy_selectSlot));
    select(proxy, slot, container);
}

}
