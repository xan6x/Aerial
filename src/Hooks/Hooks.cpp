#include "Hooks/Hooks.h"

#include <Windows.h>
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
#include "Render/SkyCubemap.h"
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
// Five arguments, taken from the dispatcher's call site rather than the
// prologue - see the item physics section below.
Detour<void(__fastcall*)(void*, void*, void*, float, float)> g_itemRender;
Detour<void(__fastcall*)(void*, float, float, float, float)> g_matrixRotate;
Detour<float(__fastcall*)(void*)> g_getShadowRadius;
Detour<void(__fastcall*)(void*, float, float)> g_renderSky;
// (this, events*) - the second argument is only visible at the call site, not
// in the prologue. Declaring one argument here handed the original a garbage
// RDX and crashed the game on the first frame.
Detour<void(__fastcall*)(void*, void*)> g_processEvents;
// (this, ButtonEvent*, char state, void* a4). Four arguments, read off the one
// call site inside InputHandler::tick - the prologue alone stops at three.
Detour<void(__fastcall*)(void*, void*, char, void*)> g_handleButtonEvent;
Detour<void(__fastcall*)(void*, char, char, short, short, short, short, char)> g_mouseFeed;

// Captured from its own hook so the movement state can also be cleared from the
// player tick, which does not always run after MoveInputHandler::tick.
void* g_moveInputHandler = nullptr;

// Set by the eject thread, consumed by MinecraftGame::update. Once done is set,
// every hook body becomes a pass-through so nothing of ours is still running
// when the detours come off.
std::atomic<bool> g_teardownRequested{false};
std::atomic<bool> g_teardownDone{false};

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

Detour<void(__fastcall*)(void*, float, float, float)> g_matrixTranslate;
Detour<float(__fastcall*)(void*)> g_getGamma;
Detour<float(__fastcall*)(void*, float, bool)> g_getFov;

std::atomic<bool> g_gammaEnabled{false};
std::atomic<float> g_gamma{1.0f};
std::atomic<bool> g_fovEnabled{false};
std::atomic<float> g_fovScale{1.0f};

std::atomic<bool> g_itemPhysics{false};
std::atomic<float> g_itemSpin{240.0f};
std::atomic<float> g_itemLift{0.3f};
std::atomic<float> g_itemPivot{0.0f};
std::atomic<bool> g_itemSmooth{true};
std::atomic<bool> g_itemPreserve{false};
std::atomic<bool> g_itemFlat{false};
std::atomic<int> g_itemThickness{1};
std::atomic<bool> g_itemNoShadow{false};

// Renderer id of a dropped item, learned the first time one is drawn. -1 until
// then, which is why the shadow hook does nothing before any item has appeared.
std::atomic<int> g_itemRendererId{-1};

// Which extrusion pass is being drawn. A flat sprite is invisible edge-on and
// nearly invisible against the ground, so the item is drawn several times a
// fraction of a block apart, which reads as a slab rather than a decal.
thread_local int t_itemPass = 0;

// Gap between passes. Small enough that the layers touch and large enough that
// they do not z-fight; eight passes come to about a Java item's thickness.
constexpr float kItemLayerStep = 0.006f;

// The actor ItemRenderer::render is currently drawing. Matrix::translate has no
// idea which entity it is being called for, and that is the whole reason the
// first version of item physics could not tell one item from another: it keyed
// its spin off the render coordinates, which move when the camera does.
thread_local void* t_itemActor = nullptr;

// Per-item orientation, keyed by actor pointer. The key is never dereferenced,
// only compared, so an entry outliving its entity is harmless - it is dropped by
// the sweep below rather than by trusting the pointer.
struct ItemSpin {
    float yaw = 0.0f;
    float roll = 0.0f;
    float direction = 1.0f;
    float lastSeen = 0.0f;
};

std::mutex g_itemSpinMutex;
std::unordered_map<const void*, ItemSpin> g_itemSpins;

// Defined with the rest of the item physics, further down; the frame hook above
// it is what drives the sweep.
void sweepItemSpins(float now);

std::atomic<bool> g_skybox{false};
std::atomic<bool> g_skyCubemap{false};

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
    // Once the eject handshake has run, nothing of ours draws again: the D2D
    // resources are about to be released and the DLL unmapped.
    if (t_drawingOverlay || g_teardownDone.load(std::memory_order_relaxed))
        return;

    t_drawingOverlay = true;
    g_overlays.fetch_add(1, std::memory_order_relaxed);

    // The single per-drawn-frame marker, whichever backend is in use: both
    // routes into here draw exactly one frame. Everything animated measures
    // itself against the gap this records.
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

// ── MinecraftGame::updateGraphics ────────────────────────────────────────────
// Fallback draw point. With the Direct2D overlay attached the frame is drawn
// from Present instead, which is later still and gives real antialiasing, so
// this path normally stays out of the way.
//
// "Attached" and "drawing" are not the same claim, though, and the difference is
// what made the menu open invisibly on someone else's machine: the client took
// the keyboard and the mouse, the module toggled, and nothing appeared. The
// overlay reports itself ready as soon as it has a device and a target, but
// everything after that - the keyed mutex, the shared texture, the composite
// onto the game's back buffer - can fail quietly on hardware we never see, and
// there is no reason to expect Present to be intercepted at all under an
// injector that maps the DLL differently.
//
// So the test is whether frames are actually coming out of it, not whether it
// says it is there. If the overlay has not drawn for this many game updates the
// game's own renderer takes back over. It is a worse-looking menu, and a menu.
//
// Taking over means telling the overlay to stand down, not merely drawing from
// here instead. DrawUtils asks D2DOverlay::ready() which backend to use, so an
// overlay that still called itself ready went on receiving every fill and every
// glyph from this path too - into a context nothing was compositing. Frames
// were dispatched, the statistics counted them, and the screen stayed empty.
// That is exactly the failure this was written to catch, and it walked straight
// past it.
//
// Generous on purpose. This game happens to run update, updateGraphics and
// Present one to one, but nothing guarantees that: cap the framerate without
// capping the update loop and Present falls behind by a fixed ratio. A tight
// window would read that as a failure and leave both renderers drawing the same
// menu on top of each other forever.
constexpr uint64_t kOverlayStallUpdates = 120;

// The value of g_gameUpdates at the last frame Direct2D drew. Only the Present
// callback sets it - the fallback must not, or it would prove itself alive.
std::atomic<uint64_t> g_lastD2DUpdate{0};

bool direct2DDrawing() {
    const uint64_t updates = g_gameUpdates.load(std::memory_order_relaxed);

    if (!render::DrawUtils::usingD2D()) {
        // Hold the marker at "now" while the overlay is not in use, so the
        // countdown starts from the moment it becomes ready rather than from
        // zero. Without this the first check after the overlay came up compared
        // against an update count from before the client was even injected, and
        // declared a stall roughly twenty milliseconds into a working session.
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

// ── MinecraftGame::update ────────────────────────────────────────────────────
// The one hook that runs in every state - main menu, loading screen and in
// world alike. Keybinds are driven from here so the menu key works before a
// world is ever loaded, and it is the frame marker that arms the overlay.
void __fastcall onGameUpdate(void* self) {
    g_gameUpdates.fetch_add(1, std::memory_order_relaxed);

    // Eject handshake: whoever noticed the key is on another thread entirely,
    // and closing the menu reaches into the game to re-grab the cursor. Do it
    // here, where that is a same-thread call, and stop drawing afterwards.
    if (g_teardownRequested.load(std::memory_order_acquire) &&
        !g_teardownDone.load(std::memory_order_relaxed)) {
        guarded("teardown", [] { gui::ClickGui::get().close(); });
        g_teardownDone.store(true, std::memory_order_release);
    }

    if (!g_teardownDone.load(std::memory_order_relaxed)) {
        render::DrawUtils::beginFrame();
        guarded("input poll", [] { input::InputManager::get().poll(); });

        // Item spin state is keyed by actor pointer, so entries have to be
        // retired when their item stops being drawn - picked up, despawned, or
        // simply out of view.
        static float lastSweep = 0.0f;
        const float now = gui::clockSeconds();
        if (now - lastSweep > 1.0f) {
            lastSweep = now;
            sweepItemSpins(now);
        }
    }

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

// ── InputHandler::_handleButtonEvent ─────────────────────────────────────────
//
// Where the menu actually takes the game's input away, and the only place that
// can be done properly.
//
// Everything tried before this was downstream of the problem. Cancelling our
// own events only stops our handlers. Nulling the messages in the queue assumed
// the game reads its input from that queue, and a CoreWindow does not have to -
// it clearly does not here, since clicks kept reaching the world through it.
// Releasing the cursor and skipping tickBuildAction stopped a hold from being
// carried forward but not the press that started one.
//
// This is the game's own dispatcher: InputHandler::tick decodes a device record
// and calls this for every button, whatever device it came from and however it
// reached the process. Nothing that a button does in Minecraft - swinging,
// placing, walking, the hotbar, the inventory, chat, pause - happens without
// going through here first.
//
// Presses are dropped and releases are not. A button held when the menu opened
// was already seen going down, so its release has to land or the game keeps
// acting on it: that is the difference between the menu pausing an input and
// the player mining forever after closing it.
void __fastcall onHandleButtonEvent(void* self, void* event, char state, void* a4) {
    if (gui::ClickGui::get().isOpen() && memory::isReadable(event, 3) &&
        static_cast<const uint8_t*>(event)[2] == 1)
        return;

    g_handleButtonEvent.call(self, event, state, a4);
}

// ── MouseDevice::feed ────────────────────────────────────────────────────────
//
// The wheel, at the only point in the process where it exists.
//
// It never reaches a window message: not WM_MOUSEWHEEL, not WM_POINTERWHEEL,
// not folded into WM_INPUT. That is why the menu would not scroll no matter
// what the message hook did, and why the game's own list kept scrolling behind
// it - both sides were reading different things, and only one of them was real.
//
// Button 4 is the wheel and `state` is the notch count, already divided by
// WHEEL_DELTA by whoever called this and signed the usual way round: positive
// is away from the hand.
constexpr char kWheelButton = 4;

void __fastcall onMouseFeed(void* self, char button, char state, short x, short y, short dx,
                            short dy, char a8) {
    if (button == kWheelButton) {
        input::InputManager::get().feedWheel(state);

        // The menu owns the wheel while it is up, so the hotbar does not change
        // under it and the screen behind does not scroll with the list.
        if (gui::ClickGui::get().isOpen())
            return;
    }

    g_mouseFeed.call(self, button, state, x, y, dx, dy, a8);
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
// read.
//
// This used to carry a logging path that named every id it saw and reported
// later changes - the tool that identified the sensitivity option in the first
// place by watching which id moved with the slider. Its only switch was a
// setting on SensMultiplier, and with that gone nothing could turn it on again,
// so it is not left sitting unreachable in the one hook the game calls most.
// SensMultiplier.cpp records what it found.
float __fastcall onGetFloatOption(void* options, uint32_t id) {
    const float value = g_getFloatOption.call(options, id);

    const uint32_t scaled = g_scaledOptionId.load(std::memory_order_relaxed);
    if (scaled != 0 && id == scaled)
        return value * g_optionMultiplier.load(std::memory_order_relaxed);

    return value;
}

// ── Options::getGamma ────────────────────────────────────────────────────────
float __fastcall onGetGamma(void* options) {
    const float value = g_getGamma.call(options);
    return g_gammaEnabled.load(std::memory_order_relaxed)
               ? g_gamma.load(std::memory_order_relaxed)
               : value;
}

// ── LevelRendererPlayer::getFov ──────────────────────────────────────────────
float __fastcall onGetFov(void* self, float partialTicks, bool a3) {
    const float value = g_getFov.call(self, partialTicks, a3);
    return g_fovEnabled.load(std::memory_order_relaxed)
               ? value * g_fovScale.load(std::memory_order_relaxed)
               : value;
}

// ── Item physics ─────────────────────────────────────────────────────────────
// Dropped items are camera-facing sprites in this build: ItemRenderer::render
// contains no rotation at all, only one translate, and the quad corners that
// follow are constants. So the orientation has to be introduced into the matrix
// the sprite is then drawn with, which is what the Matrix::translate hook does.
//
// ItemRenderer::render is hooked purely to learn *which* item is being drawn.
// It is virtual, so its arity was taken from the dispatcher's own call site in
// EntityRenderDispatcher::render (0x55D742): rcx=this, rdx=actor, r8=Vec3*,
// xmm3, then one float at [rsp+0x20] - five arguments, and the body agrees by
// reading the fifth as [rbp+0x140].
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

// Post-multiplied translation along the matrix's own Y axis, so it follows the
// rotation instead of the world. Column 1 is that axis; column 3 is the origin.
void translateLocalY(float* m, float distance) {
    for (int i = 0; i < 4; ++i)
        m[12 + i] += m[4 + i] * distance;
}

// Signed shortest way round from `current` to `target`, in degrees. Settling
// without this takes the long way whenever the item happens to have spun past
// its target, which reads as a sudden flick.
float shortestAngle(float current, float target) {
    return std::fmod(target - current + 540.0f, 360.0f) - 180.0f;
}

// Drops entries for items that have not been drawn recently. Called once a
// second from the frame hook; the map only ever holds items on screen.
void sweepItemSpins(float now) {
    std::lock_guard lock(g_itemSpinMutex);
    for (auto it = g_itemSpins.begin(); it != g_itemSpins.end();)
        it = (now - it->second.lastSeen > 2.0f) ? g_itemSpins.erase(it) : std::next(it);
}

// The orientation for one item this frame. Airborne items spin; grounded ones
// ease to rest, laid flat if they are an item and tipped onto a corner if they
// are a block - which is what the vanilla Java behaviour this imitates does.
void applyItemRotation(float* m, const void* actor) {
    namespace field = offsets::field;

    bool resting = false;
    bool isBlock = false;

    if (memory::isReadable(actor, field::itemActor::itemStack + field::itemStack::block + 8)) {
        const auto* bytes = static_cast<const uint8_t*>(actor);

        // Two independent tests, because a dropped item that has come to rest is
        // not reliably flagged as on the ground - it sits a hair above the block
        // and keeps being re-resolved. A stopped item is a settled item whatever
        // the flag says, and without the velocity test they never stop turning.
        const bool onGround = *reinterpret_cast<const bool*>(bytes + field::entity::onGround);

        const auto* velocity = reinterpret_cast<const float*>(bytes + field::entity::velocity);
        const float speedSquared = velocity[0] * velocity[0] + velocity[1] * velocity[1] +
                                   velocity[2] * velocity[2];

        resting = onGround || speedSquared < 1.0e-4f;

        const auto* stack = bytes + field::itemActor::itemStack;
        isBlock = *reinterpret_cast<void* const*>(stack + field::itemStack::block) != nullptr;
    }

    // "Lie flat" means exactly that: no falling animation at all, the item is
    // drawn resting from the moment it appears.
    if (g_itemFlat.load(std::memory_order_relaxed))
        resting = true;

    const float now = gui::clockSeconds();
    const float speed = g_itemSpin.load(std::memory_order_relaxed);
    const bool smooth = g_itemSmooth.load(std::memory_order_relaxed);
    const bool preserve = g_itemPreserve.load(std::memory_order_relaxed);

    float yaw = 0.0f;
    float roll = 0.0f;
    {
        std::lock_guard lock(g_itemSpinMutex);
        auto [it, inserted] = g_itemSpins.try_emplace(actor);
        ItemSpin& state = it->second;

        if (inserted) {
            // A fresh item gets its own angle and its own direction, so a pile
            // of them does not turn in lockstep. The pointer is the only unique
            // number to hand, and its low bits are as good a seed as any.
            const auto bits = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(actor) >> 4);
            state.yaw = static_cast<float>(bits % 360u);
            state.direction = (bits & 0x40u) ? -1.0f : 1.0f;
            state.lastSeen = now;
        }

        // Per-item, so one item appearing mid-frame cannot inherit another's
        // elapsed time. Clamped because the first frame after a pause would
        // otherwise spin an item through several turns at once. The extrusion
        // passes read the angle but must not advance it, or a thick item would
        // turn as many times faster as it has layers.
        const float delta =
            t_itemPass == 0 ? std::clamp(now - state.lastSeen, 0.0f, 0.1f) : 0.0f;
        if (t_itemPass == 0)
            state.lastSeen = now;

        if (!resting) {
            state.yaw = std::fmod(state.yaw + state.direction * speed * delta + 360.0f, 360.0f);
        } else if (!preserve) {
            const float targetYaw = isBlock ? 90.0f : 180.0f;
            const float targetRoll = isBlock ? 174.0f : 0.0f;

            // Lying flat is a resting state, not a landing, so it snaps: easing
            // in from the seeded angle would show a half turn every time an item
            // came into view.
            if (smooth && !g_itemFlat.load(std::memory_order_relaxed)) {
                // Exponential decay rather than a fixed step: it settles fast at
                // first and creeps in at the end, and it behaves the same at any
                // frame rate.
                const float factor = 1.0f - std::exp(-10.0f * delta);
                state.yaw = std::fmod(state.yaw + shortestAngle(state.yaw, targetYaw) * factor + 360.0f,
                                      360.0f);
                state.roll += (targetRoll - state.roll) * factor;
            } else {
                state.yaw = targetYaw;
                state.roll = targetRoll;
            }
        }

        yaw = state.yaw;
        roll = state.roll;
    }

    // X, then Y, then Z, each post-multiplied. The 90 degrees about X is what
    // lays the sprite down; without it the item stays upright and only turns.
    rotateColumns(m, 1, 2, 90.0f);
    rotateColumns(m, 0, 2, yaw);
    rotateColumns(m, 0, 1, roll);

    if (!isBlock) {
        const float pivot = g_itemPivot.load(std::memory_order_relaxed);
        if (pivot != 0.0f)
            translateLocalY(m, pivot);
    }
}

// ── Matrix::rotate ───────────────────────────────────────────────────────────
// The whole world rotates through here, so this drops out immediately for
// anything that is not one of the item renderer's own two calls. Those two are
// what made the sprite keep facing the camera no matter how it was laid down:
// they run after the translate, so whatever orientation we set was simply turned
// back again a few instructions later.
void __fastcall onMatrixRotate(void* matrix, float angle, float x, float y, float z) {
    if (g_itemPhysics.load(std::memory_order_relaxed)) {
        const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
        if (caller == memory::rva(offsets::func::ItemRenderer_billboardReturn) ||
            caller == memory::rva(offsets::func::ItemRenderer_spinReturn))
            return;
    }

    g_matrixRotate.call(matrix, angle, x, y, z);
}

// ── Entity::getShadowRadius ──────────────────────────────────────────────────
// A blob shadow under a thin item that is itself lying on the ground reads as a
// smear of dirt, and it is drawn after ItemRenderer::render has returned, so it
// cannot be suppressed from there. Zero radius is the game's own way of saying
// "no shadow", so that is what dropped items are told to have.
float __fastcall onGetShadowRadius(void* self) {
    if (g_itemPhysics.load(std::memory_order_relaxed) &&
        g_itemNoShadow.load(std::memory_order_relaxed) && self) {
        const int itemId = g_itemRendererId.load(std::memory_order_relaxed);
        if (itemId >= 0 && memory::isReadable(self, offsets::field::entity::rendererId + 4) &&
            *reinterpret_cast<const int*>(static_cast<const uint8_t*>(self) +
                                          offsets::field::entity::rendererId) == itemId)
            return 0.0f;
    }

    return g_getShadowRadius.call(self);
}

// ── ItemRenderer::render ─────────────────────────────────────────────────────
// Records which actor the translate belongs to, and switches off the game's own
// bob for the duration of the call.
void __fastcall onItemRender(void* self, void* actor, void* pos, float a4, float partialTicks) {
    void* const previous = t_itemActor;
    t_itemActor = actor;

    // Learned here rather than hard-coded: the id is a table index, and this is
    // the one place it is known for certain to belong to a dropped item.
    if (actor && memory::isReadable(actor, offsets::field::entity::rendererId + 4)) {
        g_itemRendererId.store(*reinterpret_cast<const int*>(static_cast<const uint8_t*>(actor) +
                                                             offsets::field::entity::rendererId),
                               std::memory_order_relaxed);
    }

    // The bob is added to Y before the translate we hook, so it cannot be undone
    // afterwards - by then it is indistinguishable from the item's real height.
    // The renderer already has a switch for it, and this is that switch: with it
    // set, both the vertical bob and the angle derived from it come out zero.
    bool* noBob = nullptr;
    bool saved = false;

    if (g_itemPhysics.load(std::memory_order_relaxed) && actor &&
        memory::isReadable(actor, offsets::field::itemActor::noBob + 1)) {
        noBob = reinterpret_cast<bool*>(static_cast<uint8_t*>(actor) +
                                        offsets::field::itemActor::noBob);
        saved = *noBob;
        *noBob = true;
    }

    // Each pass is a full redraw a hair higher than the last. The orientation is
    // only advanced on the first one, so the extra passes cannot spin the item
    // faster than it should turn.
    const int passes = g_itemPhysics.load(std::memory_order_relaxed)
                           ? std::clamp(g_itemThickness.load(std::memory_order_relaxed), 1, 8)
                           : 1;

    const int previousPass = t_itemPass;
    for (int pass = 0; pass < passes; ++pass) {
        t_itemPass = pass;
        g_itemRender.call(self, actor, pos, a4, partialTicks);
    }
    t_itemPass = previousPass;

    // Restored rather than left set: it is the game's field, and it is read
    // outside rendering too.
    if (noBob)
        *noBob = saved;

    t_itemActor = previous;
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

    // Blocks keep their own height: the offset exists to lift a flat sprite off
    // the ground, and a cube does not need it.
    const void* actor = isItem ? t_itemActor : nullptr;
    float lift = 0.0f;
    if (isItem) {
        namespace field = offsets::field;
        const bool isBlock =
            actor && memory::isReadable(actor, field::itemActor::itemStack + field::itemStack::block + 8) &&
            *reinterpret_cast<void* const*>(static_cast<const uint8_t*>(actor) +
                                            field::itemActor::itemStack + field::itemStack::block) !=
                nullptr;
        if (!isBlock)
            lift = g_itemLift.load(std::memory_order_relaxed);

        // Laid flat, the sprite's own normal is world up, so stacking the passes
        // along Y is what gives the item its thickness.
        lift += static_cast<float>(t_itemPass) * kItemLayerStep;
    }

    g_matrixTranslate.call(matrix, x, y + lift, z);

    if (!isItem || !actor || !memory::isReadable(matrix, 64))
        return;

    applyItemRotation(static_cast<float*>(matrix), actor);
}

// ── LevelRendererCamera::renderSky ───────────────────────────────────────────
// The Skybox module patches the dimension branch so the End's textured cube is
// what gets drawn everywhere. Two things that patch cannot express are done
// here instead.
//
// First the tint. The cube is drawn in the fog colour doubled, which in the End
// is near-black and gives that dim purple sky. In the overworld the fog colour
// is the biome's, so the texture would be stained by it and go dark at night.
// Feeding the renderer 0.5 makes the doubling land on exactly white, and the
// texture shows as it was authored.
//
// Then the sky. The End branch returns before the sun, moon and stars, so they
// are re-issued afterwards, with the arguments taken from the pair renderSky
// itself passes on - which are not in the order the signatures suggest.
void __fastcall onRenderSky(void* self, float a, float b) {
    if (!g_skybox.load(std::memory_order_relaxed)) {
        g_renderSky.call(self, a, b);
        return;
    }

    const bool cubemap = g_skyCubemap.load(std::memory_order_relaxed);

    // The End branch doubles this field and hands it to the shader as the tint
    // its cube is drawn in, so it decides what the cube looks like.
    //
    // Half brightness when the pack's own end_sky is what gets drawn - that is
    // the picture, and the biome fog colour would stain it.
    //
    // Zero when a cubemap is going over the top, which turns the cube black.
    // The material carrying the cubemap adds its colour to what is already
    // there rather than replacing it, so whatever the cube drew shows through
    // the pack's sky and shifts every colour in it - worst where the pack's own
    // image is darkest, which is where the End's starfield came through as a
    // wedge. Adding to black is just the image.
    constexpr ptrdiff_t kOffset = offsets::field::levelRendererCamera::fogColour;
    float* colour = nullptr;
    float saved[4]{};

    if (memory::isReadable(self, kOffset + sizeof(float) * 4)) {
        colour = reinterpret_cast<float*>(static_cast<uint8_t*>(self) + kOffset);
        for (int i = 0; i < 4; ++i) {
            saved[i] = colour[i];
            colour[i] = cubemap ? 0.0f : 0.5f;
        }
    }

    g_renderSky.call(self, a, b);

    if (colour)
        for (int i = 0; i < 4; ++i)
            colour[i] = saved[i];

    using SunOrMoon = void(__fastcall*)(void*, float, bool);
    using Stars = void(__fastcall*)(void*, float, float);
    auto sunOrMoon = reinterpret_cast<SunOrMoon>(memory::rva(func::LevelRendererCamera_renderSunOrMoon));
    auto stars = reinterpret_cast<Stars>(memory::rva(func::LevelRendererCamera_renderStars));

    sunOrMoon(self, b, true);
    sunOrMoon(self, b, false);
    stars(self, b, a);

    // Last, over everything: a pack's own six-face cubemap, when it has one.
    // Loading is attempted from here rather than from the module toggle because
    // it needs a live ClientInstance to reach the texture group, and the menu
    // can be opened before there is one.
    if (cubemap) {
        auto& sky = render::SkyCubemap::get();
        if (sky.ready() || sky.load())
            sky.draw(self);
    }
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

void setGammaOverride(bool enabled, float gamma) {
    g_gamma.store(gamma, std::memory_order_relaxed);
    g_gammaEnabled.store(enabled, std::memory_order_relaxed);
}

void setFovScale(bool enabled, float scale) {
    g_fovScale.store(scale, std::memory_order_relaxed);
    g_fovEnabled.store(enabled, std::memory_order_relaxed);
}

void setItemPhysics(bool enabled, float spin, float lift, float pivot, int thickness, bool smooth,
                    bool preserve, bool flat, bool noShadow) {
    g_itemSpin.store(spin, std::memory_order_relaxed);
    g_itemLift.store(lift, std::memory_order_relaxed);
    g_itemPivot.store(pivot, std::memory_order_relaxed);
    g_itemThickness.store(thickness, std::memory_order_relaxed);
    g_itemNoShadow.store(noShadow, std::memory_order_relaxed);
    g_itemSmooth.store(smooth, std::memory_order_relaxed);
    g_itemPreserve.store(preserve, std::memory_order_relaxed);
    g_itemFlat.store(flat, std::memory_order_relaxed);
    g_itemPhysics.store(enabled, std::memory_order_relaxed);

    if (!enabled) {
        std::lock_guard lock(g_itemSpinMutex);
        g_itemSpins.clear();
    }
}

void setSkybox(bool enabled) { g_skybox.store(enabled, std::memory_order_relaxed); }

void setSkyCubemap(bool enabled) {
    g_skyCubemap.store(enabled, std::memory_order_relaxed);
    if (!enabled)
        render::SkyCubemap::get().unload();
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
    g_handleButtonEvent.attach("InputHandler::_handleButtonEvent",
                               memory::rva(func::InputHandler_handleButtonEvent),
                               &onHandleButtonEvent);
    g_mouseFeed.attach("MouseDevice::feed", memory::rva(func::MouseDevice_feed), &onMouseFeed);
    g_getFloatOption.attach("Options::getFloat", memory::rva(func::Options_getFloat),
                            &onGetFloatOption);
    g_renderSky.attach("LevelRendererCamera::renderSky",
                       memory::rva(func::LevelRendererCamera_renderSky), &onRenderSky);
    g_setupFog.attach("LevelRendererCamera::setupFog",
                      memory::rva(func::LevelRendererCamera_setupFog), &onSetupFog);
    g_matrixTranslate.attach("Matrix::translate", memory::rva(func::Matrix_translate),
                             &onMatrixTranslate);
    g_itemRender.attach("ItemRenderer::render", memory::rva(func::ItemRenderer_render),
                        &onItemRender);
    g_matrixRotate.attach("Matrix::rotate", memory::rva(func::Matrix_rotate), &onMatrixRotate);
    g_getShadowRadius.attach("Entity::getShadowRadius", memory::rva(func::Entity_getShadowRadius),
                             &onGetShadowRadius);
    g_getGamma.attach("Options::getGamma", memory::rva(func::Options_getGamma), &onGetGamma);
    g_getFov.attach("LevelRendererPlayer::getFov", memory::rva(func::LevelRendererPlayer_getFov),
                    &onGetFov);

    // Preferred draw path. If DXGI cannot be reached the client keeps using the
    // game renderer through onUpdateGraphics.
    auto& overlay = render::D2DOverlay::get();
    overlay.setFrameCallback([] {
        // Proof of life for direct2DDrawing(). Set before the frame, not after,
        // so a draw that faults still counts as Present having reached us.
        g_lastD2DUpdate.store(g_gameUpdates.load(std::memory_order_relaxed),
                              std::memory_order_relaxed);
        dispatchOverlay();
    });
    if (!overlay.install())
        LOG_WARN("Hooks", "Direct2D overlay unavailable: {}", overlay.status());

    return HookManager::get().enableAll();
}

bool requestTeardown(unsigned timeoutMs) {
    g_teardownRequested.store(true, std::memory_order_release);

    // Poll rather than wait on an event: the game may not be updating at all
    // (minimised, or already gone), and hanging the eject would be worse than
    // tearing down from the wrong thread.
    for (unsigned waited = 0; waited < timeoutMs; waited += 5) {
        if (g_teardownDone.load(std::memory_order_acquire))
            return true;
        Sleep(5);
    }

    LOG_WARN("Hooks", "teardown handshake timed out after {} ms", timeoutMs);
    return false;
}

void removeAll() {
    // Switch the overlay off before the detours come off. A plain flag rather
    // than clearing the callback: reassigning a std::function that Present may
    // be reading on another thread is exactly the race this is trying to avoid.
    render::D2DOverlay::get().setEnabled(false);
    HookManager::get().shutdown();
}

} // namespace aerial::hooks
