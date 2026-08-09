#pragma once

#include "Event/Events.h"
#include "Module/Module.h"

namespace aerial::modules {

class ViewModel final : public Module {
public:
    ViewModel();

protected:
    void onEnable() override;
    void onDisable() override;

private:
    void onTick(TickEvent& event);
    void sync();

    FloatSetting* m_itemFov;
    FloatSetting* m_posX;
    FloatSetting* m_posY;
    FloatSetting* m_posZ;
    FloatSetting* m_rotX;
    FloatSetting* m_rotY;
    FloatSetting* m_rotZ;
    FloatSetting* m_scaleX;
    FloatSetting* m_scaleY;
    FloatSetting* m_scaleZ;
};

}
