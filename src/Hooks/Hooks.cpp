#include "Hooks/Hooks.h"

#include <atomic>
#include <cstdint>

#include "Event/Events.h"
#include "GUI/ClickGui.h"
#include "Input/InputManager.h"
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
Detour<void(__fastcall*)(void*)> g_tickBuildAction;
Detour<void(__fastcall*)(void*, void*, void*)> g_attack;
Detour<void(__fastcall*)(void*, void*)> g_packetSend;
Detour<void(__fastcall*)(void*)> g_leaveGame;

std::atomic<uint64_t> g_renderFrames{0};
std::atomic<uint64_t> g_playerTicks{0};
std::atomic<uint64_t> g_gameUpdates{0};
std::atomic<uint64_t> g_overlays{0};

// ── Where the overlay is drawn ───────────────────────────────────────────────
// Two earlier attempts were wrong, and both failure modes were informative:
//
//   * around InGamePlayScreen::render - that pass is purely 3D (its UI vtable
//     slots are null stubs), so the overlay was rendered in world space and
//     showed up floating behind the player in third person;
//   * on a ScreenRenderer::fill counter - the count is not stable enough frame
//     to frame, so the overlay landed early in the frame and was painted over
//     by the world in a world, though it still showed in the main menu.
//
// MinecraftGame::update runs the frame as startFrame -> Minecraft::update ->
// updateGraphics -> endFrame, and updateGraphics is where all rendering
// happens - the 3D world first, the UI last. Drawing at the end of it is
// therefore both after everything else and still inside the UI's 2D state.
thread_local bool t_drawingOverlay = false;

void dispatchOverlay() {
    if (t_drawingOverlay)
        return;

    t_drawingOverlay = true;
    g_overlays.fetch_add(1, std::memory_order_relaxed);

    guarded("Render2DEvent", [] {
        Render2DEvent event;
        event.context = Context::get().screenContext;
        event.screenSize = render::DrawUtils::screenSize();
        EventBus::get().dispatch(event);
    });

    t_drawingOverlay = false;
}

// ── MinecraftGame::updateGraphics ────────────────────────────────────────────
void __fastcall onUpdateGraphics(void* self, void* a2) {
    g_updateGraphics.call(self, a2);
    dispatchOverlay();
}

// ── MinecraftGame::update ────────────────────────────────────────────────────
// The one hook that runs in every state - main menu, loading screen and in
// world alike. Keybinds are driven from here so the menu key works before a
// world is ever loaded, and it is the frame marker that arms the overlay.
void __fastcall onGameUpdate(void* self) {
    g_gameUpdates.fetch_add(1, std::memory_order_relaxed);

    render::DrawUtils::beginFrame();
    guarded("input poll", [] { input::InputManager::get().poll(); });

    g_gameUpdate.call(self);
}

// ── ClientInstance::onTick ───────────────────────────────────────────────────
// The earliest place a ClientInstance pointer is available, and the anchor for
// everything reached through it (game, font, GUI data).
void __fastcall onClientTick(void* self, int a2, int a3) {
    auto& context = Context::get();
    if (context.client != self) {
        context.client = static_cast<ClientInstance*>(self);
        LOG_INFO("Hooks", "ClientInstance @ {}", self);
    }
    g_clientTick.call(self, a2, a3);
}

// ── LocalPlayer::normalTick ──────────────────────────────────────────────────
void __fastcall onPlayerTick(void* self) {
    g_playerTicks.fetch_add(1, std::memory_order_relaxed);

    auto& context = Context::get();
    auto* player = static_cast<LocalPlayer*>(self);

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

// ── GameMode::tick ───────────────────────────────────────────────────────────
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

// ── Level::tick ──────────────────────────────────────────────────────────────
void __fastcall onLevelTick(void* self) {
    Context::get().level = static_cast<Level*>(self);
    g_levelTick.call(self);
}

// ── InGamePlayScreen::render ─────────────────────────────────────────────────
// Runs on the render thread with the UI tessellator set up, which is what makes
// ScreenRenderer::fill and Font::drawCached safe to call here.
uintptr_t __fastcall onScreenRender(void* self, void* screenContext) {
    g_renderFrames.fetch_add(1, std::memory_order_relaxed);

    auto& context = Context::get();
    context.screenContext = static_cast<ScreenContext*>(screenContext);

    // Screen+0x30 is the ClientInstance; a cheap way to stay in sync even if
    // the tick hook has not run yet.
    if (!context.client && memory::isReadable(self, offsets::field::screen::clientInstance + 8))
        context.client = fieldAt<ClientInstance*>(self, offsets::field::screen::clientInstance);

    // Nothing is drawn here: this pass is 3D. It only captures the context the
    // overlay later reports to modules.
    return g_screenRender.call(self, screenContext);
}

// ── ClientInstance::tickBuildAction ──────────────────────────────────────────
// Drives the per-tick attack/build action from the held mouse buttons. Skipping
// it while the menu is open stops clicks in the GUI from reaching the world -
// releasing the cursor alone does not, since the game keeps acting on the
// button state.
void __fastcall onTickBuildAction(void* self) {
    if (gui::ClickGui::get().isOpen())
        return;
    g_tickBuildAction.call(self);
}

// ── GameMode::attack ─────────────────────────────────────────────────────────
void __fastcall onAttack(void* self, void* player, void* target) {
    Context::get().gameMode = static_cast<GameMode*>(self);

    // Belt and braces: anything that still slips through while the menu is open
    // must not land a hit.
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

// ── LoopbackPacketSender::send ───────────────────────────────────────────────
// send(this, Packet* packet) — the packet is in RDX.
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

// ── ClientInstance::leaveGame ────────────────────────────────────────────────
void __fastcall onLeaveGame(void* self) {
    LOG_INFO("Hooks", "leaving world");
    WorldLeaveEvent event;
    EventBus::get().dispatch(event);
    Context::get().reset();

    g_leaveGame.call(self);
}

} // namespace

uint64_t renderFrameCount() { return g_renderFrames.load(std::memory_order_relaxed); }

uint64_t playerTickCount() { return g_playerTicks.load(std::memory_order_relaxed); }

uint64_t gameUpdateCount() { return g_gameUpdates.load(std::memory_order_relaxed); }

uint64_t overlayCount() { return g_overlays.load(std::memory_order_relaxed); }

bool installAll() {
    if (!HookManager::get().init())
        return false;

    const bool critical =
        g_gameUpdate.attach("MinecraftGame::update", memory::rva(func::MinecraftGame_update),
                            &onGameUpdate) &&
        g_playerTick.attach("LocalPlayer::normalTick", memory::rva(func::LocalPlayer_normalTick),
                            &onPlayerTick) &&
        g_screenRender.attach("InGamePlayScreen::render", memory::rva(func::InGamePlayScreen_render),
                              &onScreenRender);

    if (!critical) {
        LOG_ERROR("Hooks", "a critical hook failed; aborting");
        return false;
    }

    // Non-critical: the client degrades gracefully without them.
    g_clientTick.attach("ClientInstance::onTick", memory::rva(func::ClientInstance_onTick), &onClientTick);
    g_gameModeTick.attach("GameMode::tick", memory::rva(func::GameMode_tick), &onGameModeTick);
    g_levelTick.attach("Level::tick", memory::rva(func::Level_tick), &onLevelTick);
    g_attack.attach("GameMode::attack", memory::rva(func::GameMode_attack), &onAttack);
    g_packetSend.attach("LoopbackPacketSender::send", memory::rva(func::LoopbackPacketSender_send),
                        &onPacketSend);
    g_leaveGame.attach("ClientInstance::leaveGame", memory::rva(func::ClientInstance_leaveGame),
                       &onLeaveGame);
    g_updateGraphics.attach("MinecraftGame::updateGraphics",
                            memory::rva(func::MinecraftGame_updateGraphics), &onUpdateGraphics);
    g_tickBuildAction.attach("ClientInstance::tickBuildAction",
                             memory::rva(func::ClientInstance_tickBuildAction), &onTickBuildAction);

    return HookManager::get().enableAll();
}

void removeAll() { HookManager::get().shutdown(); }

} // namespace aerial::hooks
