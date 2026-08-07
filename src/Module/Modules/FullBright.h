#pragma once

#include "Module/Module.h"

namespace aerial {
struct Render2DEvent;
}

namespace aerial::modules {

// Forces the game's gamma to its brightest, so caves and night are lit.
class FullBright final : public Module {
public:
    FullBright();

protected:
    void onDisable() override;

private:
    void onRender(Render2DEvent& event);

    FloatSetting* m_gamma;
};

} // namespace aerial::modules
