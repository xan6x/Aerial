#include "Hooks/InputHooks.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

#include "GUI/ClickGui.h"
#include "Hooks/HookRegistry.h"
#include "GUI/Theme.h"
#include "Input/InputManager.h"
#include "SDK/ClientInstance.h"
#include "SDK/Context.h"
#include "SDK/Offsets.h"
#include "Utils/Hook.h"
#include "Utils/Logger.h"
#include "Utils/Memory.h"
#include "Utils/Platform.h"

namespace aerial::hooks {
namespace {

namespace func = offsets::func;

using Clock = std::chrono::steady_clock;

Detour<void(__fastcall*)(void*, void*)> g_moveInputTick;
Detour<void(__fastcall*)(void*, void*)> g_processEvents;
Detour<void(__fastcall*)(void*, void*, char, void*)> g_handleButtonEvent;
Detour<void(__fastcall*)(void*, char, char, short, short, short, short, char)> g_mouseFeed;
Detour<void(__fastcall*)(void*)> g_tickBuildAction;
Detour<void(__fastcall*)(void*)> g_grabMouse;
Detour<void(__fastcall*)(void*)> g_releaseMouse;
Detour<void(__fastcall*)(void*)> g_containerTick;
Detour<uintptr_t(__fastcall*)(void*)> g_hudTick;
Detour<void(__fastcall*)(void*, const std::string*, int)> g_slotHovered;
Detour<int(__fastcall*)(void*, void*)> g_collectionIndex;

std::string g_hoveredCollection;
int g_hoveredSlot = -1;

void* g_containerController = nullptr;
std::atomic<Clock::time_point> g_containerTickAt{};

std::atomic<bool> g_freshSelect{false};

void* g_moveInputHandler = nullptr;

void* g_deferredGrab = nullptr;

thread_local bool t_ourCursorCall = false;

struct OurCall {
    OurCall() { t_ourCursorCall = true; }
    ~OurCall() { t_ourCursorCall = false; }
};

void clearMovementState(void* handler) {
    if (!handler || !memory::isReadable(handler, 0x80))
        return;

    namespace field = offsets::field::moveInput;
    auto* bytes = static_cast<uint8_t*>(handler);

    bytes[field::flagA] = 0;
    *reinterpret_cast<uint16_t*>(bytes + field::direction) = 0;
    bytes[field::flagB] = 0;
    *reinterpret_cast<uint32_t*>(bytes + field::state) = 0;

    *reinterpret_cast<float*>(bytes + field::amountX) = 0.0f;
    *reinterpret_cast<float*>(bytes + field::amountY) = 0.0f;
}

void __fastcall onMoveInputTick(void* self, void* a2) {
    g_moveInputHandler = self;

    g_moveInputTick.call(self, a2);

    if (gui::ClickGui::get().isOpen())
        clearMovementState(self);
}

void __fastcall onProcessEvents(void* self, void* events) {
    if (gui::ClickGui::get().isOpen())
        return;
    g_processEvents.call(self, events);
}

void __fastcall onHandleButtonEvent(void* self, void* event, char state, void* a4) {
    // event[2] is 1 on press, 0 on release. Releases must pass through or a
    // button held when the menu opened stays held for the game.
    if (gui::ClickGui::get().isOpen() && memory::isReadable(event, 3) &&
        static_cast<const uint8_t*>(event)[2] == 1)
        return;

    g_handleButtonEvent.call(self, event, state, a4);
}

constexpr char kWheelButton = 4;

void __fastcall onMouseFeed(void* self, char button, char state, short x, short y, short dx,
                            short dy, char a8) {
    if (button == kWheelButton) {
        input::InputManager::get().feedWheel(state);

        if (gui::ClickGui::get().isOpen())
            return;
    }

    g_mouseFeed.call(self, button, state, x, y, dx, dy, a8);
}

void __fastcall onTickBuildAction(void* self) {
    if (gui::ClickGui::get().isOpen())
        return;
    g_tickBuildAction.call(self);
}

void __fastcall onContainerTick(void* self) {
    g_containerController = self;
    g_containerTickAt.store(Clock::now(), std::memory_order_relaxed);
    g_containerTick.call(self);
}

uintptr_t __fastcall onHudTick(void* self) {
    namespace hud = offsets::field::hudScreenController;
    if (g_freshSelect.exchange(false, std::memory_order_relaxed) && self &&
        memory::isReadable(self, hud::freshSelectFlag + sizeof(int32_t)))
        *reinterpret_cast<int32_t*>(static_cast<uint8_t*>(self) + hud::freshSelectFlag) = 0;

    return g_hudTick.call(self);
}

void __fastcall onSlotHovered(void* self, const std::string* collection, int slot) {
    if (collection && memory::isReadable(collection, sizeof(std::string)) &&
        collection->size() < 64) {
        g_hoveredCollection = *collection;
        g_hoveredSlot = slot;
    }
    g_slotHovered.call(self, collection, slot);
}

int __fastcall onCollectionIndex(void* self, void* bag) {
    const int index = g_collectionIndex.call(self, bag);
    if (index < 0 || !bag || !memory::isReadable(bag, 0x18))
        return index;

    using Find = void*(__fastcall*)(void*, const char*);
    auto find = reinterpret_cast<Find>(memory::rva(func::Json_Value_find));

    void* value = find(static_cast<uint8_t*>(bag) + 8, "#collection_name");
    if (!value || !memory::isReadable(value, 9))
        return index;
    if (*(static_cast<const uint8_t*>(value) + 8) != 4)
        return index;

    const char* text = *reinterpret_cast<const char* const*>(value);
    if (!text || !memory::isReadable(const_cast<char*>(text), 1))
        return index;

    g_hoveredCollection = text;
    g_hoveredSlot = index;
    return index;
}

void __fastcall onGrabMouse(void* self) {
    const bool focused = platform::gameFocused();
    LOG_DEBUG("Input", "grabMouse from {}, focused={}", t_ourCursorCall ? "the client" : "the game",
              focused);

    if (!t_ourCursorCall && gui::ClickGui::get().isOpen()) {
        g_deferredGrab = self;
        return;
    }

    if (!focused && !t_ourCursorCall) {
        g_deferredGrab = self;
        return;
    }

    g_deferredGrab = nullptr;
    g_grabMouse.call(self);
}

void __fastcall onReleaseMouse(void* self) {
    LOG_DEBUG("Input", "releaseMouse from {}, focused={}",
              t_ourCursorCall ? "the client" : "the game", platform::gameFocused());
    g_deferredGrab = nullptr;
    g_releaseMouse.call(self);
}

bool install() {
    g_moveInputTick.attach("MoveInputHandler::tick", memory::rva(func::MoveInputHandler_tick),
                           &onMoveInputTick);
    g_processEvents.attach("ScreenView::_processEvents",
                           memory::rva(func::ScreenView_processEvents), &onProcessEvents);
    g_handleButtonEvent.attach("InputHandler::_handleButtonEvent",
                               memory::rva(func::InputHandler_handleButtonEvent),
                               &onHandleButtonEvent);
    g_mouseFeed.attach("MouseDevice::feed", memory::rva(func::MouseDevice_feed), &onMouseFeed);
    g_tickBuildAction.attach("ClientInstance::tickBuildAction",
                             memory::rva(func::ClientInstance_tickBuildAction), &onTickBuildAction);
    g_grabMouse.attach("ClientInstance::grabMouse", memory::rva(func::ClientInstance_grabMouse),
                       &onGrabMouse);
    g_releaseMouse.attach("MinecraftGame::releaseMouse",
                          memory::rva(func::MinecraftGame_releaseMouse), &onReleaseMouse);
    g_containerTick.attach("ContainerScreenController::tick",
                           memory::rva(func::ContainerScreenController_tick), &onContainerTick);
    g_hudTick.attach("HudScreenController::tick",
                     memory::rva(func::HudScreenController_tick), &onHudTick);
    g_slotHovered.attach("ContainerScreenController::_onContainerSlotPressed",
                         memory::rva(func::ContainerScreenController_onSlotHovered), &onSlotHovered);
    g_collectionIndex.attach("ContainerScreenController::getCollectionIndex",
                             memory::rva(func::ContainerScreenController_getCollectionIndex),
                             &onCollectionIndex);
    return true;
}

const Installer g_installer{"Input", &install};

}

void clearMovementInput() { clearMovementState(g_moveInputHandler); }

void* containerController() {
    constexpr auto kFreshFor = std::chrono::milliseconds(500);
    if (Clock::now() - g_containerTickAt.load(std::memory_order_relaxed) > kFreshFor)
        return nullptr;
    return g_containerController;
}

void requestFreshSelect() { g_freshSelect.store(true, std::memory_order_relaxed); }

int hoveredSlot(std::string& collection) {
    collection = g_hoveredCollection;
    return g_hoveredSlot;
}

void healMouseGrab() {
    static float heldSince = 0.0f;
    constexpr float kReleaseSeconds = 0.35f;

    auto* client = sdk::Context::get().client;
    if (!client || platform::gameFocused() || !client->mouseGrabbed()) {
        heldSince = 0.0f;
        return;
    }

    const float now = gui::clockSeconds();
    if (heldSince == 0.0f) {
        heldSince = now;
        return;
    }
    if (now - heldSince < kReleaseSeconds)
        return;

    heldSince = 0.0f;

    const OurCall ours;
    LOG_WARN("Input", "the cursor was still held while the game was in the background; letting it go");
    client->releaseMouse();
}

void holdMouseReleased() {
    if (!gui::ClickGui::get().isOpen())
        return;

    auto* client = sdk::Context::get().client;
    if (!client || !client->mouseGrabbed())
        return;

    const OurCall ours;
    LOG_DEBUG("Input", "the game grabbed the cursor while the menu was open; releasing it again");
    client->releaseMouse();
}

void replayDeferredGrab() {
    if (!g_deferredGrab || !platform::gameFocused() || gui::ClickGui::get().isOpen())
        return;

    void* target = g_deferredGrab;
    g_deferredGrab = nullptr;

    LOG_DEBUG("Input", "replaying the grab the game asked for while it was in the background");
    g_grabMouse.call(target);
}

}
