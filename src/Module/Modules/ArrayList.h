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

    // It is the list; listing itself would be noise.
    bool listed() const override { return false; }

private:
    // One row's animation state, kept between frames so a module that is
    // toggled slides instead of appearing and vanishing on the spot.
    struct Row {
        gui::Animated slide{0.0f};   // 0 = parked off the right edge, 1 = home
        gui::Animated y{0.0f};       // current vertical position
        std::string label;
        int index = 0;
        bool alive = false;          // still in the active set this frame
        bool placed = false;         // has been given a real position at least once
    };

    void onRender(Render2DEvent& event);

    // Rows outlive their module being switched off, so the map is keyed by the
    // module pointer, which ModuleManager keeps stable for the whole session.
    std::unordered_map<const Module*, Row> m_rows;

    EnumSetting* m_sort;
    BoolSetting* m_rainbow;
    BoolSetting* m_background;
    FloatSetting* m_speed;
    EnumSetting* m_accent;
};

} // namespace aerial::modules
