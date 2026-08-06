#pragma once

#include <Windows.h>

namespace aerial::platform {

// Minecraft: Windows 10 Edition is a UWP app: its drawing surface is a
// Windows.UI.Core.CoreWindow owned by the game process, but that window is
// parented into an ApplicationFrameWindow owned by ApplicationFrameHost.exe.
//
// That means "is the game focused?" cannot be answered by comparing the
// foreground window's process id with our own - it never matches. These helpers
// resolve the CoreWindow once and compare against its root ancestor instead.

// The game's CoreWindow (or, on a non-UWP build, its main window). Cached.
HWND gameWindow();

// True when the game's window - or the frame hosting it - owns the foreground.
bool gameFocused();

// Client-area size of the game window, in pixels. {0,0} if unavailable.
SIZE clientSize();

// Thread that owns the game window, 0 if unknown.
DWORD gameWindowThread();

// Merges the calling thread's input queue with the game window's thread, so
// GetKeyState/GetKeyboardState report the real keyboard. A thread with no input
// queue of its own sees nothing otherwise - which is exactly what happens to a
// worker thread inside a UWP process. Idempotent; returns true once attached.
bool attachToGameInput();
void detachFromGameInput();

// Whatever currently owns the foreground, for diagnostics.
struct ForegroundInfo {
    HWND window = nullptr;
    DWORD processId = 0;
    char className[128]{};
};

ForegroundInfo foregroundInfo();

// Maps a screen point into the game window's client area.
bool screenToGame(POINT& point);

} // namespace aerial::platform
