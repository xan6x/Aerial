#pragma once

#include "Module/Module.h"

namespace aerial {
struct KeyEvent;
struct CharEvent;
struct MouseEvent;
struct Render2DEvent;
}

namespace aerial::modules {

class ClickGuiModule final : public Module {
public:
    ClickGuiModule();

    bool persistEnabled() const override { return false; }
    bool listed() const override { return false; }

protected:
    void onEnable() override;
    void onDisable() override;

private:
    void onRender(Render2DEvent& event);
    void onMouse(MouseEvent& event);
    void onKey(KeyEvent& event);
    void onChar(CharEvent& event);

    EnumSetting* m_character;
    FloatSetting* m_characterOpacity;
};

}
