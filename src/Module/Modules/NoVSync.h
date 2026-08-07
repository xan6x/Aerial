#pragma once

#include "Module/Module.h"

namespace aerial {
struct Render2DEvent;
}

namespace aerial::modules {

// Presents without waiting for the display refresh, which uncaps the frame
// rate. Off by default and inert while off - the game's own vsync setting is
// left alone unless this is switched on.
class NoVSync final : public Module {
public:
    NoVSync();

protected:
    void onDisable() override;

private:
    void onRender(Render2DEvent& event);

    bool m_warned = false;
    bool m_hadVsync = true;   // what the game had before we took over
};

} // namespace aerial::modules
