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

Detour<void(__fastcall*)(void*, void*, void*, float, float)> g_itemRender;
Detour<void(__fastcall*)(void*, float, float, float, float)> g_matrixRotate;
Detour<float(__fastcall*)(void*)> g_getShadowRadius;
Detour<void(__fastcall*)(void*, float, float)> g_renderSky;

Detour<void(__fastcall*)(void*, void*)> g_processEvents;

Detour<void(__fastcall*)(void*, void*, char, void*)> g_handleButtonEvent;
Detour<void(__fastcall*)(void*, char, char, short, short, short, short, char)> g_mouseFeed;

void* g_moveInputHandler = nullptr;

std::atomic<bool> g_teardownRequested{false};
std::atomic<bool> g_teardownDone{false};

void clearMovementState(void* handler) {
    if (!handler || !memory::isReadable(handler, 0x80))
        return;

    namespace field = offsets::field::moveInput;
    auto* bytes = static_cast<uint8_t*>(handler);

    bytes[field::flagA] = 0;
    *reinterpret_cast<uint16_t*>(bytes + field::direction) = 0;
    bytes[field::flagB] = 0;
    *reinterpret_cast<uint32_t*>(bytes + field::state) = 0;

    *reinterpret_cast<float*>(bytes + field::amountX) = 0.0f;
    *reinterpret_cast<float*>(bytes + field::amountY) = 0.0f;
}
Detour<void(__fastcall*)(void*, void*, void*)> g_attack;
Detour<void(__fastcall*)(void*, void*)> g_packetSend;
Detour<void(__fastcall*)(void*)> g_leaveGame;
Detour<float(__fastcall*)(void*, uint32_t)> g_getFloatOption;

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

std::atomic<int> g_itemRendererId{-1};

thread_local int t_itemPass = 0;

constexpr float kItemLayerStep = 0.006f;

thread_local void* t_itemActor = nullptr;

struct ItemSpin {
    float yaw = 0.0f;
    float roll = 0.0f;
    float direction = 1.0f;
    float lastSeen = 0.0f;
};

std::mutex g_itemSpinMutex;
std::unordered_map<const void*, ItemSpin> g_itemSpins;

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

        static float lastSweep = 0.0f;
        const float now = gui::clockSeconds();
        if (now - lastSweep > 1.0f) {
            lastSweep = now;
            sweepItemSpins(now);
        }
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

void __fastcall onMoveInputTick(void* self, void* a2) {
    g_moveInputHandler = self;

    g_moveInputTick.call(self, a2);

    if (gui::ClickGui::get().isOpen())
        clearMovementState(self);
}

void __fastcall onProcessEvents(void* self, void* events) {
    if (gui::ClickGui::get().isOpen())
        return;
    g_processEvents.call(self, events);
}

void __fastcall onHandleButtonEvent(void* self, void* event, char state, void* a4) {
    // event[2] is 1 on press, 0 on release. Releases must pass through or a
    // button held when the menu opened stays held for the game.
    if (gui::ClickGui::get().isOpen() && memory::isReadable(event, 3) &&
        static_cast<const uint8_t*>(event)[2] == 1)
        return;

    g_handleButtonEvent.call(self, event, state, a4);
}

constexpr char kWheelButton = 4;

void __fastcall onMouseFeed(void* self, char button, char state, short x, short y, short dx,
                            short dy, char a8) {
    if (button == kWheelButton) {
        input::InputManager::get().feedWheel(state);

        if (gui::ClickGui::get().isOpen())
            return;
    }

    g_mouseFeed.call(self, button, state, x, y, dx, dy, a8);
}

void __fastcall onTickBuildAction(void* self) {
    if (gui::ClickGui::get().isOpen())
        return;
    g_tickBuildAction.call(self);
}

float __fastcall onGetFloatOption(void* options, uint32_t id) {
    const float value = g_getFloatOption.call(options, id);

    const uint32_t scaled = g_scaledOptionId.load(std::memory_order_relaxed);
    if (scaled != 0 && id == scaled)
        return value * g_optionMultiplier.load(std::memory_order_relaxed);

    return value;
}

float __fastcall onGetGamma(void* options) {
    const float value = g_getGamma.call(options);
    return g_gammaEnabled.load(std::memory_order_relaxed)
               ? g_gamma.load(std::memory_order_relaxed)
               : value;
}

float __fastcall onGetFov(void* self, float partialTicks, bool a3) {
    const float value = g_getFov.call(self, partialTicks, a3);
    return g_fovEnabled.load(std::memory_order_relaxed)
               ? value * g_fovScale.load(std::memory_order_relaxed)
               : value;
}

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

void translateLocalY(float* m, float distance) {
    for (int i = 0; i < 4; ++i)
        m[12 + i] += m[4 + i] * distance;
}

float shortestAngle(float current, float target) {
    return std::fmod(target - current + 540.0f, 360.0f) - 180.0f;
}

void sweepItemSpins(float now) {
    std::lock_guard lock(g_itemSpinMutex);
    for (auto it = g_itemSpins.begin(); it != g_itemSpins.end();)
        it = (now - it->second.lastSeen > 2.0f) ? g_itemSpins.erase(it) : std::next(it);
}

void applyItemRotation(float* m, const void* actor) {
    namespace field = offsets::field;

    bool resting = false;
    bool isBlock = false;

    if (memory::isReadable(actor, field::itemActor::itemStack + field::itemStack::block + 8)) {
        const auto* bytes = static_cast<const uint8_t*>(actor);

        const bool onGround = *reinterpret_cast<const bool*>(bytes + field::entity::onGround);

        const auto* velocity = reinterpret_cast<const float*>(bytes + field::entity::velocity);
        const float speedSquared = velocity[0] * velocity[0] + velocity[1] * velocity[1] +
                                   velocity[2] * velocity[2];

        resting = onGround || speedSquared < 1.0e-4f;

        const auto* stack = bytes + field::itemActor::itemStack;
        isBlock = *reinterpret_cast<void* const*>(stack + field::itemStack::block) != nullptr;
    }

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

            const auto bits = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(actor) >> 4);
            state.yaw = static_cast<float>(bits % 360u);
            state.direction = (bits & 0x40u) ? -1.0f : 1.0f;
            state.lastSeen = now;
        }

        const float delta =
            t_itemPass == 0 ? std::clamp(now - state.lastSeen, 0.0f, 0.1f) : 0.0f;
        if (t_itemPass == 0)
            state.lastSeen = now;

        if (!resting) {
            state.yaw = std::fmod(state.yaw + state.direction * speed * delta + 360.0f, 360.0f);
        } else if (!preserve) {
            const float targetYaw = isBlock ? 90.0f : 180.0f;
            const float targetRoll = isBlock ? 174.0f : 0.0f;

            if (smooth && !g_itemFlat.load(std::memory_order_relaxed)) {

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

    rotateColumns(m, 1, 2, 90.0f);
    rotateColumns(m, 0, 2, yaw);
    rotateColumns(m, 0, 1, roll);

    if (!isBlock) {
        const float pivot = g_itemPivot.load(std::memory_order_relaxed);
        if (pivot != 0.0f)
            translateLocalY(m, pivot);
    }
}

void __fastcall onMatrixRotate(void* matrix, float angle, float x, float y, float z) {
    if (g_itemPhysics.load(std::memory_order_relaxed)) {
        const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
        if (caller == memory::rva(offsets::func::ItemRenderer_billboardReturn) ||
            caller == memory::rva(offsets::func::ItemRenderer_spinReturn))
            return;
    }

    g_matrixRotate.call(matrix, angle, x, y, z);
}

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

void __fastcall onItemRender(void* self, void* actor, void* pos, float a4, float partialTicks) {
    void* const previous = t_itemActor;
    t_itemActor = actor;

    if (actor && memory::isReadable(actor, offsets::field::entity::rendererId + 4)) {
        g_itemRendererId.store(*reinterpret_cast<const int*>(static_cast<const uint8_t*>(actor) +
                                                             offsets::field::entity::rendererId),
                               std::memory_order_relaxed);
    }

    bool* noBob = nullptr;
    bool saved = false;

    if (g_itemPhysics.load(std::memory_order_relaxed) && actor &&
        memory::isReadable(actor, offsets::field::itemActor::noBob + 1)) {
        noBob = reinterpret_cast<bool*>(static_cast<uint8_t*>(actor) +
                                        offsets::field::itemActor::noBob);
        saved = *noBob;
        *noBob = true;
    }

    const int passes = g_itemPhysics.load(std::memory_order_relaxed)
                           ? std::clamp(g_itemThickness.load(std::memory_order_relaxed), 1, 8)
                           : 1;

    const int previousPass = t_itemPass;
    for (int pass = 0; pass < passes; ++pass) {
        t_itemPass = pass;
        g_itemRender.call(self, actor, pos, a4, partialTicks);
    }
    t_itemPass = previousPass;

    if (noBob)
        *noBob = saved;

    t_itemActor = previous;
}

void __fastcall onMatrixTranslate(void* matrix, float x, float y, float z) {
    if (!g_itemPhysics.load(std::memory_order_relaxed)) {
        g_matrixTranslate.call(matrix, x, y, z);
        return;
    }

    const bool isItem = reinterpret_cast<uintptr_t>(_ReturnAddress()) ==
                        memory::rva(offsets::func::ItemRenderer_translateReturn);

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

        lift += static_cast<float>(t_itemPass) * kItemLayerStep;
    }

    g_matrixTranslate.call(matrix, x, y + lift, z);

    if (!isItem || !actor || !memory::isReadable(matrix, 64))
        return;

    applyItemRotation(static_cast<float*>(matrix), actor);
}

void __fastcall onRenderSky(void* self, float a, float b) {
    if (!g_skybox.load(std::memory_order_relaxed)) {
        g_renderSky.call(self, a, b);
        return;
    }

    const bool cubemap = g_skyCubemap.load(std::memory_order_relaxed);

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

    if (cubemap) {
        auto& sky = render::SkyCubemap::get();
        if (sky.ready() || sky.load())
            sky.draw(self);
    }
}

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

    return result;
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

}

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

    auto& overlay = render::D2DOverlay::get();
    overlay.setFrameCallback([] {

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

    for (unsigned waited = 0; waited < timeoutMs; waited += 5) {
        if (g_teardownDone.load(std::memory_order_acquire))
            return true;
        Sleep(5);
    }

    LOG_WARN("Hooks", "teardown handshake timed out after {} ms", timeoutMs);
    return false;
}

void removeAll() {

    render::D2DOverlay::get().setEnabled(false);
    HookManager::get().shutdown();
}

}
