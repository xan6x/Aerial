#pragma once

#include "Module/Module.h"

namespace aerial {
struct Render2DEvent;
}

namespace aerial::modules {

class Watermark final : public Module {
public:
    Watermark();

    // Draws the list's own furniture; listing it would be noise.
    bool listed() const override { return false; }

private:
    void onRender(Render2DEvent& event);

    EnumSetting* m_style;
    BoolSetting* m_showFps;
    BoolSetting* m_rainbow;
    ColourSetting* m_colour;
};

} // namespace aerial::modules
