#include "Module/Modules/JavaHotkeys.h"

#include <cstdint>
#include <string>

#include "Event/Events.h"
#include "GUI/ClickGui.h"
#include "Hooks/InputHooks.h"
#include "SDK/Offsets.h"
#include "Utils/Memory.h"

namespace aerial::modules {
namespace {

namespace field = offsets::field::containerScreenController;

constexpr int kHotbarSlots = 9;
constexpr int kStepDelay = 1;

using TakePlace = void(__fastcall*)(void*, uint16_t, const std::string*, int);

static_assert(sizeof(std::string) == 0x20, "the game expects MSVC's std::string layout");

bool swappable(const std::string& collection) {
    if (collection.empty())
        return false;
    return collection.find("search") == std::string::npos;
}

void takePlaceAt(void* controller, uint16_t button, const std::string& collection, int slot) {
    auto takePlace = reinterpret_cast<TakePlace>(
        memory::rva(offsets::func::ContainerScreenController_handleTakePlace));
    takePlace(controller, button, &collection, slot);
}

}

JavaHotkeys::JavaHotkeys()
    : Module("JavaHotkeys", "Number keys swap the hovered item with that hotbar slot",
             Category::Input) {
    listen<KeyEvent>(&JavaHotkeys::onKey);
    listen<TickEvent>(&JavaHotkeys::onTick);
}

void JavaHotkeys::onKey(KeyEvent& event) {
    if (!event.down || event.repeat)
        return;

    const int target = event.key - '1';
    if (target < 0 || target >= kHotbarSlots)
        return;

    if (gui::ClickGui::get().isOpen())
        return;

    void* controller = hooks::containerController();
    if (!controller || !memory::isReadable(controller, field::hoveredSlot + sizeof(int)))
        return;

    std::string collection;
    const int hovered = hooks::hoveredSlot(collection);
    if (hovered < 0 || !swappable(collection))
        return;

    m_swap.active = true;
    m_swap.step = 0;
    m_swap.cooldown = 0;
    m_swap.hovered = hovered;
    m_swap.target = target;
    m_swap.button = *reinterpret_cast<const uint16_t*>(static_cast<uint8_t*>(controller) +
                                                       field::takePlaceButton);
    m_swap.collection = collection;

    event.cancel();
}

void JavaHotkeys::onTick(TickEvent&) {
    if (!m_swap.active)
        return;

    if (m_swap.cooldown > 0) {
        --m_swap.cooldown;
        return;
    }

    void* controller = hooks::containerController();
    if (!controller || !memory::isReadable(controller, field::hoveredSlot + sizeof(int))) {
        m_swap.active = false;
        return;
    }

    const std::string hotbar = "hotbar_items";

    switch (m_swap.step) {
    case 0:
        takePlaceAt(controller, m_swap.button, m_swap.collection, m_swap.hovered);
        break;
    case 1:
        takePlaceAt(controller, m_swap.button, hotbar, m_swap.target);
        break;
    case 2:
        takePlaceAt(controller, m_swap.button, m_swap.collection, m_swap.hovered);
        break;
    default:
        m_swap.active = false;
        return;
    }

    ++m_swap.step;
    m_swap.cooldown = kStepDelay;
    if (m_swap.step > 2)
        m_swap.active = false;
}

}
