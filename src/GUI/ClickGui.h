#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Event/Events.h"
#include "GUI/Theme.h"
#include "Module/Module.h"

namespace aerial::gui {

// Draggable per-category panels, module rows, expandable settings - the layout
// Java clients popularised, drawn with MC's own 2D renderer.
//
// The menu does not capture keybinds while open: its own bind has to keep
// working so the same key closes it. Capture is only taken while a key is being
// assigned to a module.
class ClickGui {
public:
    static ClickGui& get();

    void init();

    void open();
    void close();
    bool isOpen() const { return m_open; }

    void render(Render2DEvent& event);
    void onMouse(MouseEvent& event);
    void onKey(KeyEvent& event);

private:
    struct Panel {
        Category category = Category::Combat;
        Vec2 position;
        float width = 132.0f;
        bool collapsed = false;
        bool dragging = false;
        Vec2 dragOffset;
        Animated expand{1.0f};
    };

    ClickGui() = default;

    void layoutDefault();

    float renderPanel(Panel& panel, const Vec2& cursor, float openAmount);
    float renderModuleRow(Module& module, const Rect& row, const Vec2& cursor, int index);
    float renderSetting(Setting& setting, Module& module, const Rect& row, const Vec2& cursor);
    void renderTooltip(const Vec2& screenSize);
    void renderCursor();
    void updateSliderDrag();

    void handleClick(const Vec2& cursor, bool right);
    void applySettingClick(Setting& setting, Module& module, const Rect& row, const Vec2& cursor,
                           bool right);

    std::vector<Panel> m_panels;
    std::unordered_map<const Module*, bool> m_expanded;
    std::unordered_map<const void*, Animated> m_animations;

    Setting* m_draggingSlider = nullptr;
    Rect m_draggingSliderRect;
    Module* m_bindingModule = nullptr;

    Animated m_openAnimation{0.0f};
    std::string m_tooltip;

    bool m_open = false;
    bool m_initialised = false;
    Vec2 m_cursor;
};

} // namespace aerial::gui
