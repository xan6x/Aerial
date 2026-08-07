#include "Utils/Platform.h"

#include <mutex>
#include <string>

#include "Utils/Logger.h"

namespace aerial::platform {
namespace {

constexpr wchar_t kCoreWindowClass[] = L"Windows.UI.Core.CoreWindow";

std::wstring classNameOf(HWND window) {
    wchar_t buffer[128]{};
    const int length = GetClassNameW(window, buffer, 127);
    return std::wstring(buffer, static_cast<size_t>(length > 0 ? length : 0));
}

bool ownedByUs(HWND window) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    return pid == GetCurrentProcessId();
}

bool isConsole(HWND window) {
    const std::wstring name = classNameOf(window);
    return name == L"ConsoleWindowClass" || name == L"CASCADIA_HOSTING_WINDOW_CLASS" ||
           name == L"PseudoConsoleWindow";
}

struct Search {
    HWND coreWindow = nullptr;
    HWND fallback = nullptr;
    LONG fallbackArea = 0;
};

BOOL CALLBACK childProc(HWND window, LPARAM param) {
    auto* search = reinterpret_cast<Search*>(param);
    if (ownedByUs(window) && classNameOf(window) == kCoreWindowClass) {
        search->coreWindow = window;
        return FALSE;
    }
    return TRUE;
}

BOOL CALLBACK topLevelProc(HWND window, LPARAM param) {
    auto* search = reinterpret_cast<Search*>(param);

    if (ownedByUs(window)) {
        if (classNameOf(window) == kCoreWindowClass) {
            search->coreWindow = window;
            return FALSE;
        }

        if (IsWindowVisible(window) && !isConsole(window)) {
            RECT rect{};
            GetClientRect(window, &rect);
            const LONG area = (rect.right - rect.left) * (rect.bottom - rect.top);
            if (area > search->fallbackArea) {
                search->fallbackArea = area;
                search->fallback = window;
            }
        }
    }

    EnumChildWindows(window, childProc, param);
    return search->coreWindow == nullptr;
}

}

HWND gameWindow() {

    static std::mutex mutex;
    static HWND cached = nullptr;

    std::lock_guard lock(mutex);
    if (cached && IsWindow(cached))
        return cached;

    Search search;
    EnumWindows(topLevelProc, reinterpret_cast<LPARAM>(&search));

    cached = search.coreWindow ? search.coreWindow : search.fallback;
    if (cached) {
        const std::wstring wide = classNameOf(cached);
        std::string name(wide.size(), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), name.data(),
                            static_cast<int>(name.size()), nullptr, nullptr);

        RECT rect{};
        GetClientRect(cached, &rect);
        LOG_INFO("Platform", "game window {} class '{}' client {}x{}", static_cast<void*>(cached), name,
                 rect.right - rect.left, rect.bottom - rect.top);
    } else {
        LOG_WARN("Platform", "no game window found");
    }
    return cached;
}

bool gameFocused() {
    const HWND foreground = GetForegroundWindow();
    if (!foreground)
        return false;

    if (ownedByUs(foreground))
        return !isConsole(foreground);

    bool ours = false;
    EnumChildWindows(
        foreground,
        [](HWND child, LPARAM param) -> BOOL {
            if (!ownedByUs(child))
                return TRUE;
            *reinterpret_cast<bool*>(param) = true;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&ours));

    return ours;
}

DWORD gameWindowThread() {
    const HWND window = gameWindow();
    if (!window)
        return 0;

    DWORD pid = 0;
    return GetWindowThreadProcessId(window, &pid);
}

namespace {
DWORD g_attachedTo = 0;
DWORD g_attachedFrom = 0;
}

bool attachToGameInput() {
    const DWORD target = gameWindowThread();
    const DWORD self = GetCurrentThreadId();

    if (!target || target == self)
        return false;
    if (g_attachedTo == target && g_attachedFrom == self)
        return true;

    if (g_attachedTo)
        detachFromGameInput();

    if (!AttachThreadInput(self, target, TRUE)) {
        LOG_WARN("Platform", "AttachThreadInput({} -> {}) failed: {}", self, target, GetLastError());
        return false;
    }

    g_attachedFrom = self;
    g_attachedTo = target;
    LOG_INFO("Platform", "input queue attached to game thread {}", target);
    return true;
}

void detachFromGameInput() {
    if (!g_attachedTo)
        return;
    AttachThreadInput(g_attachedFrom, g_attachedTo, FALSE);
    g_attachedTo = 0;
    g_attachedFrom = 0;
}

ForegroundInfo foregroundInfo() {
    ForegroundInfo info;
    info.window = GetForegroundWindow();
    if (!info.window)
        return info;

    GetWindowThreadProcessId(info.window, &info.processId);

    const std::wstring wide = classNameOf(info.window);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), info.className,
                        static_cast<int>(sizeof(info.className)) - 1, nullptr, nullptr);
    return info;
}

SIZE clientSize() {
    SIZE size{0, 0};
    const HWND window = gameWindow();
    if (!window)
        return size;

    RECT rect{};
    if (GetClientRect(window, &rect)) {
        size.cx = rect.right - rect.left;
        size.cy = rect.bottom - rect.top;
    }
    return size;
}

bool screenToGame(POINT& point) {
    const HWND window = gameWindow();
    return window && ScreenToClient(window, &point);
}

}
