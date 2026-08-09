#include "Module/Modules/FreeLook.h"

#include <atomic>
#include <cstdint>

#include "Event/Events.h"
#include "Hooks/HookRegistry.h"
#include "Input/InputManager.h"
#include "SDK/Context.h"
#include "SDK/Offsets.h"
#include "Utils/Hook.h"
#include "Utils/Math.h"
#include "Utils/Memory.h"

namespace aerial::modules {
namespace {

namespace func = offsets::func;
namespace ef = offsets::field::entity;

Detour<void(__fastcall*)(void*, void*, char)> g_turn;
Detour<void(__fastcall*)(void*, void*, float, float)> g_moveCamera;

std::atomic<bool> g_active{false};
std::atomic<void*> g_localPlayer{nullptr};
std::atomic<int> g_savedPerspective{0};

std::atomic<float> g_camPitch{0.0f};
std::atomic<float> g_camYaw{0.0f};
std::atomic<float> g_frozenPitch{0.0f};
std::atomic<float> g_frozenYaw{0.0f};

constexpr int kPerspective[] = {1, 2};

float* rotPtr(void* entity) {
    return reinterpret_cast<float*>(static_cast<char*>(entity) + ef::rot);
}

float* rotOldPtr(void* entity) {
    return reinterpret_cast<float*>(static_cast<char*>(entity) + ef::rotOld);
}

void __fastcall onTurn(void* self, void* delta, char fast) {
    if (g_active.load(std::memory_order_relaxed) && self == g_localPlayer.load(std::memory_order_relaxed)) {
        float* rot = rotPtr(self);
        float* rotOld = rotOldPtr(self);

        rot[0] = rotOld[0] = g_camPitch.load(std::memory_order_relaxed);
        rot[1] = rotOld[1] = g_camYaw.load(std::memory_order_relaxed);

        g_turn.call(self, delta, fast);

        g_camPitch.store(rot[0], std::memory_order_relaxed);
        g_camYaw.store(rot[1], std::memory_order_relaxed);

        rot[0] = rotOld[0] = g_frozenPitch.load(std::memory_order_relaxed);
        rot[1] = rotOld[1] = g_frozenYaw.load(std::memory_order_relaxed);
        return;
    }

    g_turn.call(self, delta, fast);
}

void __fastcall onMoveCamera(void* renderer, void* camera, float a2, float a3) {
    void* player = g_localPlayer.load(std::memory_order_relaxed);
    if (g_active.load(std::memory_order_relaxed) && player) {
        float* rot = rotPtr(player);
        float* rotOld = rotOldPtr(player);

        const float savedPitch = rot[0];
        const float savedYaw = rot[1];
        const float savedOldPitch = rotOld[0];
        const float savedOldYaw = rotOld[1];

        rot[0] = rotOld[0] = g_camPitch.load(std::memory_order_relaxed);
        rot[1] = rotOld[1] = g_camYaw.load(std::memory_order_relaxed);

        g_moveCamera.call(renderer, camera, a2, a3);

        rot[0] = savedPitch;
        rot[1] = savedYaw;
        rotOld[0] = savedOldPitch;
        rotOld[1] = savedOldYaw;
        return;
    }

    g_moveCamera.call(renderer, camera, a2, a3);
}

bool install() {
    g_turn.attach("Entity::turn", memory::rva(func::Entity_turn), &onTurn);
    g_moveCamera.attach("LevelRendererPlayer::moveCameraToPlayer",
                        memory::rva(func::LevelRendererPlayer_moveCameraToPlayer), &onMoveCamera);
    return true;
}

const hooks::Installer g_installer{"FreeLook", &install};

void* gameOptions() {
    auto& ctx = sdk::Context::get();
    if (!ctx.client)
        return nullptr;
    auto* game = ctx.client->game();
    if (!game || !memory::isReadable(game, 0x200))
        return nullptr;
    using Fn = void*(__fastcall*)(void*);
    return reinterpret_cast<Fn>(memory::rva(func::MinecraftGame_getOptions))(game);
}

int getPerspective(void* options) {
    using Fn = int(__fastcall*)(void*);
    return reinterpret_cast<Fn>(memory::rva(func::Options_getPlayerViewPerspective))(options);
}

void setPerspective(void* options, int value) {
    using Fn = void(__fastcall*)(void*, int);
    reinterpret_cast<Fn>(memory::rva(func::Options_setPlayerViewPerspective))(options, value);
}

void deactivate() {
    if (!g_active.exchange(false, std::memory_order_relaxed))
        return;
    if (void* options = gameOptions())
        setPerspective(options, g_savedPerspective.load(std::memory_order_relaxed));
}

}

FreeLook::FreeLook()
    : Module("FreeLook", "Detaches the camera so you can look around without turning your body",
             Category::Visuals) {
    m_hold = addBool("Hold", "Free-look while the bind is held instead of toggling the module", true);
    m_mode = addEnum("Perspective", "Which view to detach into",
                     {"Third person", "Third person (front)"}, 0);

    listen<Render2DEvent>(&FreeLook::onRender);
}

bool FreeLook::holdBind() const { return m_hold->value; }

std::string FreeLook::suffix() const {
    if (enabled() && m_hold->value && keybind() == 0)
        return "no bind";
    return {};
}

void FreeLook::onRender(Render2DEvent& event) {
    (void)event;

    auto& ctx = sdk::Context::get();
    const bool want =
        ctx.worldInteractive() && ctx.localPlayer &&
        (!m_hold->value || (keybind() != 0 && input::InputManager::get().isDown(keybind())));

    if (!want) {
        deactivate();
        return;
    }

    void* options = gameOptions();
    if (!options)
        return;

    const int chosen = kPerspective[m_mode->value];

    if (!g_active.load(std::memory_order_relaxed)) {
        const Vec2 rot = ctx.localPlayer->rot();
        g_frozenPitch.store(rot.x, std::memory_order_relaxed);
        g_frozenYaw.store(rot.y, std::memory_order_relaxed);
        g_camPitch.store(rot.x, std::memory_order_relaxed);
        g_camYaw.store(rot.y, std::memory_order_relaxed);
        g_localPlayer.store(ctx.localPlayer, std::memory_order_relaxed);
        g_savedPerspective.store(getPerspective(options), std::memory_order_relaxed);
        setPerspective(options, chosen);
        g_active.store(true, std::memory_order_relaxed);
    } else {
        g_localPlayer.store(ctx.localPlayer, std::memory_order_relaxed);
        setPerspective(options, chosen);
    }
}

void FreeLook::onDisable() { deactivate(); }

}
