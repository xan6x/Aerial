#include "Module/Modules/NoRender.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "SDK/Offsets.h"
#include "Utils/Memory.h"
#include "Utils/Obfusc.h"

namespace aerial::modules {
namespace {

void reconcile(BytePatch& patch, bool want) {
    if (want)
        patch.apply();
    else
        patch.revert();
}

struct EntryRet {
    uintptr_t offset;
    uint8_t original = 0;
    bool applied = false;
};

void reconcile(EntryRet& p, bool want) {
    const uintptr_t addr = memory::rva(p.offset);
    if (want) {
        if (p.applied)
            return;
        p.original = *reinterpret_cast<uint8_t*>(addr);
        const uint8_t ret = 0xC3;
        if (memory::patch(addr, &ret, 1))
            p.applied = true;
    } else if (p.applied) {
        if (memory::patch(addr, &p.original, 1))
            p.applied = false;
    }
}

struct CallNops {
    std::vector<uintptr_t> sites;
    std::vector<std::array<uint8_t, 5>> original;
    bool applied = false;
};

void reconcile(CallNops& p, bool want) {
    if (want) {
        if (p.applied)
            return;
        p.original.resize(p.sites.size());
        for (size_t i = 0; i < p.sites.size(); ++i) {
            const uintptr_t addr = memory::rva(p.sites[i]);
            std::memcpy(p.original[i].data(), reinterpret_cast<void*>(addr), 5);
            const uint8_t nops[5] = {0x90, 0x90, 0x90, 0x90, 0x90};
            memory::patch(addr, nops, 5);
        }
        p.applied = true;
    } else if (p.applied) {
        for (size_t i = 0; i < p.sites.size(); ++i)
            memory::patch(memory::rva(p.sites[i]), p.original[i].data(), 5);
        p.applied = false;
    }
}

EntryRet g_starsPatch{offsets::func::LevelRendererCamera_renderStars};
CallNops g_sunMoonPatch{{0x5AD575, 0x5AD5E0, 0x5AD64D}};

}

NoRender::NoRender()
    : Module("NoRender", "Hides world clutter that gets in the way during fights",
             Category::Visuals),
      m_nameTagsPatch(
          AERIAL_STR("0F 28 DE 4C 8B C0 48 8B D6 49 8B CE E8 F4 C4 FA FF"),
          {0x0F, 0x28, 0xDE, 0x4C, 0x8B, 0xC0, 0x48, 0x8B, 0xD6, 0x49, 0x8B, 0xCE, 0x90, 0x90,
           0x90, 0x90, 0x90}),
      m_flamePatch(AERIAL_STR("0F 84 9F 01 00 00 F3 41 0F 10 BE BC 00 00 00"),
                   {0xE9, 0xA0, 0x01, 0x00, 0x00, 0x90}),
      m_particlesPatch(AERIAL_STR("40 53 48 83 EC 30 F3 0F 10 02 48 8B D9 F3 0F 11 05"), {0xC3}),
      m_particlesBlendPatch(
          AERIAL_STR("48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 50 F3 0F 10 02 48 8B F9"), {0xC3}),
      m_armorPatch(AERIAL_STR("40 53 56 41 54 41 57 48 81 EC 88 00 00 00 0F 29 74 24 50"), {0xC3}),
      m_cloudsPatch(AERIAL_STR("48 8B C4 57 48 81 EC C0 00 00 00 48 C7 40 80 FE FF FF FF"), {0xC3}) {
    m_nameTags = addBool("Name tags", "Hide entity name tags", false);
    m_flame = addBool("Flame", "Hide the fire that covers the screen while you burn", false);
    m_particles = addBool("Particles", "Hide particle effects", false);
    m_armor = addBool("Armor", "Hide armor worn on players and mobs", false);
    m_clouds = addBool("Clouds", "Hide clouds", false);
    m_stars = addBool("Stars", "Hide the night sky stars", false);
    m_sunMoon = addBool("Sun or Moon", "Hide the sun and the moon", false);

    listenAlways<TickEvent>(&NoRender::onTick);
}

void NoRender::onTick(TickEvent&) { sync(); }

void NoRender::onDisable() { sync(); }

void NoRender::sync() {
    const bool on = enabled();
    reconcile(m_nameTagsPatch, on && m_nameTags->value);
    reconcile(m_flamePatch, on && m_flame->value);

    const bool particles = on && m_particles->value;
    reconcile(m_particlesPatch, particles);
    reconcile(m_particlesBlendPatch, particles);

    reconcile(m_armorPatch, on && m_armor->value);
    reconcile(m_cloudsPatch, on && m_clouds->value);

    reconcile(g_starsPatch, on && m_stars->value);
    reconcile(g_sunMoonPatch, on && m_sunMoon->value);
}

}
