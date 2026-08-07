#pragma once

#include "Module/Module.h"

namespace aerial {
struct Render2DEvent;
}

namespace aerial::modules {

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

}
