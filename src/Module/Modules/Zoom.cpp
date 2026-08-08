#include "Module/Modules/Zoom.h"

#include <cmath>

#include "Event/Events.h"
#include "GUI/Theme.h"
#include "Hooks/FovHooks.h"
#include "Input/InputManager.h"
#include "SDK/Context.h"

namespace aerial::modules {

Zoom::Zoom() : Module("Zoom", "Narrows the field of view while the bind is held", Category::Visuals,
                      'C') {
    m_amount = addFloat("Amount", "How far in it goes", 4.0f, 1.5f, 12.0f, 0.5f);

    m_hold = addBool("Hold", "Zoom while the bind is down instead of toggling the module", true);

    m_smooth = addBool("Smooth", "Ease in and out instead of snapping", true);
    m_speed = addFloat("Speed", "How quickly it eases", 0.35f, 0.05f, 1.0f, 0.05f);
    m_speed->onlyIf([this] { return m_smooth->value; });

    listen<Render2DEvent>(&Zoom::onRender);
}

bool Zoom::holdBind() const { return m_hold->value; }

std::string Zoom::suffix() const {
    if (!enabled())
        return {};
    if (m_hold->value && keybind() == 0)
        return "no bind";
    return {};
}

void Zoom::onRender(Render2DEvent& event) {
    (void)event;

    const bool active = sdk::Context::get().worldInteractive() &&
                        (!m_hold->value ||
                         (keybind() != 0 && input::InputManager::get().isDown(keybind())));

    const float target = active ? 1.0f / m_amount->value : 1.0f;

    if (m_smooth->value) {
        const float step = 1.0f - std::pow(1.0f - m_speed->value, gui::frameDelta() * 60.0f);
        m_current += (target - m_current) * step;
        if (std::fabs(target - m_current) < 0.0005f)
            m_current = target;
    } else {
        m_current = target;
    }

    hooks::setFovScale(hooks::Fov::Zoom, m_current);
}

void Zoom::onDisable() {
    m_current = 1.0f;
    hooks::setFovScale(hooks::Fov::Zoom, 1.0f);
}

}
