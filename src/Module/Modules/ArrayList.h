#pragma once

#include <string>
#include <unordered_map>

#include "GUI/Theme.h"
#include "Module/Module.h"

namespace aerial {
struct Render2DEvent;
}

namespace aerial::modules {

class ArrayList final : public Module {
public:
    ArrayList();

    bool listed() const override { return false; }

private:

    struct Row {
        gui::Animated slide{0.0f};
        gui::Animated y{0.0f};
        std::string label;
        int index = 0;
        bool alive = false;
        bool placed = false;
    };

    void onRender(Render2DEvent& event);

    std::unordered_map<const Module*, Row> m_rows;

    EnumSetting* m_sort;
    BoolSetting* m_rainbow;
    BoolSetting* m_background;
    FloatSetting* m_speed;
    EnumSetting* m_accent;
};

}
