#pragma once

#include "Module/Module.h"

namespace aerial {
struct Render2DEvent;
}

namespace aerial::modules {

// Java's field-of-view behaviour: the view widens while sprinting and eases
// back, rather than snapping the way this build does.
class JavaFov final : public Module {
public:
    JavaFov();

protected:
    void onDisable() override;

private:
    void onRender(Render2DEvent& event);

    FloatSetting* m_sprintFov;
    FloatSetting* m_smoothing;

    // Current multiplier, eased toward the target each frame. Java lerps its
    // FOV modifier rather than applying it outright; that easing is the whole
    // reason it reads differently from Bedrock's.
    float m_current = 1.0f;
};

} // namespace aerial::modules
