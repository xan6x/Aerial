#pragma once

#include "Module/Module.h"

namespace aerial {
struct Render2DEvent;
}

namespace aerial::modules {

class ChunkBorder final : public Module {
public:
    ChunkBorder();

private:
    void onRender(Render2DEvent& event);

    IntSetting* m_gridSpacing;
    FloatSetting* m_heightSpacing;
    ColourSetting* m_mid;
    ColourSetting* m_adjacent;
    ColourSetting* m_corner;
};

}
