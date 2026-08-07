#include "Aerial.h"

#include <Windows.h>
#include <Psapi.h>
#include <filesystem>

#include "Config/Config.h"
#include "GUI/ClickGui.h"
#include "Hooks/Hooks.h"
#include "Module/ModuleManager.h"
#include "SDK/Context.h"
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

    const bool buildMatches = verifyGameBuild();
    if (!buildMatches) {
        // Refuse to hook a binary the offsets were not built for: a wrong
        // detour target is an instant crash, and a silent one.
        LOG_ERROR("Aerial", "aborting startup - wrong game build");
        MessageBoxW(nullptr,
                    L"AerialClient targets Minecraft: Windows 10 Edition 1.1.5.\n"
                    L"The running game is a different build, so the client will not load.",
                    L"AerialClient", MB_ICONERROR | MB_OK);
        return;
    }

    ModuleManager::get().registerAll();

    if (!hooks::installAll()) {
        LOG_ERROR("Aerial", "hook installation failed");
        return;
    }

    Config::get().loadActive();

    m_running = true;
    LOG_INFO("Aerial", "ready - press Y for the menu, End to unload");
}

void Aerial::shutdown() {
    if (!m_running.exchange(false))
        return;

    LOG_INFO("Aerial", "shutting down");

    Config::get().save();
    gui::ClickGui::get().close();

    hooks::removeAll();
    platform::detachFromGameInput();
    ModuleManager::get().shutdown();
    sdk::Context::get().reset();

    LOG_INFO("Aerial", "unloaded");
    Logger::get().shutdown();
}

} // namespace aerial
