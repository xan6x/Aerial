#include "Module/Modules/ItemPhysics.h"

#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "Event/Events.h"
#include "GUI/Theme.h"
#include "Hooks/HookRegistry.h"
#include "SDK/Offsets.h"
#include "Utils/Hook.h"
#include "Utils/Math.h"
#include "Utils/Memory.h"

namespace aerial::modules {
namespace {

namespace func = offsets::func;

Detour<void(__fastcall*)(void*, void*, void*, float, float)> g_itemRender;
Detour<void(__fastcall*)(void*, float, float, float)> g_matrixTranslate;
Detour<void(__fastcall*)(void*, float, float, float, float)> g_matrixRotate;
Detour<float(__fastcall*)(void*)> g_getShadowRadius;

std::atomic<bool> g_enabled{false};
std::atomic<float> g_spin{240.0f};
std::atomic<float> g_lift{0.3f};
std::atomic<float> g_pivot{0.0f};
std::atomic<int> g_thickness{1};
std::atomic<bool> g_smooth{true};
std::atomic<bool> g_preserve{false};
std::atomic<bool> g_flat{false};
std::atomic<bool> g_noShadow{false};

std::atomic<int> g_itemRendererId{-1};

thread_local int t_pass = 0;
thread_local void* t_actor = nullptr;

constexpr float kLayerStep = 0.006f;

struct Spin {
    float yaw = 0.0f;
    float roll = 0.0f;
    float direction = 1.0f;
    float lastSeen = 0.0f;
};

std::mutex g_spinMutex;
std::unordered_map<const void*, Spin> g_spins;

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

    if (g_flat.load(std::memory_order_relaxed))
        resting = true;

    const float now = gui::clockSeconds();
    const float speed = g_spin.load(std::memory_order_relaxed);
    const bool smooth = g_smooth.load(std::memory_order_relaxed);
    const bool preserve = g_preserve.load(std::memory_order_relaxed);

    float yaw = 0.0f;
    float roll = 0.0f;
    {
        std::lock_guard lock(g_spinMutex);
        auto [it, inserted] = g_spins.try_emplace(actor);
        Spin& state = it->second;

        if (inserted) {

            const auto bits = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(actor) >> 4);
            state.yaw = static_cast<float>(bits % 360u);
            state.direction = (bits & 0x40u) ? -1.0f : 1.0f;
            state.lastSeen = now;
        }

        const float delta = t_pass == 0 ? std::clamp(now - state.lastSeen, 0.0f, 0.1f) : 0.0f;
        if (t_pass == 0)
            state.lastSeen = now;

        if (!resting) {
            state.yaw = std::fmod(state.yaw + state.direction * speed * delta + 360.0f, 360.0f);
        } else if (!preserve) {
            const float targetYaw = isBlock ? 90.0f : 180.0f;
            const float targetRoll = isBlock ? 174.0f : 0.0f;

            if (smooth && !g_flat.load(std::memory_order_relaxed)) {

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
        const float pivot = g_pivot.load(std::memory_order_relaxed);
        if (pivot != 0.0f)
            translateLocalY(m, pivot);
    }
}

void __fastcall onItemRender(void* self, void* actor, void* pos, float a4, float partialTicks) {
    void* const previous = t_actor;
    t_actor = actor;

    if (actor && memory::isReadable(actor, offsets::field::entity::rendererId + 4)) {
        g_itemRendererId.store(*reinterpret_cast<const int*>(static_cast<const uint8_t*>(actor) +
                                                             offsets::field::entity::rendererId),
                               std::memory_order_relaxed);
    }

    bool* noBob = nullptr;
    bool saved = false;

    if (g_enabled.load(std::memory_order_relaxed) && actor &&
        memory::isReadable(actor, offsets::field::itemActor::noBob + 1)) {
        noBob = reinterpret_cast<bool*>(static_cast<uint8_t*>(actor) +
                                        offsets::field::itemActor::noBob);
        saved = *noBob;
        *noBob = true;
    }

    const int passes = g_enabled.load(std::memory_order_relaxed)
                           ? std::clamp(g_thickness.load(std::memory_order_relaxed), 1, 8)
                           : 1;

    const int previousPass = t_pass;
    for (int pass = 0; pass < passes; ++pass) {
        t_pass = pass;
        g_itemRender.call(self, actor, pos, a4, partialTicks);
    }
    t_pass = previousPass;

    if (noBob)
        *noBob = saved;

    t_actor = previous;
}

void __fastcall onMatrixTranslate(void* matrix, float x, float y, float z) {
    if (!g_enabled.load(std::memory_order_relaxed)) {
        g_matrixTranslate.call(matrix, x, y, z);
        return;
    }

    const bool isItem = reinterpret_cast<uintptr_t>(_ReturnAddress()) ==
                        memory::rva(func::ItemRenderer_translateReturn);

    const void* actor = isItem ? t_actor : nullptr;
    float lift = 0.0f;
    if (isItem) {
        namespace field = offsets::field;
        const bool isBlock =
            actor && memory::isReadable(actor, field::itemActor::itemStack + field::itemStack::block + 8) &&
            *reinterpret_cast<void* const*>(static_cast<const uint8_t*>(actor) +
                                            field::itemActor::itemStack + field::itemStack::block) !=
                nullptr;
        if (!isBlock)
            lift = g_lift.load(std::memory_order_relaxed);

        lift += static_cast<float>(t_pass) * kLayerStep;
    }

    g_matrixTranslate.call(matrix, x, y + lift, z);

    if (!isItem || !actor || !memory::isReadable(matrix, 64))
        return;

    applyItemRotation(static_cast<float*>(matrix), actor);
}

void __fastcall onMatrixRotate(void* matrix, float angle, float x, float y, float z) {
    if (g_enabled.load(std::memory_order_relaxed)) {
        const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
        if (caller == memory::rva(func::ItemRenderer_billboardReturn) ||
            caller == memory::rva(func::ItemRenderer_spinReturn))
            return;
    }

    g_matrixRotate.call(matrix, angle, x, y, z);
}

float __fastcall onGetShadowRadius(void* self) {
    if (g_enabled.load(std::memory_order_relaxed) && g_noShadow.load(std::memory_order_relaxed) &&
        self) {
        const int itemId = g_itemRendererId.load(std::memory_order_relaxed);
        if (itemId >= 0 && memory::isReadable(self, offsets::field::entity::rendererId + 4) &&
            *reinterpret_cast<const int*>(static_cast<const uint8_t*>(self) +
                                          offsets::field::entity::rendererId) == itemId)
            return 0.0f;
    }

    return g_getShadowRadius.call(self);
}

bool install() {
    g_matrixTranslate.attach("Matrix::translate", memory::rva(func::Matrix_translate),
                             &onMatrixTranslate);
    g_itemRender.attach("ItemRenderer::render", memory::rva(func::ItemRenderer_render),
                        &onItemRender);
    g_matrixRotate.attach("Matrix::rotate", memory::rva(func::Matrix_rotate), &onMatrixRotate);
    g_getShadowRadius.attach("Entity::getShadowRadius", memory::rva(func::Entity_getShadowRadius),
                             &onGetShadowRadius);
    return true;
}

const hooks::Installer g_installer{"ItemPhysics", &install};

void sweepSpins(float now) {
    std::lock_guard lock(g_spinMutex);
    for (auto it = g_spins.begin(); it != g_spins.end();)
        it = (now - it->second.lastSeen > 2.0f) ? g_spins.erase(it) : std::next(it);
}

}

ItemPhysics::ItemPhysics()
    : Module("ItemPhysics", "Dropped items spin as they fall and settle when they land",
             Category::Visuals) {
    m_spin = addFloat("Speed", "Degrees per second while falling", 240.0f, 0.0f, 720.0f, 10.0f);

    m_thickness = addInt("Thickness", "How solid dropped items look", 6, 1, 12);

    m_lift = addFloat("Ground offset", "Height above the block, non-block items only", -0.12f,
                      -0.5f, 0.6f, 0.01f);

    m_noShadow = addBool("No shadow", "Hide the blob shadow under dropped items", true);

    m_pivot = addFloat("Pivot", "Shift along the item's own axis after rotating", 0.0f, -1.0f, 1.0f,
                       0.05f);

    m_smooth = addBool("Smooth landing", "Ease into the resting angle instead of snapping", true);
    m_preserve = addBool("Keep angle", "Leave each item at its own angle instead of aligning them",
                         false);

    m_flat = addBool("Always flat", "Skip the falling spin and draw every item resting", false);

    listen<Render2DEvent>(&ItemPhysics::onRender);
}

void ItemPhysics::onRender(Render2DEvent& event) {
    (void)event;

    g_spin.store(m_spin->value, std::memory_order_relaxed);
    g_lift.store(m_lift->value, std::memory_order_relaxed);
    g_pivot.store(m_pivot->value, std::memory_order_relaxed);
    g_thickness.store(m_thickness->value, std::memory_order_relaxed);
    g_noShadow.store(m_noShadow->value, std::memory_order_relaxed);
    g_smooth.store(m_smooth->value, std::memory_order_relaxed);
    g_preserve.store(m_preserve->value, std::memory_order_relaxed);
    g_flat.store(m_flat->value, std::memory_order_relaxed);
    g_enabled.store(true, std::memory_order_relaxed);

    const float now = gui::clockSeconds();
    if (now - m_lastSweep > 1.0f) {
        m_lastSweep = now;
        sweepSpins(now);
    }
}

void ItemPhysics::onDisable() {
    g_enabled.store(false, std::memory_order_relaxed);

    std::lock_guard lock(g_spinMutex);
    g_spins.clear();
}

}
