#include "Aerial.h"

#include <Windows.h>
#include <Psapi.h>
#include <filesystem>

#include "Config/Config.h"
#include "GUI/ClickGui.h"
#include "Hooks/Hooks.h"
#include "Input/InputManager.h"
#include "Module/ModuleManager.h"
#include "Render/D2DOverlay.h"
#include "Render/DrawUtils.h"
#include "SDK/Context.h"
#include "Utils/CrashLog.h"
#include "Utils/Hook.h"
#include "Utils/Logger.h"
#include "Utils/Memory.h"
#include "Utils/Platform.h"

namespace aerial {
namespace {

// Identity of the Minecraft.Windows.exe 1.1.5 build every offset in
// SDK/Offsets.h was taken from, read straight out of the IDA database's PE
// header. Detouring a different binary at these addresses would corrupt the
// process, so startup refuses to continue on a mismatch.
constexpr uint32_t kExpectedTimestamp = 0x5976DA7B;  // 2017-07-25
constexpr uint32_t kExpectedImageSize = 0x1A91000;

struct BuildInfo {
    uint32_t timestamp = 0;
    uint32_t imageSize = 0;
};

BuildInfo readBuildInfo() {
    BuildInfo info;
    const auto base = memory::base();
    if (!base)
        return info;

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return info;

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return info;

    info.timestamp = nt->FileHeader.TimeDateStamp;
    info.imageSize = nt->OptionalHeader.SizeOfImage;
    return info;
}

} // namespace

Aerial& Aerial::get() {
    static Aerial instance;
    return instance;
}

bool Aerial::verifyGameBuild() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    const BuildInfo info = readBuildInfo();
    LOG_INFO("Aerial", "host: {} (base {:#x}, image {:#x}, timestamp {:#x})",
             std::filesystem::path(path).filename().string(), memory::base(), info.imageSize,
             info.timestamp);

    if (info.timestamp != kExpectedTimestamp || info.imageSize != kExpectedImageSize) {
        LOG_ERROR("Aerial",
                  "build mismatch: expected timestamp {:#x} / image {:#x}, got {:#x} / {:#x}",
                  kExpectedTimestamp, kExpectedImageSize, info.timestamp, info.imageSize);
        return false;
    }
    return true;
}

void Aerial::startup(void* moduleHandle) {
    m_module = moduleHandle;

    Logger::get().init(true);
    LOG_INFO("Aerial", "AerialClient " AERIAL_VERSION " starting");

    // Before anything is hooked, so a fault during startup is reported too.
    crash::install();

    const bool buildMatches = verifyGameBuild();
    if (!buildMatches) {
        // Refuse to hook a binary the offsets were not built for: a wrong
        // detour target is an instant crash, and a silent one.
        LOG_ERROR("Aerial", "aborting startup - wrong game build");
        MessageBoxW(nullptr,
                    L"AerialClient targets Minecraft: Windows 10 Edition 1.1.5.\n"
                    L"The running game is a different build, so the client will not load.",
                    L"AerialClient", MB_ICONERROR | MB_OK);
        crash::remove();
        Logger::get().shutdown();
        return;
    }

    ModuleManager::get().registerAll();

    if (!hooks::installAll()) {
        // Some detours may already be created and enabled. Leaving them behind
        // would be fatal: this thread unloads the DLL on the way out, and every
        // detour that survived would then be a jump into a hole in the address
        // space. That is exactly the crash a failed startup used to cause on the
        // next injection.
        LOG_ERROR("Aerial", "hook installation failed - rolling back");

        input::InputManager::get().removeMessageHook();
        hooks::removeAll();
        render::DrawUtils::releaseResources();
        render::D2DOverlay::get().shutdown();
        ModuleManager::get().shutdown();

        crash::remove();
        Logger::get().shutdown();
        return;
    }

    Config::get().loadActive();

    m_running = true;
    LOG_INFO("Aerial", "ready - press Insert for the menu, End to unload");
}

void Aerial::shutdown() {
    if (!m_running.exchange(false))
        return;

    LOG_INFO("Aerial", "shutting down");

    Config::get().save();

    // Close the menu on the game's own thread. close() hands the cursor back
    // through ClientInstance::grabMouse, and this thread has no business calling
    // into the game's state - it is the one that read a key, nothing more.
    LOG_DEBUG("Aerial", "teardown: menu");
    if (!hooks::requestTeardown(500))
        gui::ClickGui::get().close();

    // Message hooks first, so the drain inside removeAll() covers them too.
    // Unhooking does not wait for callbacks already running, and by this point
    // there is one parked on every thread in the process.
    LOG_DEBUG("Aerial", "teardown: message hook");
    input::InputManager::get().removeMessageHook();

    LOG_DEBUG("Aerial", "teardown: detours");
    hooks::removeAll();

    // Only now is it certain no Present can run, which is what makes releasing
    // the shared texture and both devices safe. Skipping this leaked an entire
    // D3D11 device per inject/eject cycle.
    LOG_DEBUG("Aerial", "teardown: overlay");
    render::DrawUtils::releaseResources();
    render::D2DOverlay::get().shutdown();

    LOG_DEBUG("Aerial", "teardown: modules");
    platform::detachFromGameInput();
    ModuleManager::get().shutdown();
    sdk::Context::get().reset();

    LOG_INFO("Aerial", "unloaded");

    // Last, and only now: until this point the handler is what would report a
    // fault raised by the teardown itself.
    crash::remove();
    Logger::get().shutdown();
}

} // namespace aerial
