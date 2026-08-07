#include "Input/InputManager.h"

#include <Windows.h>
#include <TlHelp32.h>

#include <unordered_map>

#include "Event/Events.h"
#include "Module/ModuleManager.h"
#include "Render/DrawUtils.h"
#include "Utils/Logger.h"
#include "Utils/Platform.h"

namespace aerial::input {
namespace {

bool ignoredKey(int key) {
    switch (key) {
    case 0:
    case VK_SHIFT:
    case VK_CONTROL:
    case VK_MENU:
    case VK_LWIN:
    case VK_RWIN:
    case VK_CAPITAL:
    case VK_NUMLOCK:
    case VK_SCROLL:
        return true;
    default:
        return false;
    }
}

std::atomic<int> g_wheelNotches{0};

std::atomic<bool> g_swallowInput{false};

HHOOK g_wheelHook = nullptr;
DWORD g_hookedThread = 0;
std::unordered_map<DWORD, HHOOK> g_threadHooks;
ULONGLONG g_lastScan = 0;

std::atomic<uint64_t> g_hookCalls{0};
std::atomic<uint64_t> g_wheelEvents{0};
std::atomic<uint32_t> g_lastPointerMessage{0};

constexpr UINT kPointerFirst = 0x0240;
constexpr UINT kPointerLast = 0x0250;
constexpr UINT kPointerUp = 0x0247;
constexpr UINT kPointerLeave = 0x024A;
constexpr UINT kPointerCaptureChanged = 0x024C;

bool inputMessage(UINT id) {
    if (id >= WM_KEYFIRST && id <= WM_KEYLAST)
        return true;
    if (id >= WM_MOUSEFIRST && id <= WM_MOUSELAST)
        return true;
    if (id >= kPointerFirst && id <= kPointerLast)
        return true;
    return id == WM_INPUT;
}

bool releaseMessage(UINT id) {
    switch (id) {
    case WM_KEYUP:
    case WM_SYSKEYUP:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_XBUTTONUP:
    case kPointerUp:
    case kPointerLeave:
    case kPointerCaptureChanged:
        return true;
    default:
        return false;
    }
}

LRESULT CALLBACK messageProc(int code, WPARAM wParam, LPARAM lParam) {

    if (code == HC_ACTION && lParam && wParam == PM_REMOVE) {
        g_hookCalls.fetch_add(1, std::memory_order_relaxed);

        auto* message = reinterpret_cast<MSG*>(lParam);
        const UINT id = message->message;

        if (id == WM_INPUT || (id >= WM_MOUSEFIRST && id <= WM_MOUSELAST) ||
            (id >= 0x0240 && id <= 0x0250))
            g_lastPointerMessage.store(id, std::memory_order_relaxed);

        if (g_swallowInput.load(std::memory_order_relaxed) && inputMessage(id) &&
            !releaseMessage(id)) {

            if (id == WM_INPUT)
                DefWindowProcW(message->hwnd, WM_INPUT, message->wParam, message->lParam);

            message->message = WM_NULL;
            message->wParam = 0;
            message->lParam = 0;
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

}

void InputManager::installMessageHook() {

    const DWORD windowThread = platform::gameWindowThread();
    if (!windowThread)
        return;

    const ULONGLONG now = GetTickCount64();
    if (g_wheelHook && now - g_lastScan < 3000)
        return;
    g_lastScan = now;

    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return;

    const DWORD self = GetCurrentProcessId();
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);

    int added = 0;
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID != self)
                continue;
            if (g_threadHooks.find(entry.th32ThreadID) != g_threadHooks.end())
                continue;

            const HHOOK hook = SetWindowsHookExW(WH_GETMESSAGE, messageProc, nullptr,
                                                 entry.th32ThreadID);
            if (!hook)
                continue;

            g_threadHooks.emplace(entry.th32ThreadID, hook);
            ++added;

            if (entry.th32ThreadID == windowThread) {
                g_wheelHook = hook;
                g_hookedThread = windowThread;
            }
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);

    if (!g_wheelHook && !g_threadHooks.empty())
        g_wheelHook = g_threadHooks.begin()->second;

    if (added)
        LOG_INFO("Input", "message hook on {} thread(s) (+{}), window thread {}{}",
                 g_threadHooks.size(), added, windowThread,
                 g_hookedThread ? "" : " NOT hooked");
}

void InputManager::removeMessageHook() {
    for (const auto& [thread, hook] : g_threadHooks)
        UnhookWindowsHookEx(hook);

    g_threadHooks.clear();
    g_wheelHook = nullptr;
    g_hookedThread = 0;
    g_lastScan = 0;

    g_swallowInput.store(false, std::memory_order_relaxed);
}

bool InputManager::messageHooked() const { return g_wheelHook != nullptr; }

void InputManager::setSwallowInput(bool swallow) {
    g_swallowInput.store(swallow, std::memory_order_relaxed);
}

void InputManager::feedWheel(int notches) {
    if (notches == 0)
        return;
    g_wheelNotches.fetch_add(notches, std::memory_order_relaxed);
    g_wheelEvents.fetch_add(1, std::memory_order_relaxed);
}

InputManager& InputManager::get() {
    static InputManager instance;
    return instance;
}

bool InputManager::gameFocused() { return platform::gameFocused(); }

InputManager::Stats InputManager::stats() const {
    return {m_samples.load(std::memory_order_relaxed),
            m_polls.load(std::memory_order_relaxed),
            m_transitions.load(std::memory_order_relaxed),
            m_lastKey.load(std::memory_order_relaxed),
            m_asyncDowns.load(std::memory_order_relaxed),
            m_syncDowns.load(std::memory_order_relaxed),
            g_hookCalls.load(std::memory_order_relaxed),
            g_wheelEvents.load(std::memory_order_relaxed),
            g_lastPointerMessage.load(std::memory_order_relaxed)};
}

void InputManager::sample() {
    m_samples.fetch_add(1, std::memory_order_relaxed);

    platform::attachToGameInput();

    installMessageHook();

    const bool focused = platform::gameFocused();

    KeyState state{};
    int asyncDowns = 0;
    int syncDowns = 0;

    if (focused) {
        BYTE keyboard[kKeyCount]{};
        const bool haveSyncState = GetKeyboardState(keyboard) != 0;

        for (int key = 1; key < kKeyCount; ++key) {
            const bool asyncDown = (GetAsyncKeyState(key) & 0x8000) != 0;
            const bool syncDown = haveSyncState && (keyboard[key] & 0x80) != 0;

            asyncDowns += asyncDown;
            syncDowns += syncDown;

            state[static_cast<size_t>(key)] = asyncDown || syncDown;
        }
    }

    m_asyncDowns.store(asyncDowns, std::memory_order_relaxed);
    m_syncDowns.store(syncDowns, std::memory_order_relaxed);

    std::lock_guard lock(m_mutex);
    m_sampled = state;
}

void InputManager::poll() {
    m_polls.fetch_add(1, std::memory_order_relaxed);

    POINT cursor{};
    if (GetCursorPos(&cursor) && platform::screenToGame(cursor)) {
        const float scale = render::DrawUtils::scale();
        m_cursor = {static_cast<float>(cursor.x) / scale, static_cast<float>(cursor.y) / scale};
    }

    m_previous = m_down;
    {
        std::lock_guard lock(m_mutex);
        m_down = m_sampled;
    }

    auto& bus = EventBus::get();

    if (const int notches = g_wheelNotches.exchange(0, std::memory_order_relaxed); notches != 0) {
        MouseEvent wheel;
        wheel.button = notches > 0 ? MouseEvent::Button::ScrollUp : MouseEvent::Button::ScrollDown;
        wheel.down = true;
        wheel.position = m_cursor;
        wheel.wheel = static_cast<float>(notches);
        bus.dispatch(wheel);
    }

    for (int key = 1; key < kKeyCount; ++key) {
        const size_t index = static_cast<size_t>(key);
        if (m_down[index] == m_previous[index])
            continue;

        const bool down = m_down[index];
        m_transitions.fetch_add(1, std::memory_order_relaxed);
        if (down)
            m_lastKey.store(key, std::memory_order_relaxed);

        if (key == VK_LBUTTON || key == VK_RBUTTON || key == VK_MBUTTON) {
            MouseEvent mouse;
            mouse.button = key == VK_LBUTTON   ? MouseEvent::Button::Left
                           : key == VK_RBUTTON ? MouseEvent::Button::Right
                                               : MouseEvent::Button::Middle;
            mouse.down = down;
            mouse.position = m_cursor;
            bus.dispatch(mouse);
            continue;
        }

        if (ignoredKey(key))
            continue;

        KeyEvent event;
        event.key = key;
        event.down = down;
        bus.dispatch(event);

        if (down && !event.isCancelled() && !m_captured)
            ModuleManager::get().handleKey(key, true);
    }
}

bool InputManager::isDown(int virtualKey) const {
    if (virtualKey <= 0 || virtualKey >= kKeyCount)
        return false;
    return m_down[static_cast<size_t>(virtualKey)];
}

bool InputManager::wasPressed(int virtualKey) const {
    if (virtualKey <= 0 || virtualKey >= kKeyCount)
        return false;
    const size_t index = static_cast<size_t>(virtualKey);
    return m_down[index] && !m_previous[index];
}

char InputManager::characterFor(int virtualKey) {
    BYTE state[256]{};
    if (!GetKeyboardState(state))
        return 0;

    const UINT scan = MapVirtualKeyW(static_cast<UINT>(virtualKey), MAPVK_VK_TO_VSC);

    wchar_t buffer[8]{};
    if (ToUnicode(static_cast<UINT>(virtualKey), scan, state, buffer, 8, 0) == 1) {
        const wchar_t value = buffer[0];
        if (value >= 0x20 && value <= 0x7E)
            return static_cast<char>(value);
    }

    const bool shift = (state[VK_SHIFT] & 0x80) != 0;
    const bool capsLock = (state[VK_CAPITAL] & 0x01) != 0;

    if (virtualKey >= 'A' && virtualKey <= 'Z') {
        const bool upper = shift != capsLock;
        return static_cast<char>(upper ? virtualKey : virtualKey - 'A' + 'a');
    }
    if (virtualKey >= '0' && virtualKey <= '9' && !shift)
        return static_cast<char>(virtualKey);
    if (virtualKey >= VK_NUMPAD0 && virtualKey <= VK_NUMPAD9)
        return static_cast<char>('0' + (virtualKey - VK_NUMPAD0));
    if (virtualKey == VK_SPACE)
        return ' ';
    if (virtualKey == VK_OEM_MINUS || virtualKey == VK_SUBTRACT)
        return shift ? '_' : '-';

    return 0;
}

const char* InputManager::keyName(int virtualKey) {
    static char buffer[64];

    if (virtualKey == 0)
        return "None";

    switch (virtualKey) {
    case VK_LBUTTON: return "Mouse 1";
    case VK_RBUTTON: return "Mouse 2";
    case VK_MBUTTON: return "Mouse 3";
    case VK_SPACE:   return "Space";
    case VK_TAB:     return "Tab";
    case VK_ESCAPE:  return "Escape";
    case VK_RETURN:  return "Enter";
    case VK_BACK:    return "Backspace";
    case VK_DELETE:  return "Delete";
    case VK_INSERT:  return "Insert";
    case VK_HOME:    return "Home";
    case VK_END:     return "End";
    case VK_PRIOR:   return "Page Up";
    case VK_NEXT:    return "Page Down";
    case VK_UP:      return "Up";
    case VK_DOWN:    return "Down";
    case VK_LEFT:    return "Left";
    case VK_RIGHT:   return "Right";
    default: break;
    }

    if (virtualKey >= VK_F1 && virtualKey <= VK_F24) {
        std::snprintf(buffer, sizeof(buffer), "F%d", virtualKey - VK_F1 + 1);
        return buffer;
    }
    if (virtualKey >= VK_NUMPAD0 && virtualKey <= VK_NUMPAD9) {
        std::snprintf(buffer, sizeof(buffer), "Numpad %d", virtualKey - VK_NUMPAD0);
        return buffer;
    }
    if ((virtualKey >= '0' && virtualKey <= '9') || (virtualKey >= 'A' && virtualKey <= 'Z')) {
        buffer[0] = static_cast<char>(virtualKey);
        buffer[1] = '\0';
        return buffer;
    }

    const UINT scan = MapVirtualKeyW(static_cast<UINT>(virtualKey), MAPVK_VK_TO_VSC);
    wchar_t wide[64]{};
    if (GetKeyNameTextW(static_cast<LONG>(scan << 16), wide, 63) > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, buffer, sizeof(buffer), nullptr, nullptr);
        return buffer;
    }

    std::snprintf(buffer, sizeof(buffer), "Key %d", virtualKey);
    return buffer;
}

}
