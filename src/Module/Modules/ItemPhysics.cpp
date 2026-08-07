#include "Module/Modules/ItemPhysics.h"

#include "Event/Events.h"
#include "Hooks/Hooks.h"

namespace aerial::modules {

ItemPhysics::ItemPhysics()
    : Module("ItemPhysics", "Dropped items spin as they fall and settle when they land",
             Category::Render) {
    m_flat = addBool("Lie flat", "Items rest on the block instead of turning", true);

    m_spin = addFloat("Speed", "Degrees per second while falling", 240.0f, 0.0f, 720.0f, 10.0f);

    // Renamed from "Height", whose default was 0.3 - a value tuned for the old
    // upright sprite, and the reason items hung well above their own shadow.
    // Renaming it also means an existing config cannot carry the old number in.
    m_lift = addFloat("Ground offset", "Height above the block, non-block items only", 0.03f, -0.2f,
                      0.6f, 0.01f);

    // Zero by default because the sprite in this build may already be centred -
    // this is the knob for the case where a laid-flat item sits buried in the
    // ground or floating above it.
    m_pivot = addFloat("Pivot", "Shift along the item's own axis after rotating", 0.0f, -1.0f, 1.0f,
                       0.05f);

    m_smooth = addBool("Smooth landing", "Ease into the resting angle instead of snapping", true);
    m_preserve = addBool("Keep angle", "Leave each item at its own angle instead of aligning them",
                         false);

    listen<Render2DEvent>(&ItemPhysics::onRender);
}

void ItemPhysics::onRender(Render2DEvent& event) {
    (void)event;
    hooks::setItemPhysics(true, m_spin->value, m_lift->value, m_pivot->value, m_smooth->value,
                          m_preserve->value, m_flat->value);
}

void ItemPhysics::onDisable() {
    hooks::setItemPhysics(false, 0.0f, 0.0f, 0.0f, false, false, false);
}

} // namespace aerial::modules
