#include "Module/Modules/FogColor.h"

#include <atomic>
#include <cmath>
#include <cstdint>

#include "Event/Events.h"
#include "GUI/Theme.h"
#include "Hooks/HookRegistry.h"
#include "SDK/Offsets.h"
#include "Utils/Hook.h"
#include "Utils/Memory.h"

namespace aerial::modules {
namespace {

namespace func = offsets::func;

struct Preset {
    const char* name;
    Colour colour;
};

constexpr Preset kPresets[] = {
    {"Custom", Colour::rgb(0x6C8CFF)}, {"Purple", Colour::rgb(0x7B4BD8)},
    {"Blood", Colour::rgb(0x8C1B1B)},  {"Ocean", Colour::rgb(0x1B6B8C)},
    {"Void", Colour::rgb(0x0A0A12)},   {"Sunset", Colour::rgb(0xE0733A)},
    {"Toxic", Colour::rgb(0x4BD84B)},  {"White", Colour::rgb(0xE8ECF5)},
};

Detour<uintptr_t(__fastcall*)(void*, void*, void*, void*)> g_setupFog;

std::atomic<bool> g_enabled{false};
std::atomic<float> g_red{1.0f};
std::atomic<float> g_green{1.0f};
std::atomic<float> g_blue{1.0f};

uintptr_t __fastcall onSetupFog(void* self, void* a2, void* a3, void* a4) {
    const uintptr_t result = g_setupFog.call(self, a2, a3, a4);

    if (!g_enabled.load(std::memory_order_relaxed))
        return result;

    constexpr ptrdiff_t kOffset = offsets::field::levelRendererCamera::fogColour;
    if (!memory::isReadable(self, kOffset + sizeof(float) * 4))
        return result;

    auto* colour = reinterpret_cast<float*>(static_cast<uint8_t*>(self) + kOffset);
    colour[0] = g_red.load(std::memory_order_relaxed);
    colour[1] = g_green.load(std::memory_order_relaxed);
    colour[2] = g_blue.load(std::memory_order_relaxed);

    return result;
}

bool install() {
    g_setupFog.attach("LevelRendererCamera::setupFog",
                      memory::rva(func::LevelRendererCamera_setupFog), &onSetupFog);
    return true;
}

const hooks::Installer g_installer{"FogColor", &install};

}

FogColor::FogColor() : Module("FogColor", "Recolours the world's fog", Category::Visuals) {
    std::vector<std::string> names;
    for (const Preset& preset : kPresets)
        names.emplace_back(preset.name);

    m_preset = addEnum("Preset", "Pick a colour, or Custom to use the picker", std::move(names), 1);

    m_custom = addColour("Colour", "Custom fog colour", Colour(0.48f, 0.29f, 0.85f));

    const auto custom = [this] { return m_preset->is("Custom") && !m_rainbow->value; };
    m_custom->onlyIf(custom);

    m_rainbow = addBool("Rainbow", "Cycle through the hues", false);
    m_rainbowSpeed = addFloat("Speed", "Degrees of hue per second", 30.0f, 5.0f, 180.0f, 5.0f);
    m_rainbowSpeed->onlyIf([this] { return m_rainbow->value; });

    m_preset->onlyIf([this] { return !m_rainbow->value; });

    listen<Render2DEvent>(&FogColor::onRender);
}

void FogColor::onRender(Render2DEvent& event) {
    (void)event;

    Colour colour;
    if (m_rainbow->value) {
        const float hue = std::fmod(gui::clockSeconds() * m_rainbowSpeed->value, 360.0f);
        colour = Colour::hsv(hue, 0.65f, 0.85f);
    } else if (m_preset->is("Custom")) {
        colour = m_custom->value;
    } else {
        colour = kPresets[static_cast<size_t>(m_preset->value)].colour;
    }

    g_red.store(colour.r, std::memory_order_relaxed);
    g_green.store(colour.g, std::memory_order_relaxed);
    g_blue.store(colour.b, std::memory_order_relaxed);
    g_enabled.store(true, std::memory_order_relaxed);
}

void FogColor::onDisable() {

    g_enabled.store(false, std::memory_order_relaxed);
}

}
