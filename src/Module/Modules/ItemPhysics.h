#pragma once

#include "Module/Module.h"

namespace aerial {
struct Render2DEvent;
}

namespace aerial::modules {

// Dropped items turn while they fall and come to rest when they land, instead of
// staying flat sprites that always face the camera. Blocks settle tipped onto a
// corner, other items lie flat, the way Java's do.
class ItemPhysics final : public Module {
public:
    ItemPhysics();

protected:
    void onDisable() override;

private:
    void onRender(Render2DEvent& event);

    BoolSetting* m_flat;
    FloatSetting* m_spin;
    FloatSetting* m_lift;
    FloatSetting* m_pivot;
    BoolSetting* m_smooth;
    BoolSetting* m_preserve;
};

} // namespace aerial::modules
