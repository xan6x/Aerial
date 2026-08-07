#include "Hooks/InputHooks.h"

#include <cstdint>

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

Detour<void(__fastcall*)(void*, void*)> g_moveInputTick;
Detour<void(__fastcall*)(void*, void*)> g_processEvents;
Detour<void(__fastcall*)(void*, void*, char, void*)> g_handleButtonEvent;
Detour<void(__fastcall*)(void*, char, char, short, short, short, short, char)> g_mouseFeed;
Detour<void(__fastcall*)(void*)> g_tickBuildAction;
Detour<void(__fastcall*)(void*)> g_grabMouse;
Detour<void(__fastcall*)(void*)> g_releaseMouse;

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

void __fastcall onGrabMouse(void* self) {
    const bool focused = platform::gameFocused();
    LOG_DEBUG("Input", "grabMouse from {}, focused={}", t_ourCursorCall ? "the client" : "the game",
              focused);

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
    return true;
}

const Installer g_installer{"Input", &install};

}

void clearMovementInput() { clearMovementState(g_moveInputHandler); }

void healMouseGrab() {
    static float wrongSince = 0.0f;
    constexpr float kGrabSeconds = 1.0f;
    constexpr float kReleaseSeconds = 0.35f;

    auto& context = sdk::Context::get();
    auto* client = context.client;
    if (!client) {
        wrongSince = 0.0f;
        return;
    }

    const bool focused = platform::gameFocused();
    const bool grabbed = client->mouseGrabbed();

    bool wrong = false;
    float patience = 0.0f;

    if (!focused && grabbed) {
        wrong = true;
        patience = kReleaseSeconds;
    } else if (focused && !grabbed && context.inGame() && !gui::ClickGui::get().isOpen()) {
        using Steals = bool(__fastcall*)(void*);
        auto steals = reinterpret_cast<Steals>(
            memory::rva(func::ClientInstance_currentScreenShouldStealMouse));
        wrong = !steals(client);
        patience = kGrabSeconds;
    }

    if (!wrong) {
        wrongSince = 0.0f;
        return;
    }

    const float now = gui::clockSeconds();
    if (wrongSince == 0.0f) {
        wrongSince = now;
        return;
    }
    if (now - wrongSince < patience)
        return;

    wrongSince = 0.0f;

    const OurCall ours;
    if (grabbed) {
        LOG_WARN("Input", "the cursor was still held while the game was in the background; "
                          "letting it go");
        client->releaseMouse();
    } else {
        LOG_WARN("Input", "the cursor was left released in a world with no screen up; taking it back");
        client->grabMouse();
    }
}

void replayDeferredGrab() {
    if (!g_deferredGrab || !platform::gameFocused())
        return;

    void* target = g_deferredGrab;
    g_deferredGrab = nullptr;

    LOG_DEBUG("Input", "replaying the grab the game asked for while it was in the background");
    g_grabMouse.call(target);
}

}
