#pragma once

#include "Module/Module.h"

namespace aerial {
struct Render2DEvent;
}

namespace aerial::modules {

// Spins dropped items in the world instead of leaving them as flat sprites that
// always face the camera.
class ItemPhysics final : public Module {
public:
    ItemPhysics();

protected:
    void onDisable() override;

private:
    void onRender(Render2DEvent& event);

    FloatSetting* m_spin;
    FloatSetting* m_tilt;
    FloatSetting* m_lift;
};

} // namespace aerial::modules
