#include <Windows.h>

#include "Aerial.h"
#include "Hooks/Hooks.h"
#include "Input/InputManager.h"
#include "Render/D2DOverlay.h"
#include "Render/DrawUtils.h"
#include "Utils/Logger.h"
#include "Utils/Platform.h"

namespace {

HMODULE g_module = nullptr;

// Every few seconds, report whether the hooks are running and what owns the
// foreground. If a keybind does nothing, this line says which half is at
// fault: no frames means the render hook is dead, frames but focused=false
// means the focus check is wrong.
void logHeartbeat() {
    static uint64_t lastFrames = 0;
    static uint64_t lastTicks = 0;
    static uint64_t lastUpdates = 0;
    static uint64_t lastOverlays = 0;

    static aerial::input::InputManager::Stats lastInput;

    const uint64_t frames = aerial::hooks::renderFrameCount();
    const uint64_t ticks = aerial::hooks::playerTickCount();
    const uint64_t updates = aerial::hooks::gameUpdateCount();
    const uint64_t overlays = aerial::hooks::overlayCount();
    const auto input = aerial::input::InputManager::get().stats();
    const auto draw = aerial::render::DrawUtils::stats();
    const auto foreground = aerial::platform::foregroundInfo();

    LOG_INFO("Watchdog",
             "updates +{} frames +{} ticks +{} overlays +{} | samples +{} polls +{} keys +{} last '{}' | "
             "down async={} sync={} | msgs +{} wheels +{} last pointer msg 0x{:04X} | "
             "fills {}/{} texts {}/{} | backend {} ({}) | focused={} | fg {} pid {} class '{}'",
             updates - lastUpdates, frames - lastFrames, ticks - lastTicks, overlays - lastOverlays,
             input.samples - lastInput.samples, input.polls - lastInput.polls,
             input.transitions - lastInput.transitions,
             aerial::input::InputManager::keyName(input.lastKey), input.asyncDowns, input.syncDowns,
             input.hookCalls - lastInput.hookCalls, input.wheelEvents - lastInput.wheelEvents,
             input.lastPointerMessage,
             draw.fills, draw.fillsSkipped, draw.texts, draw.textsSkipped,
             aerial::render::DrawUtils::backendName(), aerial::render::D2DOverlay::get().status(),
             aerial::platform::gameFocused(), static_cast<void*>(foreground.window),
             foreground.processId, foreground.className);

    lastFrames = frames;
    lastTicks = ticks;
    lastUpdates = updates;
    lastOverlays = overlays;
    lastInput = input;
}

// The module handle for this DLL, or null when the loader has never heard of
// it - which is what a manually mapping injector leaves behind. Derived from
// our own code rather than from what DllMain was handed, because a mapper is
// free to pass anything there.
HMODULE loaderModule() {
    HMODULE module = nullptr;
    const bool found = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                          reinterpret_cast<LPCWSTR>(&loaderModule), &module);
    return found ? module : nullptr;
}

// Startup runs off the loader lock: DllMain must not hook, allocate consoles or
// spawn work while it holds it.
DWORD WINAPI startupThread(LPVOID) {
    aerial::Aerial::get().startup(g_module);

    LOG_INFO("Aerial", "loaded from {} ({})", static_cast<void*>(loaderModule()),
             loaderModule() ? "loader-mapped" : "manually mapped - eject will not unload the image");

    // Eject key: End. Polled here rather than through the input hooks so the
    // client can always be removed, even if a hook misbehaves.
    // 15 ms: fast enough that key presses are not missed between frames, and
    // this thread is the one proven able to read the key state in this process.
    constexpr int kIntervalMs = 15;
    constexpr int kHeartbeatTicks = 3000 / kIntervalMs;

    int sinceHeartbeat = 0;
    while (!aerial::Aerial::get().shutdownRequested()) {
        if (GetAsyncKeyState(VK_END) & 0x8000)
            break;

        if (aerial::Aerial::get().running()) {
            aerial::input::InputManager::get().sample();

            if (++sinceHeartbeat >= kHeartbeatTicks) {
                sinceHeartbeat = 0;
                logHeartbeat();
            }
        }
        Sleep(kIntervalMs);
    }

    aerial::Aerial::get().shutdown();

    // FreeLibraryAndExitThread is only valid for a module the loader knows
    // about. Injectors that map the image themselves never registered one, and
    // handing the loader a base address it has no record of unloads nothing -
    // it either fails or takes the process with it. Ask whether this code is in
    // a module the loader owns, and only then unload.
    //
    // UNCHANGED_REFCOUNT, because the point is to ask a question, not to take a
    // reference that then has to be given back.
    HMODULE self = loaderModule();
    if (self)
        FreeLibraryAndExitThread(self, 0);

    // Manually mapped: the detours are off and everything is torn down, which is
    // as far as an eject can go from in here. The image stays where it is.
    ExitThread(0);
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH: {
        g_module = module;
        DisableThreadLibraryCalls(module);

        const HANDLE thread = CreateThread(nullptr, 0, startupThread, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
        break;
    }
    case DLL_PROCESS_DETACH:
        // Process teardown: the game is going away anyway, so only flush state
        // that lives outside the process.
        break;
    default:
        break;
    }
    return TRUE;
}
