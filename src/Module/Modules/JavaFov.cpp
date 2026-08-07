#include "Module/Modules/JavaFov.h"

#include <cmath>

#include "Event/Events.h"
#include "Hooks/Hooks.h"
#include "SDK/Context.h"
#include "SDK/Entity.h"

namespace aerial::modules {

JavaFov::JavaFov() : Module("JavaFov", "Java's sprint field-of-view easing", Category::Visuals) {

    m_sprintFov = addFloat("Sprint FOV", "Multiplier while sprinting", 1.15f, 1.0f, 1.5f, 0.01f);
    m_smoothing = addFloat("Smoothing", "How quickly it eases", 0.15f, 0.02f, 1.0f, 0.01f);

    listen<Render2DEvent>(&JavaFov::onRender);
}

void JavaFov::onRender(Render2DEvent& event) {
    (void)event;

    auto* player = sdk::Context::get().localPlayer;
    const bool sprinting = player && player->isSprinting();
    const float target = sprinting ? m_sprintFov->value : 1.0f;

    m_current += (target - m_current) * m_smoothing->value;
    if (std::fabs(target - m_current) < 0.0005f)
        m_current = target;

    hooks::setFovScale(true, m_current);
}

void JavaFov::onDisable() {
    m_current = 1.0f;
    hooks::setFovScale(false, 1.0f);
}

}
