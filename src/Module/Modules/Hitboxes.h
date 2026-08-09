#pragma once

#include "Module/Module.h"

namespace aerial {
struct Render2DEvent;
}

namespace aerial::modules {

class Hitboxes final : public Module {
public:
    Hitboxes();

private:
    void onRender(Render2DEvent& event);

    ColourSetting* m_colour;
    FloatSetting* m_thickness;
    FloatSetting* m_expand;
    BoolSetting* m_showSelf;
    BoolSetting* m_eyeLine;
    ColourSetting* m_eyeColour;
    BoolSetting* m_lookLine;
    ColourSetting* m_lookColour;
    FloatSetting* m_lookLength;
};

}
