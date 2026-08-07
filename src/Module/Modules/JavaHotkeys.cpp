#include "Module/Modules/JavaHotkeys.h"

#include <cstdint>
#include <string>

#include "Event/Events.h"
#include "GUI/ClickGui.h"
#include "Hooks/InputHooks.h"
#include "SDK/Offsets.h"
#include "Utils/Logger.h"
#include "Utils/Memory.h"

namespace aerial::modules {
namespace {

namespace field = offsets::field::containerScreenController;

constexpr int kHotbarSlots = 9;

using TakePlace = void(__fastcall*)(void*, uint16_t, const std::string*, int);

static_assert(sizeof(std::string) == 0x20, "the game expects MSVC's std::string layout");

bool swappable(const std::string& collection) {
    if (collection.empty())
        return false;
    return collection.find("search") == std::string::npos;
}

}

JavaHotkeys::JavaHotkeys()
    : Module("JavaHotkeys", "Number keys swap the hovered item with that hotbar slot",
             Category::Input) {
    listen<KeyEvent>(&JavaHotkeys::onKey);
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
    if (!controller || !memory::isReadable(controller, field::hoveredSlot + sizeof(int))) {
        LOG_DEBUG("JavaHotkeys", "no container controller yet");
        return;
    }

    std::string collection;
    const int hovered = hooks::hoveredSlot(collection);

    LOG_DEBUG("JavaHotkeys", "hovered slot {} collection '{}'", hovered, collection);

    if (hovered < 0 || !swappable(collection))
        return;

    auto* bytes = static_cast<uint8_t*>(controller);

    const uint16_t button = *reinterpret_cast<const uint16_t*>(bytes + field::takePlaceButton);
    auto takePlace = reinterpret_cast<TakePlace>(
        memory::rva(offsets::func::ContainerScreenController_handleTakePlace));

    const std::string hotbar = "hotbar_items";

    takePlace(controller, button, &collection, hovered);
    takePlace(controller, button, &hotbar, target);
    takePlace(controller, button, &collection, hovered);

    event.cancel();
}

}
