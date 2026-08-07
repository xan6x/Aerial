#include "Module/Modules/ItemPhysics.h"

#include "Event/Events.h"
#include "Hooks/Hooks.h"

namespace aerial::modules {

ItemPhysics::ItemPhysics()
    : Module("ItemPhysics", "Dropped items spin instead of facing you", Category::Render) {
    m_spin = addFloat("Spin", "Degrees per second", 120.0f, 0.0f, 720.0f, 10.0f);
    m_tilt = addFloat("Tilt", "How far the item lies over", 90.0f, 0.0f, 90.0f, 5.0f);
    m_lift = addFloat("Height", "Vertical offset", 0.3f, -0.5f, 1.0f, 0.05f);

    listen<Render2DEvent>(&ItemPhysics::onRender);
}

void ItemPhysics::onRender(Render2DEvent& event) {
    (void)event;
    hooks::setItemPhysics(true, m_spin->value, m_tilt->value, m_lift->value);
}

void ItemPhysics::onDisable() { hooks::setItemPhysics(false, 0.0f, 0.0f, 0.0f); }

} // namespace aerial::modules
