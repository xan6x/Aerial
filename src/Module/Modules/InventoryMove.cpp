#include "Module/Modules/Modules.h"

#include <cmath>

#include "Event/Events.h"
#include "GUI/ClickGui.h"
#include "Input/InputManager.h"
#include "SDK/ClientInstance.h"
#include "SDK/Context.h"
#include "SDK/Entity.h"

namespace aerial::modules {
namespace {

// A screen being open is read from the mouse-grab flag rather than from the
// screen stack: MinecraftGame+0xB8 is set by grabMouse and cleared by
// releaseMouse, so it is exactly "the player is not in direct control".
bool screenIsOpen() {
    auto* client = sdk::Context::get().client;
    auto* game = client ? client->game() : nullptr;
    return game && !game->mouseGrabbed();
}

} // namespace

InventoryMove::InventoryMove()
    : Module("InventoryMove", "Walk with the inventory open", Category::Movement) {
    m_speed = addFloat("Speed", "Movement speed while a screen is open", 0.325f, 0.05f, 0.6f, 0.005f);

    listen<TickEvent>(&InventoryMove::onTick);
}

void InventoryMove::onTick(TickEvent& event) {
    // The client's own menu blocks movement deliberately; do not fight it.
    if (gui::ClickGui::get().isOpen() || !screenIsOpen())
        return;

    auto& input = input::InputManager::get();
    const bool forward = input.isDown('W');
    const bool back = input.isDown('S');
    const bool left = input.isDown('A');
    const bool right = input.isDown('D');

    // Opposite keys cancel, exactly as they do in normal movement.
    if (forward && back)
        return;
    if (left && right && !forward && !back)
        return;

    // Rather than restore the input the game suppressed - which lives behind a
    // chain that resisted several attempts - the motion is applied directly,
    // with the key combination turned into an offset from where the player is
    // looking. This is the approach Horion uses on this game family.
    float yaw = event.player->rot().y;
    bool moving = true;

    if (forward && right)
        yaw += 45.0f;
    else if (forward && left)
        yaw -= 45.0f;
    else if (back && right)
        yaw += 135.0f;
    else if (back && left)
        yaw -= 135.0f;
    else if (forward)
        ;  // straight ahead
    else if (back)
        yaw += 180.0f;
    else if (right)
        yaw += 90.0f;
    else if (left)
        yaw -= 90.0f;
    else
        moving = false;

    if (!moving)
        return;

    if (yaw >= 180.0f)
        yaw -= 360.0f;

    const float radians = (yaw + 90.0f) * kDeg2Rad;
    const float speed = m_speed->value;

    // Vertical motion is left untouched so gravity and jumps still behave.
    const Vec3 motion{std::cos(radians) * speed, event.player->velocity().y,
                      std::sin(radians) * speed};
    event.player->lerpMotion(motion);
}

} // namespace aerial::modules
