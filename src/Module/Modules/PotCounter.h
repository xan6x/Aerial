#pragma once

#include <atomic>

#include "GUI/HudDrag.h"
#include "Module/Module.h"

namespace aerial {
struct Render2DEvent;
struct TickEvent;
}

namespace aerial::modules {

class PotCounter final : public Module {
public:
    PotCounter();

private:
    void onRender(Render2DEvent& event);
    void onTick(TickEvent& event);

    ColourSetting* m_colour;
    BoolSetting* m_rainbow;
    BoolSetting* m_background;
    FloatSetting* m_rounding;

    FloatSetting* m_posX;
    FloatSetting* m_posY;
    hud::Draggable m_drag;

    std::atomic<int> m_pots{0};
};

}
