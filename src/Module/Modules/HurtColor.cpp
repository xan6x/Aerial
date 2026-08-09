#include "Module/Modules/HurtColor.h"

#include <atomic>
#include <cstdint>

#include "Event/Events.h"
#include "Hooks/HookRegistry.h"
#include "SDK/Offsets.h"
#include "Utils/Hook.h"
#include "Utils/Memory.h"

namespace aerial::modules {
namespace {

namespace func = offsets::func;
namespace ef = offsets::field::entity;

struct Vec4 {
    float r, g, b, a;
};

Detour<Vec4*(__fastcall*)(void*, Vec4*, void*)> g_getOverlayColor;

std::atomic<bool> g_enabled{false};
std::atomic<float> g_red{1.0f};
std::atomic<float> g_green{1.0f};
std::atomic<float> g_blue{1.0f};
std::atomic<float> g_alpha{1.0f};

Vec4* __fastcall onGetOverlayColor(void* self, Vec4* out, void* entity) {
    Vec4* result = g_getOverlayColor.call(self, out, entity);

    if (!g_enabled.load(std::memory_order_relaxed) || !out || !entity)
        return result;

    if (out->a <= 0.001f)
        return result;

    if (!memory::isReadable(entity, ef::flags + sizeof(uint32_t)))
        return result;

    const uint32_t flags = *reinterpret_cast<const uint32_t*>(static_cast<uint8_t*>(entity) + ef::flags);
    if (!((flags >> 1) & 1u))
        return result;

    out->r = g_red.load(std::memory_order_relaxed);
    out->g = g_green.load(std::memory_order_relaxed);
    out->b = g_blue.load(std::memory_order_relaxed);
    out->a *= g_alpha.load(std::memory_order_relaxed);

    return result;
}

bool install() {
    g_getOverlayColor.attach("EntityShaderManager::getOverlayColor",
                             memory::rva(func::EntityShaderManager_getOverlayColor),
                             &onGetOverlayColor);
    return true;
}

const hooks::Installer g_installer{"HurtColor", &install};

}

HurtColor::HurtColor()
    : Module("HurtColor", "Recolours the flash entities show when they take damage",
             Category::Visuals) {
    m_colour = addColour("Colour", "The hurt flash colour and intensity",
                         Colour(1.0f, 0.0f, 0.0f, 1.0f));

    listen<Render2DEvent>(&HurtColor::onRender);
}

void HurtColor::onRender(Render2DEvent& event) {
    (void)event;

    const Colour colour = m_colour->value;
    g_red.store(colour.r, std::memory_order_relaxed);
    g_green.store(colour.g, std::memory_order_relaxed);
    g_blue.store(colour.b, std::memory_order_relaxed);
    g_alpha.store(colour.a, std::memory_order_relaxed);
    g_enabled.store(enabled(), std::memory_order_relaxed);
}

void HurtColor::onDisable() { g_enabled.store(false, std::memory_order_relaxed); }

}
