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

// Only the ambiguous modifier aliases are dropped: VK_SHIFT/CONTROL/MENU fire
// alongside their left/right counterparts, so keeping both would double every
// event. The sided keys stay bindable - Right Shift is a common menu bind.
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

// ── The wheel ────────────────────────────────────────────────────────────────
//
// There is no "is the wheel down" state to poll - a detent is an event, not a
// state - so it is fed in from the MouseDevice::feed hook and drained by poll().
// Notches, not raw units: that is the form the game hands it over in.
//
// This used to be read off the message queue, on the assumption that the wheel
// arrives as WM_MOUSEWHEEL or WM_POINTERWHEEL or inside WM_INPUT. In this
// process it arrives as none of them, and the menu simply never scrolled.
std::atomic<int> g_wheelNotches{0};

// ── Taking the game's input away ─────────────────────────────────────────────
//
// A second line of defence only. The game does not read its input from this
// queue - that is settled, and it is why the real blocking lives on
// InputHandler::_handleButtonEvent instead - but anything else in the process
// that does read it has no business acting while the menu is up.
//
// Releases are deliberately let through. A key held when the menu opened has
// already been seen going down, and dropping its WM_KEYUP would leave whoever
// saw it convinced it is still held.
std::atomic<bool> g_swallowInput{false};

HHOOK g_wheelHook = nullptr;   // the game window's thread, for reporting
DWORD g_hookedThread = 0;
std::unordered_map<DWORD, HHOOK> g_threadHooks;
ULONGLONG g_lastScan = 0;

// Diagnostics: how much of the queue the hook sees, and how many notches the
// game has handed us.
std::atomic<uint64_t> g_hookCalls{0};
std::atomic<uint64_t> g_wheelEvents{0};
std::atomic<uint32_t> g_lastPointerMessage{0};

// The pointer family is only declared by the SDK at WINVER 6.2 and above, and
// this translation unit does not need to raise its target to name three values.
constexpr UINT kPointerFirst = 0x0240;
constexpr UINT kPointerLast = 0x0250;
constexpr UINT kPointerUp = 0x0247;             // WM_POINTERUP
constexpr UINT kPointerLeave = 0x024A;          // WM_POINTERLEAVE
constexpr UINT kPointerCaptureChanged = 0x024C; // WM_POINTERCAPTURECHANGED

// Anything that could move the player, the camera, the hotbar or a game screen.
bool inputMessage(UINT id) {
    if (id >= WM_KEYFIRST && id <= WM_KEYLAST)
        return true;
    if (id >= WM_MOUSEFIRST && id <= WM_MOUSELAST)
        return true;
    if (id >= kPointerFirst && id <= kPointerLast)
        return true;
    return id == WM_INPUT;
}

// The ones that only ever end an interaction, never start one.
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
    // The hook is called for peeks as well as for removals; acting on both would
    // count every detent twice or more.
    if (code == HC_ACTION && lParam && wParam == PM_REMOVE) {
        g_hookCalls.fetch_add(1, std::memory_order_relaxed);

        auto* message = reinterpret_cast<MSG*>(lParam);
        const UINT id = message->message;

        // Remember anything pointer-shaped, so a wheel arriving by a route not
        // handled below still shows up in the log.
        if (id == WM_INPUT || (id >= WM_MOUSEFIRST && id <= WM_MOUSELAST) ||
            (id >= 0x0240 && id <= 0x0250))
            g_lastPointerMessage.store(id, std::memory_order_relaxed);

        if (g_swallowInput.load(std::memory_order_relaxed) && inputMessage(id) &&
            !releaseMessage(id)) {
            // Raw input holds a buffer per message that the system only frees
            // once the message reaches DefWindowProc. Nulling WM_INPUT without
            // this leaks a little for every mouse report - thousands a second -
            // for as long as the menu stays open.
            if (id == WM_INPUT)
                DefWindowProcW(message->hwnd, WM_INPUT, message->wParam, message->lParam);

            message->message = WM_NULL;
            message->wParam = 0;
            message->lParam = 0;
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

} // namespace

void InputManager::installMessageHook() {
    // Which thread pumps the wheel is not knowable from outside: a DirectX UWP
    // app can run its input on a core-independent thread that has nothing to do
    // with the one owning the CoreWindow. So every thread in the process gets a
    // hook. On a thread without a message queue that costs nothing, and it
    // removes the guesswork entirely.
    const DWORD windowThread = platform::gameWindowThread();
    if (!windowThread)
        return;

    // Threads come and go, so this re-scans - but not every sample.
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

            // hMod is null on purpose: the hook lives in this process, so
            // Windows wants it that way and there is no module to keep loaded.
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

    // Nothing is filtering the queue any more, so the flag must not survive to
    // describe a state that no longer exists.
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

    // Without this the synchronous key APIs below have no input queue to read
    // from and report nothing at all.
    platform::attachToGameInput();

    // The game window does not exist for the first frames after injection, so
    // this keeps trying until it does. It returns immediately once attached.
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

            // Either source counts: whichever one this platform actually feeds
            // is the one that wins, and the watchdog reports which that is.
            state[static_cast<size_t>(key)] = asyncDown || syncDown;
        }
    }
    // Unfocused: leave everything false so keys cannot stick down.

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

    // Wheel first: notches accumulate between polls, and one event carrying the
    // whole movement is both cheaper and smoother than replaying a detent at a
    // time.
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

        // Binds fire on press only, and never while a GUI has capture.
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

    // First ask the active layout, so punctuation lands where the key actually
    // prints it.
    wchar_t buffer[8]{};
    if (ToUnicode(static_cast<UINT>(virtualKey), scan, state, buffer, 8, 0) == 1) {
        const wchar_t value = buffer[0];
        if (value >= 0x20 && value <= 0x7E)
            return static_cast<char>(value);
    }

    // Otherwise fall back to the key's own label. On a non-Latin layout the
    // call above returns Cyrillic, which cannot go into a file name and used to
    // be dropped - so letters simply did not type while digits did. The key is
    // still plainly "A" on the keyboard, and that is what a config name wants.
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

    // Fall back to the layout-dependent name Windows reports.
    const UINT scan = MapVirtualKeyW(static_cast<UINT>(virtualKey), MAPVK_VK_TO_VSC);
    wchar_t wide[64]{};
    if (GetKeyNameTextW(static_cast<LONG>(scan << 16), wide, 63) > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, buffer, sizeof(buffer), nullptr, nullptr);
        return buffer;
    }

    std::snprintf(buffer, sizeof(buffer), "Key %d", virtualKey);
    return buffer;
}

} // namespace aerial::input
