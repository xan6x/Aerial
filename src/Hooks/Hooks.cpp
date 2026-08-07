#include "Hooks/Hooks.h"

#include <intrin.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "GUI/Theme.h"

#include "Event/Events.h"
#include "GUI/ClickGui.h"
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
Detour<void(__fastcall*)(void*)> g_tickBuildAction;
Detour<void(__fastcall*)(void*, void*)> g_moveInputTick;
// (this, events*) - the second argument is only visible at the call site, not
// in the prologue. Declaring one argument here handed the original a garbage
// RDX and crashed the game on the first frame.
Detour<void(__fastcall*)(void*, void*)> g_processEvents;

// Captured from its own hook so the movement state can also be cleared from the
// player tick, which does not always run after MoveInputHandler::tick.
void* g_moveInputHandler = nullptr;

// Zeroes the small state fields MoveInputHandler::tick maintains. The qword at
// +8 that the game's own clearMovementState also zeroes is a pointer, and
// clearing it every tick made the game fault once input resumed, so it is left
// alone.
void clearMovementState(void* handler) {
    if (!handler || !memory::isReadable(handler, 0x80))
        return;

    namespace field = offsets::field::moveInput;
    auto* bytes = static_cast<uint8_t*>(handler);

    bytes[field::flagA] = 0;
    *reinterpret_cast<uint16_t*>(bytes + field::direction) = 0;
    bytes[field::flagB] = 0;
    *reinterpret_cast<uint32_t*>(bytes + field::state) = 0;

    // The amounts are what actually reach the player. Clearing only the byte
    // state left these standing, which is why movement still leaked through.
    *reinterpret_cast<float*>(bytes + field::amountX) = 0.0f;
    *reinterpret_cast<float*>(bytes + field::amountY) = 0.0f;
}
Detour<void(__fastcall*)(void*, void*, void*)> g_attack;
Detour<void(__fastcall*)(void*, void*)> g_packetSend;
Detour<void(__fastcall*)(void*)> g_leaveGame;
Detour<float(__fastcall*)(void*, uint32_t)> g_getFloatOption;

// setupFog reads rcx, rdx and r8, and its only call site sets no stack
// arguments. The return is declared and forwarded rather than dropped: if the
// function does return something, handing back the trampoline's own value keeps
// the caller seeing exactly what it would have.
Detour<uintptr_t(__fastcall*)(void*, void*, void*, void*)> g_setupFog;

std::atomic<uint32_t> g_scaledOptionId{0};
std::atomic<float> g_optionMultiplier{1.0f};
std::atomic<bool> g_optionLogging{false};

Detour<void(__fastcall*)(void*, float, float, float)> g_matrixTranslate;

std::atomic<bool> g_itemPhysics{false};
std::atomic<float> g_itemSpin{120.0f};
std::atomic<float> g_itemTilt{90.0f};
std::atomic<float> g_itemLift{0.3f};

std::atomic<bool> g_fogEnabled{false};
std::atomic<float> g_fogRed{1.0f};
std::atomic<float> g_fogGreen{1.0f};
std::atomic<float> g_fogBlue{1.0f};

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

    render::DrawUtils::beginFrame();
    guarded("Render2DEvent", [] {
        Render2DEvent event;
        event.context = Context::get().screenContext;
        event.screenSize = render::DrawUtils::screenSize();
        EventBus::get().dispatch(event);
    });

    t_drawingOverlay = false;
}

// ── MinecraftGame::updateGraphics ────────────────────────────────────────────
// Fallback draw point. With the Direct2D overlay attached the frame is drawn
// from Present instead, which is later still and gives real antialiasing, so
// this path only runs when Direct2D could not attach.
void __fastcall onUpdateGraphics(void* self, void* a2) {
    g_updateGraphics.call(self, a2);

    if (!render::DrawUtils::usingD2D())
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

    // The player tick does not always follow MoveInputHandler::tick, so the
    // movement state is cleared from both ends while the menu is open.
    if (gui::ClickGui::get().isOpen())
        clearMovementState(g_moveInputHandler);

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

// ── MoveInputHandler::tick ───────────────────────────────────────────────────
// Runs normally, then the movement it produced is wiped while the menu is open.
// Letting the pass run and discarding its result keeps the input state machine
// consistent - the alternative, skipping the whole input pass, froze the state
// instead of clearing it.
//
// Only the small state fields the tick itself maintains are touched. The game's
// own clearMovementState additionally zeroes the qword at +8, which is a
// pointer: calling it every tick rather than at the safe points the game uses
// left that pointer null, and the game faulted on it as soon as input resumed.
void __fastcall onMoveInputTick(void* self, void* a2) {
    g_moveInputHandler = self;

    g_moveInputTick.call(self, a2);

    if (gui::ClickGui::get().isOpen())
        clearMovementState(self);
}

// ── ScreenView::_processEvents ───────────────────────────────────────────────
// The game's UI event pump. Skipping it while the menu is open stops clicks
// from reaching the screen underneath - without this, pressing a button in the
// client menu also pressed whatever game button happened to sit behind it.
void __fastcall onProcessEvents(void* self, void* events) {
    if (gui::ClickGui::get().isOpen())
        return;
    g_processEvents.call(self, events);
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

// ── Options::getFloat ────────────────────────────────────────────────────────
// Every float setting comes through here, keyed by a 32-bit option id, so the
// multiplier is applied to exactly one id rather than to whatever happens to be
// read. The logging path exists to find that id: it names each option once and
// reports later changes, so moving a slider in the game's settings identifies
// the option behind it.
float __fastcall onGetFloatOption(void* options, uint32_t id) {
    const float value = g_getFloatOption.call(options, id);

    if (g_optionLogging.load(std::memory_order_relaxed)) {
        static std::mutex mutex;
        static std::unordered_map<uint32_t, float> seen;

        // A dragged slider produces hundreds of changes a second, and more than
        // one options object reads the same id, so the two alternate. Cap the
        // output: the point is to name an id, not to trace every read.
        static int reported = 0;
        constexpr int kMaxReports = 200;

        std::lock_guard lock(mutex);
        const auto it = seen.find(id);
        if (it == seen.end()) {
            if (seen.size() < 128) {
                seen.emplace(id, value);
                LOG_INFO("Options", "id {:#010x} = {:.4f}", id, value);
            }
        } else if (it->second != value) {
            if (reported < kMaxReports) {
                ++reported;
                LOG_INFO("Options", "id {:#010x} changed {:.4f} -> {:.4f}", id, it->second, value);
                if (reported == kMaxReports)
                    LOG_INFO("Options", "further changes will not be reported");
            }
            it->second = value;
        }
    }

    const uint32_t scaled = g_scaledOptionId.load(std::memory_order_relaxed);
    if (scaled != 0 && id == scaled)
        return value * g_optionMultiplier.load(std::memory_order_relaxed);

    return value;
}

// ── Matrix::translate ────────────────────────────────────────────────────────
// Dropped items are camera-facing sprites in this build: ItemRenderer::render
// contains no rotation at all, only this one translate, and the quad corners
// that follow are constants. So the orientation has to be introduced here, into
// the matrix the sprite is then drawn with.
//
// The renderer itself is virtual and has no call site, so its argument count is
// unknowable and it cannot be hooked safely. It does not need to be: the return
// address of this translate identifies the caller exactly, and translate's own
// signature was read off its body.
//
// The matrix is 16 floats in column-major order, the convention the game's own
// maths uses, so a rotation is applied by combining columns.
void rotateColumns(float* m, int columnA, int columnB, float degrees) {
    const float radians = degrees * kDeg2Rad;
    const float c = std::cos(radians);
    const float s = std::sin(radians);

    float* a = m + columnA * 4;
    float* b = m + columnB * 4;

    for (int i = 0; i < 4; ++i) {
        const float av = a[i];
        const float bv = b[i];
        a[i] = av * c + bv * s;
        b[i] = -av * s + bv * c;
    }
}

void __fastcall onMatrixTranslate(void* matrix, float x, float y, float z) {
    if (!g_itemPhysics.load(std::memory_order_relaxed)) {
        g_matrixTranslate.call(matrix, x, y, z);
        return;
    }

    // Everything in the world goes through this function; only the item
    // renderer's own call is ours to change.
    const bool isItem = reinterpret_cast<uintptr_t>(_ReturnAddress()) ==
                        memory::rva(offsets::func::ItemRenderer_translateReturn);

    g_matrixTranslate.call(matrix, x, y + (isItem ? g_itemLift.load(std::memory_order_relaxed) : 0.0f),
                           z);
    if (!isItem || !memory::isReadable(matrix, 64))
        return;

    auto* m = static_cast<float*>(matrix);

    // Items are desynchronised by their position rather than by per-item state:
    // there is no identity to key a map on here, and a resting item's position
    // is stable, which is exactly when a fixed phase matters.
    const float phase = std::floor(x) * 37.0f + std::floor(z) * 17.0f;
    const float spin = std::fmod(gui::clockSeconds() * g_itemSpin.load(std::memory_order_relaxed) +
                                     phase,
                                 360.0f);

    rotateColumns(m, 0, 2, spin);                                       // yaw
    rotateColumns(m, 1, 2, g_itemTilt.load(std::memory_order_relaxed)); // lay it over
}

// ── LevelRendererCamera::setupFog ────────────────────────────────────────────
// The colour is overwritten after the fact rather than by patching the maths:
// setupFog picks between several branches - underwater, lava, the biome table -
// and every one of them lands on the same field, so writing it once here covers
// all of them.
uintptr_t __fastcall onSetupFog(void* self, void* a2, void* a3, void* a4) {
    const uintptr_t result = g_setupFog.call(self, a2, a3, a4);

    if (!g_fogEnabled.load(std::memory_order_relaxed))
        return result;

    constexpr ptrdiff_t kOffset = offsets::field::levelRendererCamera::fogColour;
    if (!memory::isReadable(self, kOffset + sizeof(float) * 4))
        return result;

    auto* colour = reinterpret_cast<float*>(static_cast<uint8_t*>(self) + kOffset);
    colour[0] = g_fogRed.load(std::memory_order_relaxed);
    colour[1] = g_fogGreen.load(std::memory_order_relaxed);
    colour[2] = g_fogBlue.load(std::memory_order_relaxed);
    // The fourth float is left as the game set it - it is not an opacity the
    // module has any business guessing at.

    return result;
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

void* moveInputHandler() { return g_moveInputHandler; }

void setOptionScale(uint32_t optionId, float multiplier) {
    g_scaledOptionId.store(optionId, std::memory_order_relaxed);
    g_optionMultiplier.store(multiplier, std::memory_order_relaxed);
}

void setOptionLogging(bool enabled) {
    g_optionLogging.store(enabled, std::memory_order_relaxed);
}

void setItemPhysics(bool enabled, float spin, float tilt, float lift) {
    g_itemSpin.store(spin, std::memory_order_relaxed);
    g_itemTilt.store(tilt, std::memory_order_relaxed);
    g_itemLift.store(lift, std::memory_order_relaxed);
    g_itemPhysics.store(enabled, std::memory_order_relaxed);
}

void setFogColour(bool enabled, float red, float green, float blue) {
    g_fogRed.store(red, std::memory_order_relaxed);
    g_fogGreen.store(green, std::memory_order_relaxed);
    g_fogBlue.store(blue, std::memory_order_relaxed);
    g_fogEnabled.store(enabled, std::memory_order_relaxed);
}

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
    g_moveInputTick.attach("MoveInputHandler::tick", memory::rva(func::MoveInputHandler_tick),
                           &onMoveInputTick);
    g_processEvents.attach("ScreenView::_processEvents",
                           memory::rva(func::ScreenView_processEvents), &onProcessEvents);
    g_getFloatOption.attach("Options::getFloat", memory::rva(func::Options_getFloat),
                            &onGetFloatOption);
    g_setupFog.attach("LevelRendererCamera::setupFog",
                      memory::rva(func::LevelRendererCamera_setupFog), &onSetupFog);
    g_matrixTranslate.attach("Matrix::translate", memory::rva(func::Matrix_translate),
                             &onMatrixTranslate);

    // Preferred draw path. If DXGI cannot be reached the client keeps using the
    // game renderer through onUpdateGraphics.
    auto& overlay = render::D2DOverlay::get();
    overlay.setFrameCallback([] { dispatchOverlay(); });
    if (!overlay.install())
        LOG_WARN("Hooks", "Direct2D overlay unavailable: {}", overlay.status());

    return HookManager::get().enableAll();
}

void removeAll() { HookManager::get().shutdown(); }

} // namespace aerial::hooks
