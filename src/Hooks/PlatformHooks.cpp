#include <Windows.h>

#include <atomic>

#include "Event/EventBus.h"
#include "Event/Events.h"
#include "GUI/ClickGui.h"
#include "Hooks/HookRegistry.h"
#include "Input/InputManager.h"
#include "SDK/Offsets.h"
#include "Utils/Hook.h"
#include "Utils/Logger.h"
#include "Utils/Memory.h"
#include "Utils/Platform.h"

namespace aerial::hooks {
namespace {

namespace func = offsets::func;

Detour<int(__fastcall*)(void*)> g_getScreenWidth;

std::atomic<void*> g_platform{nullptr};

bool isPlatform(const void* candidate) {
    if (!candidate || !memory::isReadable(candidate, sizeof(void*)))
        return false;

    return *reinterpret_cast<void* const*>(candidate) ==
           reinterpret_cast<void*>(memory::rva(func::AppPlatform_vtable));
}

void* platform() {
    if (void* cached = g_platform.load(std::memory_order_relaxed))
        return cached;

    const auto* slot = reinterpret_cast<void* const*>(memory::rva(func::g_appPlatform));
    if (!memory::isReadable(slot, sizeof(void*)))
        return nullptr;

    void* candidate = *slot;
    if (!isPlatform(candidate))
        return nullptr;

    g_platform.store(candidate, std::memory_order_relaxed);
    return candidate;
}

int __fastcall onGetScreenWidth(void* self) {
    if (!g_platform.load(std::memory_order_relaxed) && isPlatform(self))
        g_platform.store(self, std::memory_order_relaxed);

    return g_getScreenWidth.call(self);
}

bool looksFullscreen() {
    const HWND window = platform::gameWindow();
    if (!window)
        return false;

    RECT client{};
    if (!GetClientRect(window, &client))
        return false;

    return client.right >= GetSystemMetrics(SM_CXSCREEN) &&
           client.bottom >= GetSystemMetrics(SM_CYSCREEN);
}

void toggleFullscreen() {
    void* self = platform();
    if (!self) {
        LOG_WARN("Platform", "no usable AppPlatform, cannot switch the window mode");
        return;
    }

    const int mode = looksFullscreen() ? 0 : 1;

    using SetMode = void(__fastcall*)(void*, int);
    auto set = reinterpret_cast<SetMode>(memory::rva(func::AppPlatform_setFullscreenMode));

    LOG_INFO("Platform", "F11: switching to {}", mode ? "fullscreen" : "windowed");
    set(self, mode);
}

void onKey(KeyEvent& event) {
    if (!event.down || event.key != VK_F11)
        return;
    if (gui::ClickGui::get().isOpen() || input::InputManager::get().captured())
        return;

    event.cancel();
    toggleFullscreen();
}

bool install() {
    g_getScreenWidth.attach("AppPlatform::getScreenWidth",
                            memory::rva(func::AppPlatform_getScreenWidth), &onGetScreenWidth);

    static int owner = 0;
    EventBus::get().subscribe<KeyEvent>(&owner, [](KeyEvent& event) { onKey(event); });
    return true;
}

const Installer g_installer{"Platform", &install};

}
}
