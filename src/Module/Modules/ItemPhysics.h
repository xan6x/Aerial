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

    FloatSetting* m_spin;
    IntSetting* m_thickness;
    FloatSetting* m_lift;
    FloatSetting* m_pivot;
    BoolSetting* m_noShadow;
    BoolSetting* m_smooth;
    BoolSetting* m_preserve;
    BoolSetting* m_flat;
};

} // namespace aerial::modules
