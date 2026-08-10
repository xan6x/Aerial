#pragma once

#include "GUI/HudDrag.h"
#include "GUI/Theme.h"
#include "Input/ClickCounter.h"
#include "Module/Module.h"

namespace aerial {
struct Render2DEvent;
}

namespace aerial::modules {

class Keystrokes final : public Module {
public:
    Keystrokes();

private:
    void onRender(Render2DEvent& event);

    BoolSetting* m_mouse;
    BoolSetting* m_space;
    BoolSetting* m_showCps;
    BoolSetting* m_background;
    BoolSetting* m_rainbow;
    ColourSetting* m_colour;
    FloatSetting* m_rounding;

    FloatSetting* m_posX;
    FloatSetting* m_posY;
    hud::Draggable m_drag;

    gui::Animated m_w, m_a, m_s, m_d, m_spaceA, m_lmbA, m_rmbA;
    input::ClickCounter m_lmb, m_rmb;
};

}
