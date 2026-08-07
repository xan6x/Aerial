#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Event/Events.h"
#include "GUI/Theme.h"
#include "Module/Module.h"

namespace aerial::gui {

// A single centred window: frosted glass, a category rail on the left and
// module cards on the right, with the settings for a card expanding inline.
//
// The menu does not capture keybinds while open - its own bind has to keep
// working so the same key closes it. Capture is only taken while a key is being
// assigned to a module.
class ClickGui {
public:
    static ClickGui& get();

    void open();
    void close();
    bool isOpen() const { return m_open; }

    void render(Render2DEvent& event);
    void onMouse(MouseEvent& event);
    void onKey(KeyEvent& event);

private:
    ClickGui() = default;

    struct Layout {
        Rect window;
        Rect rail;
        Rect content;
        Rect header;
        // Right edge of the module cards. With a background character the cards
        // stop short of it so the artwork keeps a column of its own instead of
        // being sliced into strips by opaque rows.
        float cardsRight = 0.0f;
        float scale = 1.0f;
    };

    // Resource id of the selected artwork, 0 when none.
    int activeCharacter() const;

    Layout computeLayout(const Vec2& screenSize) const;

    // Artwork behind the cards, anchored to the bottom-right of the content
    // area and clipped by the window's rounded corners.
    void renderCharacter(const Layout& layout, float amount);

    void renderChrome(const Layout& layout, float amount);
    void renderRail(const Layout& layout, float amount);
    void renderCards(const Layout& layout, float amount);
    float renderCard(Module& module, const Rect& card, float scale, int index);
    float renderSetting(Setting& setting, Module& module, const Rect& row, float scale);
    void renderCursor(float scale);
    void renderTooltip(const Vec2& screenSize, float scale);

    void handleClick(const Vec2& cursor, bool right);
    void applySettingClick(Setting& setting, Module& module, const Rect& row, const Vec2& cursor,
                           bool right);

    Animated& animation(const void* key, float initial = 0.0f);

    std::vector<Module*> visibleModules() const;

    Category m_category = Category::Render;
    std::unordered_map<const Module*, bool> m_expanded;
    std::unordered_map<const void*, Animated> m_animations;
    std::unordered_map<const void*, Animated> m_hoverAnimations;

    // Hit rectangles recorded while drawing, so clicks test exactly what is on
    // screen instead of a second, drifting copy of the layout maths.
    enum class HitKind {
        Module,
        Setting,
        Category,
        Expander,
        ConfigsTab,
        ConfigLoad,
        ConfigSave,
        ConfigDelete,
        ConfigNameField,
        ConfigCreate,
    };

    struct Hit {
        Rect area;
        HitKind kind = HitKind::Module;
        Module* module = nullptr;
        Setting* setting = nullptr;
        Category category = Category::Render;
        std::string config;
    };
    std::vector<Hit> m_hits;

    // Which page the content area shows: the modules of a category, or the
    // config manager.
    enum class Page { Modules, Configs };
    Page m_page = Page::Modules;

    void renderConfigs(const Layout& layout, float amount);
    bool renderButton(const Rect& area, const std::string& label, float scale, const Colour& tint);

    bool m_editingName = false;
    std::string m_nameBuffer;

    Setting* m_draggingSlider = nullptr;
    Rect m_draggingSliderRect;
    Module* m_bindingModule = nullptr;

    Animated m_openAnimation{0.0f};
    std::string m_tooltip;

    bool m_open = false;
    bool m_releasedMouse = false;
    Vec2 m_cursor;
};

} // namespace aerial::gui
