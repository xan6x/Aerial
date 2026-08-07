#include "Module/Modules/ItemPhysics.h"

#include "Event/Events.h"
#include "Hooks/Hooks.h"

namespace aerial::modules {

ItemPhysics::ItemPhysics()
    : Module("ItemPhysics", "Dropped items spin as they fall and settle when they land",
             Category::Visuals) {
    m_spin = addFloat("Speed", "Degrees per second while falling", 240.0f, 0.0f, 720.0f, 10.0f);

    // A flat sprite lying on grass is nearly invisible - it has no silhouette
    // from any angle. Redrawing it a few times a fraction of a block apart gives
    // it one, which is what makes a dropped item findable again.
    m_thickness = addInt("Thickness", "How solid dropped items look", 6, 1, 12);

    // Renamed from "Height": its default of 0.3 was tuned for the old upright
    // sprite and is what left items hanging above their own shadow. Renaming it
    // also stops an existing config carrying the old number back in. Now that the
    // game's bob is suppressed it can go below zero, which is where the gap
    // finally closes.
    m_lift = addFloat("Ground offset", "Height above the block, non-block items only", -0.12f,
                      -0.5f, 0.6f, 0.01f);

    m_noShadow = addBool("No shadow", "Hide the blob shadow under dropped items", true);

    // Zero by default because the sprite in this build may already be centred -
    // this is the knob for the case where a laid-flat item sits buried in the
    // ground or floating above it.
    m_pivot = addFloat("Pivot", "Shift along the item's own axis after rotating", 0.0f, -1.0f, 1.0f,
                       0.05f);

    m_smooth = addBool("Smooth landing", "Ease into the resting angle instead of snapping", true);
    m_preserve = addBool("Keep angle", "Leave each item at its own angle instead of aligning them",
                         false);

    // Off by default: the point of the module is that items fall with the spin
    // and settle when they land. This is the escape hatch for the case where the
    // game never reports an item as having come to rest - then nothing ever
    // settles, and forcing the resting pose is the only way to get one.
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

} // namespace aerial::modules
