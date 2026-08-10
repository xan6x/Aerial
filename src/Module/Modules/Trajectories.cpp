#include "Module/Modules/Trajectories.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "Event/Events.h"
#include "Render/DrawUtils.h"
#include "Render/Projection.h"
#include "SDK/Context.h"
#include "SDK/Entity.h"
#include "SDK/Offsets.h"
#include "Utils/Guard.h"
#include "Utils/Math.h"
#include "Utils/Memory.h"

namespace aerial::modules {
namespace {

namespace func = offsets::func;
namespace field = offsets::field;

struct Preset {
    float speed;
    float gravity;
    float drag;
    float pitchOffset;
};

void* carriedItem(void* player) {
    if (!player)
        return nullptr;
    using Fn = void*(__fastcall*)(void*);
    return reinterpret_cast<Fn>(memory::rva(func::Player_getCarriedItem))(player);
}

std::string readStdString(const void* base) {
    const auto* p = static_cast<const uint8_t*>(base);
    if (!memory::isReadable(p, 0x20))
        return {};
    const size_t size = *reinterpret_cast<const size_t*>(p + 0x10);
    const size_t cap = *reinterpret_cast<const size_t*>(p + 0x18);
    if (size == 0 || size > 256)
        return {};
    const char* data = cap >= 16 ? *reinterpret_cast<const char* const*>(p) : reinterpret_cast<const char*>(p);
    if (!memory::isReadable(data, size))
        return {};
    return std::string(data, size);
}

std::string heldItemName(void* player) {
    void* stack = carriedItem(player);
    if (!stack || !memory::isReadable(stack, field::itemStack::item + sizeof(void*)))
        return {};
    void* item = *reinterpret_cast<void**>(static_cast<uint8_t*>(stack) + field::itemStack::item);
    if (!item || !memory::isReadable(item, 0x58))
        return {};
    return readStdString(static_cast<uint8_t*>(item) + 0x38);
}

void* derefAt(void* p, ptrdiff_t off) {
    if (!p || !memory::isReadable(static_cast<uint8_t*>(p) + off, sizeof(void*)))
        return nullptr;
    return *reinterpret_cast<void**>(static_cast<uint8_t*>(p) + off);
}

void* blockSource(void* player) {
    return derefAt(player, 0xD8);
}

bool solidAt(void* bs, const Vec3& p) {
    if (!bs)
        return false;
    struct BlockPos {
        int x, y, z;
    } pos{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)),
          static_cast<int>(std::floor(p.z))};
    uint8_t out[8]{};
    using Fn = void*(__fastcall*)(void*, void*, void*);
    reinterpret_cast<Fn>(memory::rva(func::BlockSource_getBlockID))(bs, out, &pos);
    return out[0] != 0;
}

bool presetFor(const std::string& name, Preset& out) {
    const auto has = [&](const char* s) { return name.find(s) != std::string::npos; };

    if (has("ender_pearl") || has("snowball") || (has("egg") && !has("spawn"))) {
        out = {1.5f, 0.03f, 0.99f, 0.0f};
        return true;
    }
    if (has("splash_potion") || has("lingering_potion")) {
        out = {0.5f, 0.05f, 0.99f, -20.0f};
        return true;
    }
    if (has("experience_bottle")) {
        out = {0.7f, 0.07f, 0.99f, -20.0f};
        return true;
    }
    if (has("bow") && !has("crossbow")) {
        out = {3.0f, 0.05f, 0.99f, 0.0f};
        return true;
    }
    if (has("fishing_rod")) {
        out = {1.0f, 0.03f, 0.92f, 0.0f};
        return true;
    }
    return false;
}

}

Trajectories::Trajectories()
    : Module("Trajectories", "Predicts where thrown projectiles will land", Category::Visuals) {
    m_colour = addColour("Colour", "Trajectory line colour", Colour::rgb(0x6C8CFF));
    m_thickness = addFloat("Thickness", "Line thickness", 1.6f, 0.5f, 4.0f, 0.1f);
    m_steps = addInt("Length", "How many ticks to simulate", 120, 20, 300);
    m_landing = addBool("Landing box", "Draw a marker where it lands", true);
    m_motion = addBool("Add motion", "Include your own velocity", false);

    listen<Render2DEvent>(&Trajectories::onRender);
}

void Trajectories::onRender(Render2DEvent& event) {
    auto& ctx = sdk::Context::get();
    auto* player = ctx.localPlayer;
    if (!player || !ctx.worldInteractive())
        return;

    const std::string name = heldItemName(player);
    if (name.empty())
        return;

    Preset preset;
    if (!presetFor(name, preset))
        return;

    const Vec2 rot = player->rot();
    const Vec2 aim{rot.x + preset.pitchOffset, rot.y};

    Vec3 pos = player->eyePos();
    Vec3 vel = Vec3::fromAngles(aim) * preset.speed;
    if (m_motion->value)
        vel += player->velocity();

    const Vec3 origin = pos;
    const float groundY = player->pos().y;
    const int steps = m_steps->value;
    void* bs = blockSource(player);

    std::vector<Vec3> points;
    points.reserve(static_cast<size_t>(steps) + 1);
    points.push_back(pos);

    const auto simulate = [&] {
        for (int i = 0; i < steps; ++i) {
            pos += vel;
            vel = vel * preset.drag;
            vel.y -= preset.gravity;
            points.push_back(pos);
            if (bs && solidAt(bs, pos))
                break;
            if (vel.y < 0.0f && pos.y <= groundY - 24.0f)
                break;
            if (pos.distanceSquared(origin) > 256.0f * 256.0f)
                break;
        }
    };

    if (bs)
        guarded("trajectory", simulate);
    else
        simulate();

    render::Camera cam;
    if (!cam.build(event.screenSize))
        return;

    const Colour colour = m_colour->value;
    const float thickness = m_thickness->value;

    for (size_t i = 1; i < points.size(); ++i)
        render::drawLine3D(cam, points[i - 1], points[i], colour, thickness);

    if (m_landing->value && points.size() >= 2) {
        const Vec3& end = points.back();
        const float r = 0.22f;
        render::drawBox3D(cam, AABB{end - Vec3{r, r, r}, end + Vec3{r, r, r}}, colour, thickness);
    }
}

}
