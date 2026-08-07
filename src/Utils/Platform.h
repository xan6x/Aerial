#pragma once

#include <Windows.h>

namespace aerial::platform {

HWND gameWindow();

bool gameFocused();

SIZE clientSize();

DWORD gameWindowThread();

bool attachToGameInput();
void detachFromGameInput();

struct ForegroundInfo {
    HWND window = nullptr;
    DWORD processId = 0;
    char className[128]{};
};

ForegroundInfo foregroundInfo();

bool screenToGame(POINT& point);

}
