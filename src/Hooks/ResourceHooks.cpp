#include <atomic>
#include <chrono>

#include "Hooks/HookRegistry.h"
#include "Render/SkyCubemap.h"
#include "SDK/Offsets.h"
#include "Utils/Hook.h"
#include "Utils/Logger.h"
#include "Utils/Memory.h"

namespace aerial::hooks {
namespace {

namespace func = offsets::func;
namespace camera = offsets::field::levelRendererCamera;

using Clock = std::chrono::steady_clock;

Detour<void(__fastcall*)(void*, void*, char, void*)> g_packsChanged;
Detour<void(__fastcall*)(void*, void*)> g_updateViewArea;
Detour<void(__fastcall*)(void*, void*, void*, void*, char, char)> g_uploadTexture;

std::atomic<bool> g_rebuildPending{false};
std::atomic<Clock::time_point> g_packsChangedAt{};
std::atomic<Clock::time_point> g_lastUpload{};

constexpr auto kSettleGap = std::chrono::milliseconds(750);
constexpr auto kMinDelay = std::chrono::milliseconds(500);
constexpr auto kGiveUpAfter = std::chrono::seconds(30);

void __fastcall onUpdateViewArea(void* self, void* frame) {
    g_updateViewArea.call(self, frame);

    if (!g_rebuildPending.load(std::memory_order_acquire))
        return;

    const auto now = Clock::now();
    const auto elapsed = now - g_packsChangedAt.load(std::memory_order_relaxed);

    if (elapsed > kGiveUpAfter) {
        g_rebuildPending.store(false, std::memory_order_release);
        LOG_WARN("Resources", "the atlas never settled; leaving the chunks alone");
        return;
    }

    if (elapsed < kMinDelay)
        return;
    if (now - g_lastUpload.load(std::memory_order_relaxed) < kSettleGap)
        return;

    g_rebuildPending.store(false, std::memory_order_release);

    *reinterpret_cast<int32_t*>(static_cast<char*>(self) + camera::viewRadius) = -1;
    LOG_INFO("Resources", "rebuilding the loaded chunks against the new atlas");
}

void __fastcall onUploadTexture(void* self, void* a2, void* a3, void* a4, char a5, char a6) {
    g_uploadTexture.call(self, a2, a3, a4, a5, a6);

    if (g_rebuildPending.load(std::memory_order_relaxed))
        g_lastUpload.store(Clock::now(), std::memory_order_relaxed);
}

void __fastcall onPacksChanged(void* self, void* a2, char a3, void* a4) {
    g_packsChanged.call(self, a2, a3, a4);

    LOG_INFO("Resources", "the active packs changed; dropping our cached textures");
    render::SkyCubemap::get().unload();

    const auto now = Clock::now();
    g_packsChangedAt.store(now, std::memory_order_relaxed);
    g_lastUpload.store(now, std::memory_order_relaxed);
    g_rebuildPending.store(true, std::memory_order_release);
}

bool install() {
    g_packsChanged.attach("MinecraftGame::_onActiveResourcePacksChanged",
                          memory::rva(func::MinecraftGame_onActiveResourcePacksChanged),
                          &onPacksChanged);

    g_updateViewArea.attach("LevelRendererCamera::updateViewArea",
                            memory::rva(func::LevelRendererCamera_updateViewArea),
                            &onUpdateViewArea);

    g_uploadTexture.attach("mce::TextureGroup::uploadTexture",
                           memory::rva(func::TextureGroup_uploadTexture),
                           &onUploadTexture);
    return true;
}

const Installer g_installer{"Resources", &install};

}
}
