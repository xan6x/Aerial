#pragma once

#include <vector>

#include "Module/Module.h"
#include "Utils/Math.h"

namespace aerial {
struct Render2DEvent;
}

namespace aerial::modules {

class JumpCircles final : public Module {
public:
    JumpCircles();

protected:
    void onEnable() override;
    void onDisable() override;

private:
    void onRender(Render2DEvent& event);

    struct Ripple {
        Vec3 origin;
        float age = 0.0f;
    };

    ColourSetting* m_colour;
    BoolSetting* m_rainbow;
    FloatSetting* m_radius;
    IntSetting* m_rings;
    FloatSetting* m_duration;
    FloatSetting* m_thickness;
    IntSetting* m_segments;
    FloatSetting* m_height;

    std::vector<Ripple> m_ripples;
    uint64_t m_lastSeq = 0;
};

}
