#include "Module/Modules/FullBright.h"

#include "Event/Events.h"
#include "Hooks/Hooks.h"

namespace aerial::modules {

FullBright::FullBright() : Module("FullBright", "Lights up caves and night", Category::Visuals) {

    m_gamma = addFloat("Brightness", "Gamma the game is told to use", 1.0f, 0.5f, 5.0f, 0.1f);

    listen<Render2DEvent>(&FullBright::onRender);
}

void FullBright::onRender(Render2DEvent& event) {
    (void)event;
    hooks::setGammaOverride(true, m_gamma->value);
}

void FullBright::onDisable() { hooks::setGammaOverride(false, 1.0f); }

}
