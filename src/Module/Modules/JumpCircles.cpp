#include "Module/Modules/JumpCircles.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Event/Events.h"
#include "GUI/Theme.h"
#include "Hooks/HookRegistry.h"
#include "Render/Projection.h"
#include "SDK/Context.h"
#include "SDK/Entity.h"
#include "SDK/Offsets.h"
#include "Utils/Hook.h"
#include "Utils/Memory.h"

namespace aerial::modules {
namespace {

Detour<void(__fastcall*)(void*)> g_jump;
std::atomic<bool> g_on{false};
std::atomic<uint64_t> g_jumpSeq{0};

void __fastcall onJump(void* self) {
    if (g_on.load(std::memory_order_relaxed) && self == sdk::Context::get().localPlayer)
        g_jumpSeq.fetch_add(1, std::memory_order_relaxed);
    g_jump.call(self);
}

bool install() {
    g_jump.attach("Player::jumpFromGround", memory::rva(offsets::func::Player_jumpFromGround),
                  &onJump);
    return true;
}

const hooks::Installer g_installer{"JumpCircles", &install};

const std::vector<Vec2>& unitCircle(int segments) {
    static std::vector<Vec2> cache;
    static int cached = -1;
    if (cached != segments) {
        cache.clear();
        cache.reserve(static_cast<size_t>(segments) + 1);
        for (int i = 0; i <= segments; ++i) {
            const float a = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * kPi;
            cache.push_back({std::cos(a), std::sin(a)});
        }
        cached = segments;
    }
    return cache;
}

void drawRing(const render::Camera& cam, const Vec3& centre, float radius, const Colour& colour,
              float thickness, int segments) {
    const std::vector<Vec2>& circle = unitCircle(segments);
    for (size_t i = 1; i < circle.size(); ++i) {
        const Vec3 a{centre.x + circle[i - 1].x * radius, centre.y,
                     centre.z + circle[i - 1].y * radius};
        const Vec3 b{centre.x + circle[i].x * radius, centre.y, centre.z + circle[i].y * radius};
        render::drawLine3D(cam, a, b, colour, thickness);
    }
}

float easeOut(float t) {
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

}

JumpCircles::JumpCircles()
    : Module("JumpCircles", "Emits an animated ring from your feet when you jump",
             Category::Visuals) {
    m_colour = addColour("Colour", "Ring colour", Colour::rgb(0x6C8CFF));
    m_rainbow = addBool("Rainbow", "Cycle the ring colour", false);
    m_radius = addFloat("Radius", "How far the rings expand", 1.3f, 0.3f, 4.0f, 0.05f);
    m_rings = addInt("Rings", "How many rings each jump emits", 3, 1, 6);
    m_duration = addFloat("Duration", "How long a ring lives (seconds)", 0.55f, 0.2f, 1.5f, 0.05f);
    m_thickness = addFloat("Thickness", "Line thickness", 1.8f, 0.5f, 4.0f, 0.1f);
    m_segments = addInt("Smoothness", "Circle segment count", 40, 12, 72);
    m_height = addFloat("Height", "Lift above the ground", 0.03f, 0.0f, 1.0f, 0.01f);

    listen<Render2DEvent>(&JumpCircles::onRender);
}

void JumpCircles::onRender(Render2DEvent& event) {
    auto& ctx = sdk::Context::get();
    auto* player = ctx.localPlayer;
    if (!player || !ctx.worldInteractive()) {
        m_ripples.clear();
        m_lastSeq = g_jumpSeq.load(std::memory_order_relaxed);
        return;
    }

    const uint64_t seq = g_jumpSeq.load(std::memory_order_relaxed);
    if (seq != m_lastSeq) {
        m_lastSeq = seq;
        if (m_ripples.size() < 16) {
            const Vec3 p = player->pos();
            const float feetY = player->worldAABB().min.y;
            m_ripples.push_back({Vec3{p.x, feetY, p.z}, 0.0f});
        }
    }

    const float dt = gui::frameDelta();
    const int rings = m_rings->value;
    const float lifetime = m_duration->value;
    const float ringDelay = lifetime * 0.16f;
    const float total = lifetime + ringDelay * static_cast<float>(rings - 1);

    for (Ripple& r : m_ripples)
        r.age += dt;
    std::erase_if(m_ripples, [&](const Ripple& r) { return r.age > total; });

    if (m_ripples.empty())
        return;

    render::Camera cam;
    if (!cam.build(event.screenSize))
        return;

    const auto& theme = gui::Theme::get();
    const float maxRadius = m_radius->value;
    const float thickness = m_thickness->value;
    const int segments = m_segments->value;
    const float lift = m_height->value;

    for (const Ripple& r : m_ripples) {
        const Vec3 centre{r.origin.x, r.origin.y + lift, r.origin.z};
        for (int k = 0; k < rings; ++k) {
            const float a = r.age - static_cast<float>(k) * ringDelay;
            if (a < 0.0f || a > lifetime)
                continue;
            const float t = a / lifetime;
            const float radius = maxRadius * easeOut(t);
            const float fade = (1.0f - t) * (1.0f - t);
            const Colour base = m_rainbow->value ? theme.rainbowAt(k * 3) : m_colour->value;
            drawRing(cam, centre, radius, base.withAlpha(base.a * fade), thickness, segments);
        }
    }
}

void JumpCircles::onEnable() { g_on.store(true, std::memory_order_relaxed); }

void JumpCircles::onDisable() {
    g_on.store(false, std::memory_order_relaxed);
    m_ripples.clear();
}

}
