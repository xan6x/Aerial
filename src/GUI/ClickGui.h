#pragma once

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include "Event/Events.h"
#include "GUI/Theme.h"
#include "Module/Module.h"

namespace aerial::gui {

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

        float cardsRight = 0.0f;
        float scale = 1.0f;
    };

    int activeCharacter() const;

    Layout computeLayout(const Vec2& screenSize, float amount) const;

    void renderCharacter(const Layout& layout, float amount);

    struct ScrollState {
        Animated position{0.0f};
        float max = 0.0f;

        void nudge(float delta) {
            position.set(std::clamp(position.target() + delta, 0.0f, max));
        }
        void reset() { position.set(0.0f); }
    };

    void renderChrome(const Layout& layout, float amount);
    void renderSearch(const Rect& field, float scale, float amount);
    void renderRail(const Layout& layout, float amount);
    void renderCards(const Layout& layout, float amount);

    void renderScrollbar(const Rect& view, const ScrollState& scroll, float contentHeight,
                         float scale, float amount);
    float renderCard(Module& module, const Rect& card, float scale, int index);
    float renderSetting(Setting& setting, Module& module, const Rect& row, float scale);
    void renderCursor(float scale);
    void renderTooltip(const Vec2& screenSize, float scale);

    void handleClick(const Vec2& cursor, bool right);

    void dragSliderTo(float x);

    Animated& animation(const void* key, float initial = 0.0f);

    std::vector<Module*> visibleModules() const;

    Category m_category = Category::Visuals;
    std::unordered_map<const Module*, bool> m_expanded;
    std::unordered_map<const void*, Animated> m_animations;
    std::unordered_map<const void*, Animated> m_hoverAnimations;

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
        SearchField,
        SearchClear,
    };

    struct Hit {
        Rect area;
        HitKind kind = HitKind::Module;
        Module* module = nullptr;
        Setting* setting = nullptr;
        Category category = Category::Visuals;
        std::string config;

        Rect track;
        Rect grab;
    };
    std::vector<Hit> m_hits;

    void applySettingClick(const Hit& hit, const Vec2& cursor, bool right);

    enum class Page { Modules, Configs };
    Page m_page = Page::Modules;

    void renderConfigs(const Layout& layout, float amount);
    bool renderButton(const Rect& area, const std::string& label, float scale, const Colour& tint);

    bool m_editingName = false;
    std::string m_nameBuffer;

    bool m_searching = false;
    std::string m_search;

    ScrollState m_moduleScroll;
    ScrollState m_configScroll;

    ScrollState& activeScroll();

    Setting* m_draggingSlider = nullptr;
    Rect m_draggingSliderRect;
    Module* m_bindingModule = nullptr;

    float m_transition = 0.0f;

    float m_contentSlide = 0.0f;

    std::string m_tooltip;

    bool m_open = false;
    bool m_releasedMouse = false;
    Vec2 m_cursor;
};

}
