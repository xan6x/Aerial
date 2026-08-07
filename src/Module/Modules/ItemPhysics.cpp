#include "Module/Modules/ItemPhysics.h"

#include "Event/Events.h"
#include "Hooks/Hooks.h"

namespace aerial::modules {

ItemPhysics::ItemPhysics()
    : Module("ItemPhysics", "Dropped items spin as they fall and settle when they land",
             Category::Visuals) {
    m_spin = addFloat("Speed", "Degrees per second while falling", 240.0f, 0.0f, 720.0f, 10.0f);

    m_thickness = addInt("Thickness", "How solid dropped items look", 6, 1, 12);

    m_lift = addFloat("Ground offset", "Height above the block, non-block items only", -0.12f,
                      -0.5f, 0.6f, 0.01f);

    m_noShadow = addBool("No shadow", "Hide the blob shadow under dropped items", true);

    m_pivot = addFloat("Pivot", "Shift along the item's own axis after rotating", 0.0f, -1.0f, 1.0f,
                       0.05f);

    m_smooth = addBool("Smooth landing", "Ease into the resting angle instead of snapping", true);
    m_preserve = addBool("Keep angle", "Leave each item at its own angle instead of aligning them",
                         false);

    m_flat = addBool("Always flat", "Skip the falling spin and draw every item resting", false);

    listen<Render2DEvent>(&ItemPhysics::onRender);
}

void ItemPhysics::onRender(Render2DEvent& event) {
    (void)event;
    hooks::setItemPhysics(true, m_spin->value, m_lift->value, m_pivot->value, m_thickness->value,
                          m_smooth->value, m_preserve->value, m_flat->value, m_noShadow->value);
}

void ItemPhysics::onDisable() {
    hooks::setItemPhysics(false, 0.0f, 0.0f, 0.0f, 1, false, false, false, false);
}

}
