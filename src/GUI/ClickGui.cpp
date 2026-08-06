#include "GUI/ClickGui.h"

#include <Windows.h>
#include <algorithm>
#include <format>

#include "Input/InputManager.h"
#include "Module/ModuleManager.h"
#include "Render/DrawUtils.h"
#include "SDK/Context.h"
#include "Utils/Logger.h"

namespace aerial::gui {
namespace {

using render::DrawUtils;

constexpr float kPanelWidth = 132.0f;
constexpr float kHeaderHeight = 17.0f;
constexpr float kRowHeight = 14.0f;
constexpr float kSettingHeight = 12.0f;
constexpr float kTextOffset = 3.0f;      // vertical nudge to centre 10px text
constexpr float kAccentWidth = 2.0f;
constexpr float kPanelGap = 7.0f;

const Category kCategories[] = {Category::Combat, Category::Movement, Category::Player,
                                Category::World,  Category::Render,   Category::Misc};

// Track geometry for a slider row, shared by drawing and hit-testing so the two
// can never disagree.
Rect sliderTrack(const Rect& row) {
    return {row.left + 8.0f, row.bottom - 3.5f, row.right - 7.0f, row.bottom - 1.5f};
}

std::string formatValue(const Setting& setting) {
    if (setting.type() == Setting::Type::Float) {
        const auto& value = static_cast<const FloatSetting&>(setting);
        return value.step >= 1.0f ? std::format("{:.0f}", value.value) : std::format("{:.2f}", value.value);
    }
    return std::to_string(static_cast<const IntSetting&>(setting).value);
}

} // namespace

ClickGui& ClickGui::get() {
    static ClickGui instance;
    return instance;
}

void ClickGui::init() {
    if (m_initialised)
        return;
    layoutDefault();
    m_initialised = true;
}

void ClickGui::layoutDefault() {
    m_panels.clear();

    float x = 10.0f;
    for (Category category : kCategories) {
        // An empty category would render as a bare header with nothing under
        // it, so skip it entirely.
        if (ModuleManager::get().byCategory(category).empty())
            continue;

        Panel panel;
        panel.category = category;
        panel.position = {x, 10.0f};
        panel.width = kPanelWidth;
        m_panels.push_back(panel);
        x += kPanelWidth + kPanelGap;
    }
}

void ClickGui::open() {
    init();
    m_open = true;
    m_openAnimation.set(1.0f);

    auto& context = sdk::Context::get();

    if (!context.inGame())
        LOG_INFO("ClickGui", "opened outside a world - nothing will be drawn until you join one");

    if (context.client && context.inGame())
        context.client->releaseMouse();
}

void ClickGui::close() {
    m_open = false;
    m_openAnimation.set(0.0f);
    m_draggingSlider = nullptr;
    m_bindingModule = nullptr;

    for (auto& panel : m_panels)
        panel.dragging = false;

    // Capture only ever guards keybind assignment; clear it defensively.
    input::InputManager::get().setCapture(false);

    auto& context = sdk::Context::get();
    if (context.client && context.inGame())
        context.client->grabMouse();
}

void ClickGui::render(Render2DEvent& event) {
    const float amount = m_openAnimation.update(Theme::get().animationSpeed);
    if (!m_open && amount <= 0.01f)
        return;

    init();
    m_cursor = input::InputManager::get().cursor();
    m_tooltip.clear();

    // Dim the world behind the menu, scaled by the open animation.
    DrawUtils::fill({0.0f, 0.0f, event.screenSize.x, event.screenSize.y},
                    Colour::rgb(0x05070C, 0.45f * amount));

    for (auto& panel : m_panels) {
        if (panel.dragging)
            panel.position = m_cursor - panel.dragOffset;

        panel.position.x = std::clamp(panel.position.x, 2.0f, event.screenSize.x - panel.width - 2.0f);
        panel.position.y = std::clamp(panel.position.y, 2.0f, event.screenSize.y - kHeaderHeight - 2.0f);

        renderPanel(panel, m_cursor, amount);
    }

    updateSliderDrag();
    renderTooltip(event.screenSize);
    renderCursor();
}

void ClickGui::updateSliderDrag() {
    if (!m_draggingSlider || !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
        m_draggingSlider = nullptr;
        return;
    }

    const float t = std::clamp((m_cursor.x - m_draggingSliderRect.left) / m_draggingSliderRect.width(),
                               0.0f, 1.0f);
    if (auto* asFloat = dynamic_cast<FloatSetting*>(m_draggingSlider))
        asFloat->setFraction(t);
    else if (auto* asInt = dynamic_cast<IntSetting*>(m_draggingSlider))
        asInt->setFraction(t);
}

void ClickGui::renderCursor() {
    const Theme& theme = Theme::get();
    const Vec2 c = m_cursor;

    // Small crosshair-style pointer with a dark outline so it reads on any
    // background.
    DrawUtils::fill({c.x - 0.5f, c.y - 0.5f, c.x + 8.5f, c.y + 2.5f}, Colour::rgb(0x000000, 0.55f));
    DrawUtils::fill({c.x - 0.5f, c.y - 0.5f, c.x + 2.5f, c.y + 8.5f}, Colour::rgb(0x000000, 0.55f));
    DrawUtils::fill({c.x, c.y, c.x + 8.0f, c.y + 2.0f}, theme.textActive);
    DrawUtils::fill({c.x, c.y, c.x + 2.0f, c.y + 8.0f}, theme.textActive);
}

void ClickGui::renderTooltip(const Vec2& screenSize) {
    if (m_tooltip.empty())
        return;

    const Theme& theme = Theme::get();
    const float width = DrawUtils::textWidth(m_tooltip) + 8.0f;
    const float height = 12.0f;

    float x = m_cursor.x + 10.0f;
    float y = m_cursor.y + 10.0f;
    x = std::min(x, screenSize.x - width - 2.0f);
    y = std::min(y, screenSize.y - height - 2.0f);

    const Rect box{x, y, x + width, y + height};
    DrawUtils::fill(box.offset({1.0f, 1.0f}), Colour::rgb(0x000000, 0.45f));
    DrawUtils::fill(box, Colour::rgb(0x11141B, 0.97f));
    DrawUtils::fill({box.left, box.top, box.left + 1.5f, box.bottom}, theme.accent);
    DrawUtils::text(m_tooltip, {box.left + 5.0f, box.top + kTextOffset}, theme.text);
}

float ClickGui::renderPanel(Panel& panel, const Vec2& cursor, float openAmount) {
    const Theme& theme = Theme::get();
    const auto modules = ModuleManager::get().byCategory(panel.category);
    const int categoryIndex = static_cast<int>(panel.category);

    panel.expand.set(panel.collapsed ? 0.0f : 1.0f);
    const float expand = panel.expand.update(theme.animationSpeed);

    float contentHeight = 0.0f;
    for (Module* module : modules) {
        contentHeight += kRowHeight;
        if (m_expanded[module]) {
            for (const auto& setting : module->settings()) {
                if (setting->visible())
                    contentHeight += kSettingHeight;
            }
        }
    }
    contentHeight = (contentHeight + 3.0f) * expand;

    const float height = kHeaderHeight + contentHeight;
    const Rect body{panel.position.x, panel.position.y, panel.position.x + panel.width,
                    panel.position.y + height};

    // Shadow, then body, then header with an accent underline.
    DrawUtils::fill(body.offset({2.0f, 2.0f}), Colour::rgb(0x000000, 0.35f * openAmount));
    DrawUtils::fill(body, theme.panel.withAlpha(theme.panel.a * openAmount));

    const Rect header{body.left, body.top, body.right, body.top + kHeaderHeight};
    DrawUtils::fill(header, theme.header.withAlpha(theme.header.a * openAmount));
    DrawUtils::gradientHorizontal({header.left, header.bottom - 1.5f, header.right, header.bottom},
                                  theme.accentFor(categoryIndex),
                                  theme.accentFor(categoryIndex + 3), 16);

    DrawUtils::text(categoryName(panel.category), {header.left + 6.0f, header.top + 4.0f},
                    theme.textActive);
    DrawUtils::textRight(panel.collapsed ? "+" : "\xE2\x80\x93", {header.right - 6.0f, header.top + 4.0f},
                         theme.textDim);

    if (header.contains(cursor))
        m_tooltip = "drag to move, right-click to collapse";

    if (expand <= 0.02f)
        return height;

    float y = header.bottom + 1.0f;
    int index = 0;
    for (Module* module : modules) {
        const Rect row{body.left, y, body.right, y + kRowHeight};
        if (row.bottom > body.bottom)
            break;

        y += renderModuleRow(*module, row, cursor, index++);

        if (!m_expanded[module])
            continue;

        for (const auto& setting : module->settings()) {
            if (!setting->visible())
                continue;
            const Rect settingRow{body.left, y, body.right, y + kSettingHeight};
            if (settingRow.bottom > body.bottom)
                break;
            y += renderSetting(*setting, *module, settingRow, cursor);
        }
    }

    return height;
}

float ClickGui::renderModuleRow(Module& module, const Rect& row, const Vec2& cursor, int index) {
    const Theme& theme = Theme::get();
    const bool hovered = row.contains(cursor);

    Animated& highlight = m_animations[&module];
    highlight.set(module.enabled() ? 1.0f : (hovered ? 0.4f : 0.0f));
    const float amount = highlight.update(theme.animationSpeed);

    const Colour accent = theme.accentFor(index);

    if (amount > 0.01f) {
        DrawUtils::fill(row, accent.withAlpha(0.16f * amount));
        DrawUtils::fill({row.left, row.top, row.left + kAccentWidth, row.bottom},
                        accent.withAlpha(amount));
    }
    if (hovered && !module.enabled())
        DrawUtils::fill(row, Colour::rgb(0xFFFFFF, 0.04f));

    const Colour textColour = module.enabled() ? theme.textActive : theme.textDim;
    DrawUtils::text(module.name(), {row.left + 7.0f, row.top + kTextOffset}, textColour);

    // Right-hand marker: binding state, or an expander when there are settings.
    const bool hasSettings = module.settings().size() > 1;  // every module owns a keybind
    if (m_bindingModule == &module)
        DrawUtils::textRight("...", {row.right - 6.0f, row.top + kTextOffset}, accent);
    else if (hasSettings)
        DrawUtils::textRight(m_expanded[&module] ? "-" : "+", {row.right - 6.0f, row.top + kTextOffset},
                             theme.textDim);

    if (hovered) {
        m_tooltip = module.description();
        const int bind = module.keybind();
        if (bind != 0)
            m_tooltip += std::format("  [{}]", input::InputManager::keyName(bind));
    }

    return kRowHeight;
}

float ClickGui::renderSetting(Setting& setting, Module& module, const Rect& row, const Vec2& cursor) {
    const Theme& theme = Theme::get();
    const bool hovered = row.contains(cursor);

    DrawUtils::fill(row, theme.panelAlt.withAlpha(hovered ? 0.85f : 0.55f));
    DrawUtils::fill({row.left, row.top, row.left + kAccentWidth, row.bottom},
                    theme.accent.withAlpha(0.25f));

    if (hovered)
        m_tooltip = setting.description();

    const Vec2 label{row.left + 8.0f, row.top + 1.5f};

    switch (setting.type()) {
    case Setting::Type::Bool: {
        const auto& toggle = static_cast<BoolSetting&>(setting);
        DrawUtils::text(setting.name(), label, toggle.value ? theme.text : theme.textDim);

        const Rect box{row.right - 15.0f, row.top + 3.0f, row.right - 7.0f, row.top + 9.0f};
        DrawUtils::fill(box.inset(-0.5f), Colour::rgb(0x000000, 0.35f));
        DrawUtils::fill(box, toggle.value ? theme.accent : Colour::rgb(0x363C4B));
        break;
    }
    case Setting::Type::Float:
    case Setting::Type::Int: {
        const float fraction = setting.type() == Setting::Type::Float
                                   ? static_cast<FloatSetting&>(setting).fraction()
                                   : static_cast<IntSetting&>(setting).fraction();

        DrawUtils::text(setting.name(), label, theme.textDim);
        DrawUtils::textRight(formatValue(setting), {row.right - 6.0f, row.top + 1.5f}, theme.text);

        const Rect track = sliderTrack(row);
        DrawUtils::fill(track, Colour::rgb(0x363C4B));

        const float filled = track.left + track.width() * fraction;
        DrawUtils::fill({track.left, track.top, filled, track.bottom}, theme.accent);
        DrawUtils::fill({filled - 1.0f, track.top - 1.5f, filled + 1.0f, track.bottom + 1.5f},
                        theme.textActive);
        break;
    }
    case Setting::Type::Enum: {
        const auto& choice = static_cast<EnumSetting&>(setting);
        DrawUtils::text(setting.name(), label, theme.textDim);
        DrawUtils::textRight(choice.selected(), {row.right - 6.0f, row.top + 1.5f}, theme.accent);
        break;
    }
    case Setting::Type::Keybind: {
        const auto& bind = static_cast<KeybindSetting&>(setting);
        const bool binding = m_bindingModule == &module;
        DrawUtils::text(setting.name(), label, theme.textDim);
        DrawUtils::textRight(binding ? "press a key" : input::InputManager::keyName(bind.value),
                             {row.right - 6.0f, row.top + 1.5f}, binding ? theme.accent : theme.text);
        if (hovered)
            m_tooltip = "click to rebind, right-click to clear";
        break;
    }
    case Setting::Type::Colour: {
        const auto& colour = static_cast<ColourSetting&>(setting);
        DrawUtils::text(setting.name(), label, theme.textDim);
        const Rect swatch{row.right - 20.0f, row.top + 3.0f, row.right - 7.0f, row.top + 9.0f};
        DrawUtils::fill(swatch.inset(-0.5f), Colour::rgb(0x000000, 0.35f));
        DrawUtils::fill(swatch, colour.value);
        break;
    }
    case Setting::Type::Text: {
        const auto& textValue = static_cast<TextSetting&>(setting);
        DrawUtils::text(setting.name(), label, theme.textDim);
        DrawUtils::textRight(textValue.value, {row.right - 6.0f, row.top + 1.5f}, theme.text);
        break;
    }
    }

    return kSettingHeight;
}

void ClickGui::onMouse(MouseEvent& event) {
    if (!m_open)
        return;

    event.cancel();  // the GUI owns the mouse while it is open

    if (event.button == MouseEvent::Button::Left && !event.down) {
        m_draggingSlider = nullptr;
        for (auto& panel : m_panels)
            panel.dragging = false;
        return;
    }
    if (!event.down)
        return;

    if (event.button == MouseEvent::Button::Left || event.button == MouseEvent::Button::Right)
        handleClick(event.position, event.button == MouseEvent::Button::Right);
}

void ClickGui::handleClick(const Vec2& cursor, bool right) {
    for (auto& panel : m_panels) {
        const Rect header{panel.position.x, panel.position.y, panel.position.x + panel.width,
                          panel.position.y + kHeaderHeight};

        if (header.contains(cursor)) {
            if (right) {
                panel.collapsed = !panel.collapsed;
            } else {
                panel.dragging = true;
                panel.dragOffset = cursor - panel.position;
            }
            return;
        }

        if (panel.collapsed)
            continue;

        float y = header.bottom + 1.0f;
        for (Module* module : ModuleManager::get().byCategory(panel.category)) {
            const Rect row{header.left, y, header.right, y + kRowHeight};
            if (row.contains(cursor)) {
                if (right)
                    m_expanded[module] = !m_expanded[module];
                else
                    module->toggle();
                return;
            }
            y += kRowHeight;

            if (!m_expanded[module])
                continue;

            for (const auto& setting : module->settings()) {
                if (!setting->visible())
                    continue;

                const Rect settingRow{header.left, y, header.right, y + kSettingHeight};
                if (settingRow.contains(cursor)) {
                    applySettingClick(*setting, *module, settingRow, cursor, right);
                    return;
                }
                y += kSettingHeight;
            }
        }
    }
}

void ClickGui::applySettingClick(Setting& setting, Module& module, const Rect& row, const Vec2& cursor,
                                 bool right) {
    switch (setting.type()) {
    case Setting::Type::Bool:
        static_cast<BoolSetting&>(setting).toggle();
        break;

    case Setting::Type::Enum:
        static_cast<EnumSetting&>(setting).cycle(right ? -1 : 1);
        break;

    case Setting::Type::Keybind:
        if (right) {
            static_cast<KeybindSetting&>(setting).value = 0;
            m_bindingModule = nullptr;
            input::InputManager::get().setCapture(false);
        } else {
            m_bindingModule = &module;
            // Only now are keybinds suppressed, so the key being assigned does
            // not also toggle whatever it is currently bound to.
            input::InputManager::get().setCapture(true);
        }
        break;

    case Setting::Type::Float:
    case Setting::Type::Int: {
        m_draggingSlider = &setting;
        m_draggingSliderRect = sliderTrack(row);
        // Jump straight to the clicked position rather than waiting for a drag.
        const float t = std::clamp((cursor.x - m_draggingSliderRect.left) / m_draggingSliderRect.width(),
                                   0.0f, 1.0f);
        if (auto* asFloat = dynamic_cast<FloatSetting*>(&setting))
            asFloat->setFraction(t);
        else if (auto* asInt = dynamic_cast<IntSetting*>(&setting))
            asInt->setFraction(t);
        break;
    }

    case Setting::Type::Colour:
    case Setting::Type::Text:
        break;
    }
}

void ClickGui::onKey(KeyEvent& event) {
    if (!m_open || !event.down)
        return;

    if (m_bindingModule) {
        m_bindingModule->setKeybind(event.key == VK_ESCAPE ? 0 : event.key);
        m_bindingModule = nullptr;
        input::InputManager::get().setCapture(false);
        event.cancel();
        return;
    }

    if (event.key == VK_ESCAPE) {
        close();
        event.cancel();
    }
    // Anything else falls through, so the module's own bind still toggles the
    // menu shut.
}

} // namespace aerial::gui
