#include "Module/Modules/FogColor.h"

#include <cmath>

#include "Event/Events.h"
#include "GUI/Theme.h"
#include "Hooks/Hooks.h"

namespace aerial::modules {
namespace {

struct Preset {
    const char* name;
    Colour colour;
};

// Named starting points, because picking a colour out of three sliders from
// scratch is tedious. "Custom" leaves the sliders alone.
constexpr Preset kPresets[] = {
    {"Custom", Colour::rgb(0x6C8CFF)}, {"Purple", Colour::rgb(0x7B4BD8)},
    {"Blood", Colour::rgb(0x8C1B1B)},  {"Ocean", Colour::rgb(0x1B6B8C)},
    {"Void", Colour::rgb(0x0A0A12)},   {"Sunset", Colour::rgb(0xE0733A)},
    {"Toxic", Colour::rgb(0x4BD84B)},  {"White", Colour::rgb(0xE8ECF5)},
};

} // namespace

FogColor::FogColor() : Module("FogColor", "Recolours the world's fog", Category::Visuals) {
    std::vector<std::string> names;
    for (const Preset& preset : kPresets)
        names.emplace_back(preset.name);

    m_preset = addEnum("Preset", "Pick a colour, or Custom to use the sliders", std::move(names), 1);

    m_red = addFloat("Red", "Red channel", 0.48f, 0.0f, 1.0f, 0.01f);
    m_green = addFloat("Green", "Green channel", 0.29f, 0.0f, 1.0f, 0.01f);
    m_blue = addFloat("Blue", "Blue channel", 0.85f, 0.0f, 1.0f, 0.01f);

    const auto custom = [this] { return m_preset->is("Custom") && !m_rainbow->value; };
    m_red->onlyIf(custom);
    m_green->onlyIf(custom);
    m_blue->onlyIf(custom);

    m_rainbow = addBool("Rainbow", "Cycle through the hues", false);
    m_rainbowSpeed = addFloat("Speed", "Degrees of hue per second", 30.0f, 5.0f, 180.0f, 5.0f);
    m_rainbowSpeed->onlyIf([this] { return m_rainbow->value; });

    // The preset only makes sense while it is driving the colour.
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
        colour = {m_red->value, m_green->value, m_blue->value};
    } else {
        colour = kPresets[static_cast<size_t>(m_preset->value)].colour;
    }

    hooks::setFogColour(true, colour.r, colour.g, colour.b);
}

void FogColor::onDisable() {
    // Hand the fog straight back; the next frame the game computes it as usual.
    hooks::setFogColour(false, 0.0f, 0.0f, 0.0f);
}

} // namespace aerial::modules
