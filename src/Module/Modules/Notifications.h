#pragma once

#include <deque>
#include <string>
#include <vector>

#include "GUI/Theme.h"
#include "Module/Module.h"

namespace aerial {
struct ModuleToggleEvent;
struct Render2DEvent;
}

namespace aerial::modules {

class Notifications final : public Module {
public:
    enum class Level { Info, Success, Warning, Error };

    Notifications();

    bool listed() const override { return false; }

    static void push(std::string text, Level level = Level::Info);

private:
    struct Toast {
        std::string text;
        Level level = Level::Info;
        float born = 0.0f;
        gui::Animated slide{0.0f};
        bool expiring = false;
    };

    void onRender(Render2DEvent& event);
    void onModuleToggle(ModuleToggleEvent& event);

    void add(std::string text, Level level);
    void playChime(bool rising);
    Colour colourFor(Level level) const;

    std::deque<Toast> m_toasts;

    BoolSetting* m_sound;
    FloatSetting* m_volume;
    FloatSetting* m_duration;
    IntSetting* m_maxVisible;
    EnumSetting* m_corner;
    BoolSetting* m_moduleToggles;

    std::vector<uint8_t> m_chimeUp;
    std::vector<uint8_t> m_chimeDown;
    float m_builtVolume = -1.0f;
};

}
