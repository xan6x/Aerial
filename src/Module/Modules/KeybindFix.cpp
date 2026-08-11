#include "Module/Modules/KeybindFix.h"

#include <Windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "Event/Events.h"
#include "Hooks/InputHooks.h"
#include "Input/InputManager.h"
#include "SDK/Context.h"
#include "Utils/Memory.h"

namespace aerial::modules {
namespace {

KeybindFix* g_instance = nullptr;

constexpr ptrdiff_t kRangeA = 0x40;
constexpr size_t kSizeA = 0x18;
constexpr ptrdiff_t kRangeB = 0x60;
constexpr size_t kSizeB = 0x14;
constexpr ptrdiff_t kEnd = kRangeB + static_cast<ptrdiff_t>(kSizeB);

bool held(int vk) { return input::InputManager::get().isDown(vk); }

bool isGuiScreen(const std::string& s) {
    return !s.empty() && s != "hud_screen" && s != "start_screen";
}

using XInputGetStateFn = DWORD(WINAPI*)(DWORD, void*);

XInputGetStateFn xinput() {
    static XInputGetStateFn fn = [] {
        const wchar_t* names[] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
        for (const auto* name : names) {
            if (HMODULE mod = LoadLibraryW(name)) {
                if (auto* p = GetProcAddress(mod, "XInputGetState"))
                    return reinterpret_cast<XInputGetStateFn>(p);
            }
        }
        return static_cast<XInputGetStateFn>(nullptr);
    }();
    return fn;
}

bool controllerMoving() {
    auto* getState = xinput();
    if (!getState)
        return false;

    uint8_t state[16];
    for (DWORD pad = 0; pad < 4; ++pad) {
        if (getState(pad, state) != ERROR_SUCCESS)
            continue;

        const uint16_t buttons = *reinterpret_cast<uint16_t*>(state + 4);
        const int16_t lx = *reinterpret_cast<int16_t*>(state + 8);
        const int16_t ly = *reinterpret_cast<int16_t*>(state + 10);
        const float mag = std::sqrt(static_cast<float>(lx) * lx + static_cast<float>(ly) * ly);
        constexpr float kDeadzone = 7849.0f;

        if (mag > kDeadzone || (buttons & 0x000F))
            return true;
    }
    return false;
}

bool stateMoving(void* handler) {
    auto* b = static_cast<uint8_t*>(handler);
    for (ptrdiff_t off = 0x40; off < 0x74; ++off) {
        if (off == 0x4C)
            continue;
        if (off >= 0x58 && off < 0x60)
            continue;
        if (b[off] != 0)
            return true;
    }
    return false;
}

}

KeybindFix::KeybindFix()
    : Module("Modern Handling",
             "Restores movement after closing a GUI screen while an input is still held",
             Category::Input) {
    m_fixInventory = addBool("Inventory", "Restore movement after inventory and container screens", true);
    m_fixPause = addBool("Pause", "Restore movement after the pause menu", true);
    m_fixChat = addBool("Chat", "Restore movement after the chat screen", true);
    m_fixAll = addBool("All Screens", "Restore movement after any screen", false);

    g_instance = this;
    listen<TickEvent>(&KeybindFix::onTick);
}

void KeybindFix::onEnable() {
    m_wasGui = false;
    m_haveSnap = false;
    m_active.store(false, std::memory_order_relaxed);
    hooks::setMoveInputPostTick(&KeybindFix::onMoveTick);
}

void KeybindFix::onDisable() {
    hooks::setMoveInputPostTick(nullptr);
    m_active.store(false, std::memory_order_relaxed);
}

bool KeybindFix::shouldRestoreFor(const std::string& screen) const {
    if (m_fixAll->value)
        return true;
    if (m_fixInventory->value &&
        (screen.find("inventory") != std::string::npos ||
         screen.find("chest") != std::string::npos ||
         screen.find("furnace") != std::string::npos ||
         screen.find("crafting") != std::string::npos ||
         screen.find("anvil") != std::string::npos ||
         screen.find("container") != std::string::npos))
        return true;
    if (m_fixPause->value && screen.find("pause") != std::string::npos)
        return true;
    if (m_fixChat->value && screen.find("chat") != std::string::npos)
        return true;
    return false;
}

void KeybindFix::onTick(TickEvent& event) {
    (void)event;
    const std::string screen = sdk::Context::get().currentScreenName();
    const bool inGui = isGuiScreen(screen);
    const bool isGame = screen == "hud_screen";

    if (inGui) {
        m_lastGuiScreen = screen;
        m_active.store(false, std::memory_order_relaxed);
        m_guiOpen.store(true, std::memory_order_relaxed);
        m_wasGui = true;
        return;
    }

    if (isGame) {
        m_guiOpen.store(false, std::memory_order_relaxed);
        if (m_wasGui && m_haveSnap && shouldRestoreFor(m_lastGuiScreen))
            m_active.store(true, std::memory_order_relaxed);
        m_wasGui = false;
    }
}

void KeybindFix::captureState(void* handler) {
    auto* b = static_cast<uint8_t*>(handler);
    std::memcpy(m_snapA.data(), b + kRangeA, kSizeA);
    std::memcpy(m_snapB.data(), b + kRangeB, kSizeB);
    m_haveSnap = true;
}

void KeybindFix::writeState(void* handler) const {
    auto* b = static_cast<uint8_t*>(handler);
    std::memcpy(b + kRangeA, m_snapA.data(), kSizeA);
    std::memcpy(b + kRangeB, m_snapB.data(), kSizeB);
}

bool KeybindFix::anyCapturedHeld() const {
    for (int vk = 7; vk < 256; ++vk)
        if (m_keys[static_cast<size_t>(vk)] && held(vk))
            return true;
    return m_controller && controllerMoving();
}

void KeybindFix::onMoveTick(void* handler) {
    auto* self = g_instance;
    if (!self || !self->enabled())
        return;
    if (!handler || !memory::isReadable(handler, kEnd))
        return;

    if (self->m_active.load(std::memory_order_relaxed)) {
        if (stateMoving(handler)) {
            self->m_active.store(false, std::memory_order_relaxed);
        } else if (self->anyCapturedHeld()) {
            self->writeState(handler);
            return;
        } else {
            self->m_active.store(false, std::memory_order_relaxed);
            return;
        }
    }

    if (!self->m_guiOpen.load(std::memory_order_relaxed) && stateMoving(handler)) {
        std::array<bool, 256> keys{};
        bool any = false;
        for (int vk = 7; vk < 256; ++vk) {
            if (held(vk)) {
                keys[static_cast<size_t>(vk)] = true;
                any = true;
            }
        }
        const bool ctrl = controllerMoving();
        if (any || ctrl) {
            self->captureState(handler);
            self->m_keys = keys;
            self->m_controller = ctrl;
        }
    }
}

}
