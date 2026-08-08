#pragma once

#include "Event/Events.h"
#include "Module/Module.h"

namespace aerial::modules {

class AspectRatio : public Module {
public:
    AspectRatio();

protected:
    void onEnable() override;
    void onDisable() override;

private:
    void onTick(TickEvent& event);

    FloatSetting* m_ratio;
};

}
