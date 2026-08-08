#include "Module/Modules/NoRender.h"

#include "Utils/Obfusc.h"

namespace aerial::modules {
namespace {

void reconcile(BytePatch& patch, bool want) {
    if (want)
        patch.apply();
    else
        patch.revert();
}

}

NoRender::NoRender()
    : Module("NoRender", "Hides world clutter that gets in the way during fights",
             Category::Visuals),
      m_nameTagsPatch(
          AERIAL_STR("48 8B C4 57 41 54 41 55 41 56 41 57 48 83 EC 70 48 C7 40 C0 FE FF FF FF"),
          {0xC3}),
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
}

}
