#include <Windows.h>

#include <atomic>
#include <cstdint>

#include "Event/Events.h"
#include "GUI/ClickGui.h"
#include "GUI/Theme.h"
#include "Hooks/HookRegistry.h"
#include "Hooks/Hooks.h"
#include "Hooks/InputHooks.h"
#include "Input/InputManager.h"
#include "Render/D2DOverlay.h"
#include "Render/DrawUtils.h"
#include "SDK/ClientInstance.h"
#include "SDK/Context.h"
#include "SDK/Level.h"
#include "SDK/Offsets.h"
#include "SDK/Types.h"
#include "Utils/Guard.h"
#include "Utils/Hook.h"
#include "Utils/Logger.h"
#include "Utils/Memory.h"

namespace aerial::hooks {
namespace {

using namespace aerial::sdk;
namespace func = offsets::func;

Detour<void(__fastcall*)(void*)> g_gameUpdate;
Detour<void(__fastcall*)(void*, int, int)> g_clientTick;
Detour<void(__fastcall*)(void*)> g_playerTick;
Detour<void(__fastcall*)(void*)> g_gameModeTick;
Detour<void(__fastcall*)(void*)> g_levelTick;
Detour<uintptr_t(__fastcall*)(void*, void*)> g_screenRender;
Detour<void(__fastcall*)(void*, void*)> g_updateGraphics;
Detour<void(__fastcall*)(void*, void*, void*)> g_attack;
Detour<void(__fastcall*)(void*, void*)> g_packetSend;
Detour<void(__fastcall*)(void*)> g_leaveGame;

std::atomic<uint64_t> g_renderFrames{0};
std::atomic<uint64_t> g_playerTicks{0};
std::atomic<uint64_t> g_gameUpdates{0};
std::atomic<uint64_t> g_overlays{0};

std::atomic<bool> g_teardownRequested{false};
std::atomic<bool> g_teardownDone{false};

thread_local bool t_drawingOverlay = false;

void dispatchOverlay() {

    if (t_drawingOverlay || g_teardownDone.load(std::memory_order_relaxed))
        return;

    t_drawingOverlay = true;
    g_overlays.fetch_add(1, std::memory_order_relaxed);

    gui::advanceFrame();

    render::DrawUtils::beginFrame();
    guarded("Render2DEvent", [] {
        Render2DEvent event;
        event.context = Context::get().screenContext;
        event.screenSize = render::DrawUtils::screenSize();
        EventBus::get().dispatch(event);
    });

    t_drawingOverlay = false;
}

constexpr uint64_t kOverlayStallUpdates = 120;

std::atomic<uint64_t> g_lastD2DUpdate{0};

bool direct2DDrawing() {
    const uint64_t updates = g_gameUpdates.load(std::memory_order_relaxed);

    if (!render::DrawUtils::usingD2D()) {

        g_lastD2DUpdate.store(updates, std::memory_order_relaxed);
        return false;
    }

    const uint64_t last = g_lastD2DUpdate.load(std::memory_order_relaxed);
    if (updates <= last + kOverlayStallUpdates)
        return true;

    LOG_WARN("Hooks", "Direct2D has not drawn a frame in {} updates while reporting '{}'",
             updates - last, render::D2DOverlay::get().status());
    render::D2DOverlay::get().abandon("Present intercepted but no frames came out of it");
    return false;
}

void __fastcall onUpdateGraphics(void* self, void* a2) {
    g_updateGraphics.call(self, a2);

    if (!direct2DDrawing())
        dispatchOverlay();
}

void __fastcall onGameUpdate(void* self) {
    g_gameUpdates.fetch_add(1, std::memory_order_relaxed);

    if (g_teardownRequested.load(std::memory_order_acquire) &&
        !g_teardownDone.load(std::memory_order_relaxed)) {
        guarded("teardown", [] { gui::ClickGui::get().close(); });
        g_teardownDone.store(true, std::memory_order_release);
    }

    if (!g_teardownDone.load(std::memory_order_relaxed)) {
        render::DrawUtils::beginFrame();
        guarded("input poll", [] { input::InputManager::get().poll(); });
        guarded("deferred grab", [] { replayDeferredGrab(); });
        guarded("cursor heal", [] { healMouseGrab(); });
    }

    g_gameUpdate.call(self);
}

void __fastcall onClientTick(void* self, int a2, int a3) {
    auto& context = Context::get();
    if (context.client != self) {
        context.client = static_cast<ClientInstance*>(self);
        LOG_INFO("Hooks", "ClientInstance @ {}", self);
    }
    g_clientTick.call(self, a2, a3);
}

void __fastcall onPlayerTick(void* self) {
    g_playerTicks.fetch_add(1, std::memory_order_relaxed);

    auto& context = Context::get();
    auto* player = static_cast<LocalPlayer*>(self);

    if (gui::ClickGui::get().isOpen())
        clearMovementInput();

    const bool joined = context.localPlayer != player;
    context.localPlayer = player;
    if (!context.client)
        context.client = player->clientInstance();

    if (joined && context.level) {
        guarded("WorldJoinEvent", [&] {
            WorldJoinEvent event;
            event.player = player;
            event.level = context.level;
            EventBus::get().dispatch(event);
        });
    }

    guarded("TickEvent", [&] {
        TickEvent tick;
        tick.player = player;
        EventBus::get().dispatch(tick);
    });

    g_playerTick.call(self);
}

void __fastcall onGameModeTick(void* self) {
    auto& context = Context::get();
    context.gameMode = static_cast<GameMode*>(self);

    if (context.localPlayer) {
        guarded("GameTickEvent", [&] {
            GameTickEvent event;
            event.gameMode = context.gameMode;
            event.player = context.localPlayer;
            EventBus::get().dispatch(event);
        });
    }

    g_gameModeTick.call(self);
}

void __fastcall onLevelTick(void* self) {
    Context::get().level = static_cast<Level*>(self);
    g_levelTick.call(self);
}

uintptr_t __fastcall onScreenRender(void* self, void* screenContext) {
    g_renderFrames.fetch_add(1, std::memory_order_relaxed);

    auto& context = Context::get();
    context.screenContext = static_cast<ScreenContext*>(screenContext);

    if (!context.client && memory::isReadable(self, offsets::field::screen::clientInstance + 8))
        context.client = fieldAt<ClientInstance*>(self, offsets::field::screen::clientInstance);

    return g_screenRender.call(self, screenContext);
}

void __fastcall onAttack(void* self, void* player, void* target) {
    Context::get().gameMode = static_cast<GameMode*>(self);

    if (gui::ClickGui::get().isOpen())
        return;

    AttackEvent event;
    event.player = static_cast<LocalPlayer*>(player);
    event.target = static_cast<Entity*>(target);
    guarded("AttackEvent", [&] { EventBus::get().dispatch(event); });

    if (event.isCancelled())
        return;

    g_attack.call(self, player, event.target);
}

void __fastcall onPacketSend(void* self, void* packet) {
    PacketSendEvent event;
    event.packet = static_cast<Packet*>(packet);

    guarded("PacketSendEvent", [&] {
        if (packet && memory::isReadable(packet, 8))
            event.packetId = callVirtual<int>(packet, offsets::vidx::packet::getId);
        EventBus::get().dispatch(event);
    });

    if (event.isCancelled())
        return;

    g_packetSend.call(self, packet);
}

void __fastcall onLeaveGame(void* self) {
    LOG_INFO("Hooks", "leaving world");
    WorldLeaveEvent event;
    EventBus::get().dispatch(event);
    Context::get().reset();

    g_leaveGame.call(self);
}

bool install() {
    const bool critical =
        g_gameUpdate.attach("MinecraftGame::update", memory::rva(func::MinecraftGame_update),
                            &onGameUpdate) &&
        g_playerTick.attach("LocalPlayer::normalTick", memory::rva(func::LocalPlayer_normalTick),
                            &onPlayerTick) &&
        g_screenRender.attach("InGamePlayScreen::render", memory::rva(func::InGamePlayScreen_render),
                              &onScreenRender);

    if (!critical)
        return false;

    g_clientTick.attach("ClientInstance::onTick", memory::rva(func::ClientInstance_onTick),
                        &onClientTick);
    g_gameModeTick.attach("GameMode::tick", memory::rva(func::GameMode_tick), &onGameModeTick);
    g_levelTick.attach("Level::tick", memory::rva(func::Level_tick), &onLevelTick);
    g_attack.attach("GameMode::attack", memory::rva(func::GameMode_attack), &onAttack);
    g_packetSend.attach("LoopbackPacketSender::send", memory::rva(func::LoopbackPacketSender_send),
                        &onPacketSend);
    g_leaveGame.attach("ClientInstance::leaveGame", memory::rva(func::ClientInstance_leaveGame),
                       &onLeaveGame);
    g_updateGraphics.attach("MinecraftGame::updateGraphics",
                            memory::rva(func::MinecraftGame_updateGraphics), &onUpdateGraphics);

    auto& overlay = render::D2DOverlay::get();
    overlay.setFrameCallback([] {

        g_lastD2DUpdate.store(g_gameUpdates.load(std::memory_order_relaxed),
                              std::memory_order_relaxed);
        dispatchOverlay();
    });
    if (!overlay.install())
        LOG_WARN("Hooks", "Direct2D overlay unavailable: {}", overlay.status());

    return true;
}

const Installer g_installer{"Game", &install};

}

uint64_t renderFrameCount() { return g_renderFrames.load(std::memory_order_relaxed); }

uint64_t playerTickCount() { return g_playerTicks.load(std::memory_order_relaxed); }

uint64_t gameUpdateCount() { return g_gameUpdates.load(std::memory_order_relaxed); }

uint64_t overlayCount() { return g_overlays.load(std::memory_order_relaxed); }

bool requestTeardown(unsigned timeoutMs) {
    g_teardownRequested.store(true, std::memory_order_release);

    for (unsigned waited = 0; waited < timeoutMs; waited += 5) {
        if (g_teardownDone.load(std::memory_order_acquire))
            return true;
        Sleep(5);
    }

    LOG_WARN("Hooks", "teardown handshake timed out after {} ms", timeoutMs);
    return false;
}

}
