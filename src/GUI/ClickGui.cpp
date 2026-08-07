#include "GUI/ClickGui.h"

#include <Windows.h>
#include <algorithm>
#include <format>

#include "Assets/Resources.h"
#include "Config/Config.h"
#include "Input/InputManager.h"
#include "Module/ModuleManager.h"
#include "Module/Modules/Modules.h"
#include "Render/DrawUtils.h"
#include "SDK/Context.h"
#include "Utils/Logger.h"

namespace aerial::gui {
namespace {

using render::DrawUtils;
using Weight = DrawUtils::Weight;
using Align = DrawUtils::Align;

// Authored against a 1000-unit-tall screen; everything is multiplied by
// DrawUtils::uiScale() so the menu keeps its proportions at any resolution.
// A fixed, generous window. Sizing it to its content made it collapse to a
// squat box whenever a category held few modules, and the empty area is not
// waste - it is where the background character lives.
constexpr float kWindowWidth = 900.0f;
constexpr float kWindowHeight = 560.0f;
constexpr float kRailWidth = 196.0f;
constexpr float kRailItemHeight = 36.0f;
constexpr float kHeaderHeight = 58.0f;
constexpr float kPadding = 14.0f;
constexpr float kCardHeight = 46.0f;
constexpr float kCardGap = 7.0f;
constexpr float kSettingHeight = 30.0f;
constexpr float kRadius = 14.0f;
constexpr float kCardRadius = 9.0f;

const Category kCategories[] = {Category::Combat, Category::Movement, Category::Player,
                                Category::World,  Category::Render,   Category::Misc};

std::string formatValue(const Setting& setting) {
    if (setting.type() == Setting::Type::Float) {
        const auto& value = static_cast<const FloatSetting&>(setting);
        return value.step >= 1.0f ? std::format("{:.0f}", value.value) : std::format("{:.2f}", value.value);
    }
    return std::to_string(static_cast<const IntSetting&>(setting).value);
}

// Row heights follow the text metrics rather than fixed numbers, because the
// fallback backend draws a bitmap font at one fixed size - a card sized purely
// in design units ends up too short for it and the two lines collide.
float cardHeightFor(float scale) {
    const float name = DrawUtils::textHeight(15.0f * scale);
    const float description = DrawUtils::textHeight(11.5f * scale);
    return std::max(kCardHeight * scale, name + description + 16.0f * scale);
}

float settingHeightFor(float scale) {
    return std::max(kSettingHeight * scale, DrawUtils::textHeight(12.5f * scale) + 16.0f * scale);
}

// Right-aligned capsule holding a setting's current value.
Rect valuePill(const Rect& row, const std::string& value, float scale) {
    const float width =
        std::max(38.0f * scale, DrawUtils::textWidth(value, 12.5f * scale, Weight::Medium) + 18.0f * scale);
    const float height = 21.0f * scale;
    const float centreY = row.top + row.height() * 0.5f;
    return {row.right - kPadding * scale - width, centreY - height * 0.5f,
            row.right - kPadding * scale, centreY + height * 0.5f};
}

Rect sliderTrack(const Rect& row, float scale) {
    const float width = 92.0f * scale;
    const float y = row.top + row.height() * 0.5f;
    const float height = 4.0f * scale;
    return {row.right - kPadding * scale - width, y - height * 0.5f, row.right - kPadding * scale,
            y + height * 0.5f};
}

// Ease-out cubic: fast start, soft landing. Used for the open transition.
float easeOut(float t) {
    const float inverted = 1.0f - t;
    return 1.0f - inverted * inverted * inverted;
}

} // namespace

ClickGui& ClickGui::get() {
    static ClickGui instance;
    return instance;
}

Animated& ClickGui::animation(const void* key, float initial) {
    const auto it = m_animations.find(key);
    if (it != m_animations.end())
        return it->second;
    return m_animations.emplace(key, Animated{initial}).first->second;
}

std::vector<Module*> ClickGui::visibleModules() const {
    return ModuleManager::get().byCategory(m_category);
}

void ClickGui::open() {
    m_open = true;
    m_openAnimation.set(1.0f);

    auto& context = sdk::Context::get();
    if (!context.inGame())
        LOG_INFO("ClickGui", "opened outside a world");

    if (context.client && context.inGame()) {
        context.client->releaseMouse();
        m_releasedMouse = true;
    }
}

void ClickGui::close() {
    m_open = false;
    m_openAnimation.set(0.0f);
    m_draggingSlider = nullptr;
    m_bindingModule = nullptr;
    m_editingName = false;

    input::InputManager::get().setCapture(false);

    // Restore the cursor if - and only if - we were the ones who released it.
    // Gating this on inGame() the way open() does was a bug: open in a world and
    // close after leaving it, and the grab never happened, leaving the game in
    // its cursor-released state. That state is also what hides the hand, the
    // hotbar and the chat, so the HUD stayed gone until the next restart.
    auto& context = sdk::Context::get();
    if (m_releasedMouse && context.client) {
        context.client->grabMouse();
        m_releasedMouse = false;
    }
}

int ClickGui::activeCharacter() const {
    const auto* module = ModuleManager::get().find("ClickGui");
    if (!module)
        return 0;

    const auto* choice = dynamic_cast<const EnumSetting*>(module->findSetting("Character"));
    if (!choice || choice->is("None"))
        return 0;

    return choice->is("Asuka") ? AERIAL_ASSET_ASUKA : AERIAL_ASSET_AYANAMI;
}

ClickGui::Layout ClickGui::computeLayout(const Vec2& screenSize) const {
    Layout layout;
    layout.scale = DrawUtils::uiScale();

    // Shrink to fit a short screen rather than hanging off it.
    const float fit = std::min(1.0f, (screenSize.y - 40.0f) / (kWindowHeight * layout.scale));
    layout.scale *= fit;

    const float width = kWindowWidth * layout.scale;
    const float height = kWindowHeight * layout.scale;
    const float left = (screenSize.x - width) * 0.5f;
    const float top = (screenSize.y - height) * 0.5f;

    layout.window = {left, top, left + width, top + height};
    layout.header = {left, top, left + width, top + kHeaderHeight * layout.scale};
    layout.rail = {left, layout.header.bottom, left + kRailWidth * layout.scale, layout.window.bottom};
    layout.content = {layout.rail.right, layout.header.bottom, layout.window.right,
                      layout.window.bottom};

    // Cards span the full content width; the artwork sits behind them and shows
    // through their translucent surface.
    layout.cardsRight = layout.content.right;
    return layout;
}

void ClickGui::render(Render2DEvent& event) {
    const float raw = m_openAnimation.update(Theme::get().animationSpeed);

    // Safety net for the cursor: whatever route the menu took to close, the
    // game must not be left in its released state - that is what hides the
    // hand, the hotbar and the chat.
    if (!m_open && m_releasedMouse) {
        if (auto* client = sdk::Context::get().client) {
            client->grabMouse();
            m_releasedMouse = false;
        }
    }

    if (!m_open && raw <= 0.005f)
        return;

    const float amount = easeOut(std::clamp(raw, 0.0f, 1.0f));

    m_cursor = input::InputManager::get().cursor();
    m_tooltip.clear();
    m_hits.clear();

    const Layout layout = computeLayout(event.screenSize);

    // Scrim over the world, so the menu reads as a focused surface.
    DrawUtils::fill({0.0f, 0.0f, event.screenSize.x, event.screenSize.y},
                    Colour::rgb(0x05070C, 0.55f * amount));

    renderChrome(layout, amount);
    renderCharacter(layout, amount);
    renderRail(layout, amount);

    if (m_page == Page::Configs)
        renderConfigs(layout, amount);
    else
        renderCards(layout, amount);

    if (m_draggingSlider && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
        const float t = std::clamp((m_cursor.x - m_draggingSliderRect.left) /
                                       std::max(1.0f, m_draggingSliderRect.width()),
                                   0.0f, 1.0f);
        if (auto* asFloat = dynamic_cast<FloatSetting*>(m_draggingSlider))
            asFloat->setFraction(t);
        else if (auto* asInt = dynamic_cast<IntSetting*>(m_draggingSlider))
            asInt->setFraction(t);
    } else {
        m_draggingSlider = nullptr;
    }

    renderTooltip(event.screenSize, layout.scale);
    renderCursor(layout.scale);
}

void ClickGui::renderChrome(const Layout& layout, float amount) {
    const Theme& theme = Theme::get();
    const float scale = layout.scale;

    // The window slides up a little as it fades in.
    const float radius = kRadius * scale;

    DrawUtils::shadow(layout.window, Colour::rgb(0x000000, 0.55f * amount), 22.0f * scale, radius,
                      {0.0f, 8.0f * scale});

    DrawUtils::blurBehind(layout.window, radius, 18.0f);
    DrawUtils::fill(layout.window, theme.background.withAlpha(theme.background.a * amount), radius);
    DrawUtils::outline(layout.window, Colour::rgb(0xFFFFFF, 0.09f * amount), 1.0f * scale, radius);

    // Header: wordmark, version, and a hairline separator.
    const Vec2 title{layout.header.left + kPadding * 1.4f * scale,
                     layout.header.top + 17.0f * scale};
    DrawUtils::text("Aerial", title, theme.textActive.withAlpha(amount), 21.0f * scale,
                    Weight::SemiBold);

    const float titleWidth = DrawUtils::textWidth("Aerial", 21.0f * scale, Weight::SemiBold);
    DrawUtils::text("1.1.5", {title.x + titleWidth + 8.0f * scale, title.y + 6.0f * scale},
                    theme.textDim.withAlpha(amount), 12.0f * scale, Weight::Medium);

    DrawUtils::fill({layout.window.left + kPadding * scale, layout.header.bottom - 1.0f * scale,
                     layout.window.right - kPadding * scale, layout.header.bottom},
                    Colour::rgb(0xFFFFFF, 0.07f * amount));

    const std::string hint = std::format("{} to close", input::InputManager::keyName(
                                                            [] {
                                                                auto* gui = ModuleManager::get().find("ClickGui");
                                                                return gui ? gui->keybind() : 0;
                                                            }()));
    DrawUtils::text(hint, {layout.header.right - kPadding * 1.4f * scale, title.y + 6.0f * scale},
                    theme.textDim.withAlpha(0.75f * amount), 12.0f * scale, Weight::Regular,
                    Align::Right);
}

void ClickGui::renderCharacter(const Layout& layout, float amount) {
    const int resource = activeCharacter();
    if (!resource)
        return;

    const float aspect = DrawUtils::imageAspect(resource);
    if (aspect <= 0.0f)
        return;

    const auto* module = ModuleManager::get().find("ClickGui");
    const auto* opacity =
        module ? dynamic_cast<const FloatSetting*>(module->findSetting("Character opacity")) : nullptr;

    // Sized to the content area and anchored to its bottom-right corner. No
    // scrim: the cards above are translucent, so the artwork is meant to read
    // through them rather than being faded out behind them.
    const float height = layout.content.height();
    const float width = height * aspect;
    const Rect area{layout.content.right - width, layout.window.bottom - height,
                    layout.content.right, layout.window.bottom};

    // Two clips: the window's rounded corners, then the content area, so the
    // artwork never rides up into the header.
    DrawUtils::pushClip(layout.window, kRadius * layout.scale);
    DrawUtils::pushClip(layout.content);

    DrawUtils::image(resource, area, (opacity ? opacity->value : 1.0f) * amount);

    DrawUtils::popClip();
    DrawUtils::popClip();
}

void ClickGui::renderRail(const Layout& layout, float amount) {
    const Theme& theme = Theme::get();
    const float scale = layout.scale;

    // Hairline between the rail and the cards, so the two columns read as
    // separate regions rather than as one loose grid.
    DrawUtils::fill({layout.rail.right - 0.5f * scale, layout.rail.top + 6.0f * scale,
                     layout.rail.right + 0.5f * scale, layout.rail.bottom - 10.0f * scale},
                    Colour::rgb(0xFFFFFF, 0.06f * amount));

    float y = layout.rail.top + kPadding * scale;
    const float itemHeight = kRailItemHeight * scale;

    for (Category category : kCategories) {
        const auto modules = ModuleManager::get().byCategory(category);

        const Rect item{layout.rail.left + kPadding * scale, y,
                        layout.rail.right - kPadding * 0.5f * scale, y + itemHeight};

        const bool selected = m_page == Page::Modules && category == m_category;
        const bool hovered = item.contains(m_cursor);
        const bool empty = modules.empty();

        Animated& highlight = animation(&kCategories[static_cast<int>(category)]);
        highlight.set(selected ? 1.0f : (hovered && !empty ? 0.4f : 0.0f));
        const float glow = highlight.update(theme.animationSpeed);

        if (glow > 0.01f) {
            DrawUtils::fill(item, Colour::rgb(0xFFFFFF, 0.07f * glow * amount), 8.0f * scale);
            // Selection pill on the left edge.
            const float pillHeight = item.height() * 0.5f * glow;
            const float centre = item.top + item.height() * 0.5f;
            DrawUtils::fill({item.left - 6.0f * scale, centre - pillHeight * 0.5f,
                             item.left - 3.0f * scale, centre + pillHeight * 0.5f},
                            theme.menuAccent().withAlpha(glow * amount), 2.0f * scale);
        }

        Colour label = selected ? theme.textActive : theme.textDim;
        if (empty)
            label = label.withAlpha(0.35f);

        DrawUtils::text(categoryName(category),
                        {item.left + 12.0f * scale, item.top + 9.0f * scale},
                        label.withAlpha(label.a * amount), 14.0f * scale,
                        selected ? Weight::SemiBold : Weight::Medium);

        if (!empty) {
            DrawUtils::text(std::to_string(modules.size()),
                            {item.right - 10.0f * scale, item.top + 10.0f * scale},
                            theme.textDim.withAlpha(0.6f * amount), 12.0f * scale, Weight::Regular,
                            Align::Right);

            m_hits.push_back({item, HitKind::Category, nullptr, nullptr, category, {}});
        }

        y += itemHeight + 3.0f * scale;
    }

    // Configs live below a separator: they are not a module category, and
    // grouping them with one would imply they are.
    y += 6.0f * scale;
    DrawUtils::fill({layout.rail.left + kPadding * scale + 6.0f * scale, y,
                     layout.rail.right - kPadding * scale, y + 1.0f * scale},
                    Colour::rgb(0xFFFFFF, 0.06f * amount));
    y += 9.0f * scale;

    const Rect item{layout.rail.left + kPadding * scale, y,
                    layout.rail.right - kPadding * 0.5f * scale, y + itemHeight};

    const bool selected = m_page == Page::Configs;
    Animated& highlight = animation(&m_page);
    highlight.set(selected ? 1.0f : (item.contains(m_cursor) ? 0.4f : 0.0f));
    const float glow = highlight.update(theme.animationSpeed);

    if (glow > 0.01f) {
        DrawUtils::fill(item, Colour::rgb(0xFFFFFF, 0.07f * glow * amount), 8.0f * scale);
        const float pillHeight = item.height() * 0.5f * glow;
        const float centre = item.top + item.height() * 0.5f;
        DrawUtils::fill({item.left - 6.0f * scale, centre - pillHeight * 0.5f,
                         item.left - 3.0f * scale, centre + pillHeight * 0.5f},
                        theme.menuAccent().withAlpha(glow * amount), 2.0f * scale);
    }

    DrawUtils::text("Configs", {item.left + 12.0f * scale, item.top + 9.0f * scale},
                    (selected ? theme.textActive : theme.textDim).withAlpha(amount), 14.0f * scale,
                    selected ? Weight::SemiBold : Weight::Medium);

    m_hits.push_back({item, HitKind::ConfigsTab, nullptr, nullptr, Category::Misc, {}});
}

void ClickGui::renderCards(const Layout& layout, float amount) {
    const Theme& theme = Theme::get();
    const float scale = layout.scale;
    const auto modules = visibleModules();

    DrawUtils::pushClip(layout.content);

    if (modules.empty()) {
        DrawUtils::text("Nothing here yet",
                        {layout.content.left + (layout.cardsRight - layout.content.left) * 0.5f,
                         layout.content.top + layout.content.height() * 0.5f - 10.0f * scale},
                        theme.textDim.withAlpha(0.6f * amount), 14.0f * scale, Weight::Regular,
                        Align::Centre);
        DrawUtils::popClip();
        return;
    }

    float y = layout.content.top + kPadding * scale;
    int index = 0;

    for (Module* module : modules) {
        const float cardHeight = cardHeightFor(scale);
        const bool expanded = m_expanded[module];

        // Measure the whole group up front so the card and its settings share
        // one surface. Loose rows floating on the panel read as unrelated to
        // the module they belong to.
        float settingsHeight = 0.0f;
        if (expanded) {
            for (const auto& setting : module->settings()) {
                if (setting->visible())
                    settingsHeight += settingHeightFor(scale);
            }
            if (settingsHeight > 0.0f)
                settingsHeight += 8.0f * scale;
        }

        const Rect group{layout.content.left + kPadding * 0.5f * scale, y,
                         layout.cardsRight - kPadding * 1.4f * scale,
                         y + cardHeight + settingsHeight};
        const Rect card{group.left, group.top, group.right, group.top + cardHeight};

        auto& hover = m_hoverAnimations.try_emplace(module, Animated{0.0f}).first->second;
        hover.set(card.contains(m_cursor) ? 1.0f : 0.0f);
        const float lift = hover.update(theme.animationSpeed);

        // Deliberately translucent: an opaque body would slice the artwork
        // behind it into strips, which is exactly how the previous attempt went
        // wrong. Readability comes from the text weight and shadow instead.
        const float radius = kCardRadius * scale;
        DrawUtils::fill(group, Colour::rgb(0xFFFFFF, 0.045f + 0.035f * lift), radius);
        DrawUtils::outline(group, Colour::rgb(0xFFFFFF, 0.05f + 0.04f * lift), 1.0f * scale, radius);

        renderCard(*module, card, scale, index++);

        if (expanded && settingsHeight > 0.0f) {
            DrawUtils::fill({group.left + 14.0f * scale, card.bottom, group.right - 14.0f * scale,
                             card.bottom + 1.0f * scale},
                            Colour::rgb(0xFFFFFF, 0.06f));

            float rowY = card.bottom + 4.0f * scale;
            for (const auto& setting : module->settings()) {
                if (!setting->visible())
                    continue;
                const Rect row{group.left, rowY, group.right, rowY + settingHeightFor(scale)};
                rowY += renderSetting(*setting, *module, row, scale);
            }
        }

        y = group.bottom + kCardGap * scale;
    }

    (void)amount;
    DrawUtils::popClip();
}

float ClickGui::renderCard(Module& module, const Rect& card, float scale, int index) {
    const Theme& theme = Theme::get();
    const bool hovered = card.contains(m_cursor);
    const bool on = module.enabled();

    Animated& state = animation(&module);
    state.set(on ? 1.0f : 0.0f);
    const float active = state.update(theme.animationSpeed);

    const float radius = kCardRadius * scale;
    const Colour accent = theme.menuAccent(index);

    // The surface belongs to the group drawn by the caller; only the state
    // decoration is drawn here.
    if (active > 0.01f) {
        DrawUtils::gradient({card.left, card.top, card.left + card.width() * 0.55f, card.bottom},
                            accent.withAlpha(0.16f * active), accent.withAlpha(0.0f), false, radius);
        DrawUtils::fill({card.left, card.top + radius * 0.6f, card.left + 3.0f * scale,
                         card.bottom - radius * 0.6f},
                        accent.withAlpha(active), 1.5f * scale);
    }

    // Everything right of this belongs to the switch and the expander, so text
    // is fitted to what is left rather than allowed to run under them.
    const float textLeft = card.left + 14.0f * scale;
    const float textRight = card.right - 62.0f * scale -
                            (module.settings().size() > 1 ? 40.0f * scale : 0.0f);
    const float available = std::max(20.0f * scale, textRight - textLeft);

    const std::string tag = module.suffix();
    const float tagWidth =
        tag.empty() ? 0.0f : DrawUtils::textWidth(tag, 12.0f * scale, Weight::Medium) + 7.0f * scale;

    const Vec2 name{textLeft, card.top + 8.0f * scale};
    const std::string fittedName =
        DrawUtils::fit(module.name(), available - tagWidth, 15.0f * scale, Weight::Medium);
    DrawUtils::text(fittedName, name, on ? theme.textActive : theme.text, 15.0f * scale,
                    Weight::Medium);

    if (!tag.empty()) {
        const float width = DrawUtils::textWidth(fittedName, 15.0f * scale, Weight::Medium);
        DrawUtils::text(tag, {name.x + width + 7.0f * scale, name.y + 2.0f * scale},
                        accent.withAlpha(0.9f), 12.0f * scale, Weight::Medium);
    }

    const float descriptionY = name.y + DrawUtils::textHeight(15.0f * scale) + 2.0f * scale;
    DrawUtils::text(DrawUtils::fit(module.description(), available, 11.5f * scale), {name.x, descriptionY},
                    theme.textDim, 11.5f * scale, Weight::Regular);

    // Toggle switch.
    const float switchWidth = 34.0f * scale;
    const float switchHeight = 18.0f * scale;
    const Rect track{card.right - 16.0f * scale - switchWidth,
                     card.top + (card.height() - switchHeight) * 0.5f, card.right - 16.0f * scale,
                     card.top + (card.height() + switchHeight) * 0.5f};

    DrawUtils::fill(track, Colour::rgb(0x2A303C).lerp(accent, active), switchHeight * 0.5f);

    const float knobRadius = switchHeight * 0.5f - 2.5f * scale;
    const float knobCentre =
        track.left + knobRadius + 2.5f * scale + (track.width() - knobRadius * 2.0f - 5.0f * scale) * active;
    const float knobY = track.top + switchHeight * 0.5f;
    DrawUtils::fill({knobCentre - knobRadius, knobY - knobRadius, knobCentre + knobRadius,
                     knobY + knobRadius},
                    Colour::rgb(0xFFFFFF, 0.95f), knobRadius);

    // Expander, only when the module has more than its keybind. It gets its own
    // padded, hoverable square instead of sitting flush against the switch.
    if (module.settings().size() > 1) {
        const float side = 22.0f * scale;
        const float centreY = card.top + card.height() * 0.5f;
        const Rect expander{track.left - 16.0f * scale - side, centreY - side * 0.5f,
                            track.left - 16.0f * scale, centreY + side * 0.5f};

        const bool openNow = m_expanded[&module];
        if (expander.contains(m_cursor))
            DrawUtils::fill(expander, Colour::rgb(0xFFFFFF, 0.07f), 6.0f * scale);

        DrawUtils::text(openNow ? "-" : "+",
                        {expander.left + side * 0.5f,
                         centreY - DrawUtils::textHeight(15.0f * scale) * 0.5f},
                        openNow ? theme.text : theme.textDim, 15.0f * scale, Weight::SemiBold,
                        Align::Centre);

        m_hits.push_back({expander, HitKind::Expander, &module, nullptr, m_category, {}});
    }

    if (hovered && m_bindingModule != &module) {
        const int bind = module.keybind();
        m_tooltip = bind ? std::format("bound to {}", input::InputManager::keyName(bind))
                         : "no keybind";
    }

    m_hits.push_back({card, HitKind::Module, &module, nullptr, m_category, {}});
    return card.height();
}

float ClickGui::renderSetting(Setting& setting, Module& module, const Rect& row, float scale) {
    const Theme& theme = Theme::get();
    const bool hovered = row.contains(m_cursor);

    if (hovered) {
        DrawUtils::fill(row.inset(3.0f * scale), Colour::rgb(0xFFFFFF, 0.045f), 6.0f * scale);
        m_tooltip = setting.description();
    }

    const Vec2 label{row.left + 14.0f * scale,
                     row.top + (row.height() - DrawUtils::textHeight(12.5f * scale)) * 0.5f};
    DrawUtils::text(DrawUtils::fit(setting.name(), row.width() * 0.45f, 12.5f * scale), label,
                    theme.textDim, 12.5f * scale, Weight::Regular);

    switch (setting.type()) {
    case Setting::Type::Bool: {
        const auto& toggle = static_cast<BoolSetting&>(setting);
        Animated& state = animation(&setting);
        state.set(toggle.value ? 1.0f : 0.0f);
        const float on = state.update(theme.animationSpeed);

        const float w = 26.0f * scale;
        const float h = 14.0f * scale;
        const Rect track{row.right - kPadding * scale - w, row.top + row.height() * 0.5f - h * 0.5f,
                         row.right - kPadding * scale, row.top + row.height() * 0.5f + h * 0.5f};

        DrawUtils::fill(track, Colour::rgb(0x2A303C).lerp(theme.menuAccent(), on), h * 0.5f);
        const float r = h * 0.5f - 2.0f * scale;
        const float cx = track.left + r + 2.0f * scale + (track.width() - r * 2.0f - 4.0f * scale) * on;
        const float cy = track.top + h * 0.5f;
        DrawUtils::fill({cx - r, cy - r, cx + r, cy + r}, Colour::rgb(0xFFFFFF, 0.95f), r);
        break;
    }
    case Setting::Type::Float:
    case Setting::Type::Int: {
        const float fraction = setting.type() == Setting::Type::Float
                                   ? static_cast<FloatSetting&>(setting).fraction()
                                   : static_cast<IntSetting&>(setting).fraction();

        const Rect track = sliderTrack(row, scale);
        DrawUtils::fill(track, Colour::rgb(0x2A303C), track.height() * 0.5f);

        const float filled = track.left + track.width() * fraction;
        DrawUtils::fill({track.left, track.top, filled, track.bottom}, theme.accent,
                        track.height() * 0.5f);

        const float knob = 5.5f * scale;
        const float cy = track.top + track.height() * 0.5f;
        DrawUtils::fill({filled - knob, cy - knob, filled + knob, cy + knob},
                        Colour::rgb(0xFFFFFF, 0.96f), knob);

        DrawUtils::text(formatValue(setting), {track.left - 10.0f * scale, label.y}, theme.text,
                        12.5f * scale, Weight::Medium, Align::Right);
        break;
    }
    case Setting::Type::Enum: {
        const auto& choice = static_cast<EnumSetting&>(setting);
        const Rect pill = valuePill(row, choice.selected(), scale);

        // At 6% white these pills were invisible; they need a readable body and
        // an edge to look like something you can click.
        DrawUtils::fill(pill, Colour::rgb(0xFFFFFF, 0.10f), pill.height() * 0.5f);
        DrawUtils::outline(pill, Colour::rgb(0xFFFFFF, 0.08f), 1.0f * scale, pill.height() * 0.5f);
        DrawUtils::text(choice.selected(),
                        {pill.left + pill.width() * 0.5f,
                         pill.top + (pill.height() - DrawUtils::textHeight(12.5f * scale)) * 0.5f},
                        theme.accent, 12.5f * scale, Weight::Medium, Align::Centre);
        break;
    }
    case Setting::Type::Keybind: {
        const auto& bind = static_cast<KeybindSetting&>(setting);
        const bool binding = m_bindingModule == &module;
        const std::string value = binding ? "press a key" : input::InputManager::keyName(bind.value);
        const Rect pill = valuePill(row, value, scale);

        DrawUtils::fill(pill, Colour::rgb(0xFFFFFF, binding ? 0.16f : 0.10f), pill.height() * 0.5f);
        DrawUtils::outline(pill, Colour::rgb(0xFFFFFF, binding ? 0.22f : 0.08f), 1.0f * scale,
                           pill.height() * 0.5f);
        DrawUtils::text(value,
                        {pill.left + pill.width() * 0.5f,
                         pill.top + (pill.height() - DrawUtils::textHeight(12.5f * scale)) * 0.5f},
                        binding ? theme.accent : theme.text, 12.5f * scale, Weight::Medium,
                        Align::Centre);
        if (hovered)
            m_tooltip = "click to rebind, right-click to clear";
        break;
    }
    case Setting::Type::Colour: {
        const auto& colour = static_cast<ColourSetting&>(setting);
        const Rect swatch{row.right - kPadding * scale - 26.0f * scale,
                          row.top + row.height() * 0.5f - 8.0f * scale, row.right - kPadding * scale,
                          row.top + row.height() * 0.5f + 8.0f * scale};
        DrawUtils::fill(swatch, colour.value, 5.0f * scale);
        DrawUtils::outline(swatch, Colour::rgb(0xFFFFFF, 0.15f), 1.0f * scale, 5.0f * scale);
        break;
    }
    case Setting::Type::Text: {
        const auto& textValue = static_cast<TextSetting&>(setting);
        DrawUtils::text(textValue.value, {row.right - kPadding * scale, label.y}, theme.text,
                        12.5f * scale, Weight::Regular, Align::Right);
        break;
    }
    }

    m_hits.push_back({row, HitKind::Setting, &module, &setting, m_category, {}});
    return row.height();
}

bool ClickGui::renderButton(const Rect& area, const std::string& label, float scale,
                            const Colour& tint) {
    const bool hovered = area.contains(m_cursor);
    const float radius = area.height() * 0.5f;

    DrawUtils::fill(area, tint.withAlpha(hovered ? 0.28f : 0.16f), radius);
    DrawUtils::outline(area, tint.withAlpha(hovered ? 0.55f : 0.28f), 1.0f * scale, radius);
    DrawUtils::text(label,
                    {area.left + area.width() * 0.5f,
                     area.top + (area.height() - DrawUtils::textHeight(12.0f * scale)) * 0.5f},
                    tint, 12.0f * scale, Weight::SemiBold, Align::Centre);
    return hovered;
}

void ClickGui::renderConfigs(const Layout& layout, float amount) {
    const Theme& theme = Theme::get();
    const float scale = layout.scale;
    auto& config = Config::get();

    DrawUtils::pushClip(layout.content);

    const float left = layout.content.left + kPadding * 0.5f * scale;
    const float right = layout.cardsRight - kPadding * 1.4f * scale;
    float y = layout.content.top + kPadding * scale;

    // ── New config ───────────────────────────────────────────────────────────
    const float rowHeight = 36.0f * scale;
    const Rect newRow{left, y, right, y + rowHeight};
    DrawUtils::fill(newRow, Colour::rgb(0xFFFFFF, 0.045f), kCardRadius * scale);
    DrawUtils::outline(newRow, Colour::rgb(0xFFFFFF, 0.05f), 1.0f * scale, kCardRadius * scale);

    const float buttonWidth = 74.0f * scale;
    const Rect createButton{newRow.right - 10.0f * scale - buttonWidth, newRow.top + 7.0f * scale,
                            newRow.right - 10.0f * scale, newRow.bottom - 7.0f * scale};

    const Rect field{newRow.left + 10.0f * scale, newRow.top + 6.0f * scale,
                     createButton.left - 10.0f * scale, newRow.bottom - 6.0f * scale};

    DrawUtils::fill(field, Colour::rgb(0x000000, m_editingName ? 0.35f : 0.22f), 6.0f * scale);
    DrawUtils::outline(field, Colour::rgb(0xFFFFFF, m_editingName ? 0.20f : 0.07f), 1.0f * scale,
                       6.0f * scale);

    const bool empty = m_nameBuffer.empty();
    const std::string shown = empty && !m_editingName ? "New config name" : m_nameBuffer;
    const float textY = field.top + (field.height() - DrawUtils::textHeight(13.0f * scale)) * 0.5f;
    DrawUtils::text(DrawUtils::fit(shown, field.width() - 20.0f * scale, 13.0f * scale),
                    {field.left + 9.0f * scale, textY}, empty ? theme.textDim : theme.text,
                    13.0f * scale, Weight::Regular);

    if (m_editingName) {
        // Blinking caret, so it is obvious the field is taking keystrokes.
        const float phase = std::fmod(clockSeconds(), 1.0f);
        if (phase < 0.55f) {
            const float caretX =
                field.left + 9.0f * scale + DrawUtils::textWidth(m_nameBuffer, 13.0f * scale);
            DrawUtils::fill({caretX + 1.0f * scale, field.top + 5.0f * scale, caretX + 2.5f * scale,
                             field.bottom - 5.0f * scale},
                            theme.textActive);
        }
    }

    renderButton(createButton, "Create", scale, theme.menuAccent());

    m_hits.push_back({field, HitKind::ConfigNameField, nullptr, nullptr, Category::Misc, {}});
    m_hits.push_back({createButton, HitKind::ConfigCreate, nullptr, nullptr, Category::Misc, {}});

    y = newRow.bottom + kCardGap * scale * 1.6f;

    // ── Saved configs ────────────────────────────────────────────────────────
    const auto names = config.list();
    if (names.empty()) {
        DrawUtils::text("No saved configs yet",
                        {left + (right - left) * 0.5f, y + 20.0f * scale},
                        theme.textDim.withAlpha(0.7f * amount), 13.0f * scale, Weight::Regular,
                        Align::Centre);
        DrawUtils::popClip();
        return;
    }

    for (const std::string& name : names) {
        const Rect card{left, y, right, y + 42.0f * scale};
        if (card.bottom > layout.content.bottom - 4.0f * scale)
            break;

        const bool active = name == config.current();
        const bool hovered = card.contains(m_cursor);

        DrawUtils::fill(card, Colour::rgb(0xFFFFFF, 0.045f + (hovered ? 0.03f : 0.0f)),
                        kCardRadius * scale);
        DrawUtils::outline(card, Colour::rgb(0xFFFFFF, active ? 0.14f : 0.05f), 1.0f * scale,
                           kCardRadius * scale);

        if (active) {
            DrawUtils::fill({card.left, card.top + 8.0f * scale, card.left + 3.0f * scale,
                             card.bottom - 8.0f * scale},
                            theme.menuAccent(), 1.5f * scale);
        }

        const float nameY = card.top + (card.height() - DrawUtils::textHeight(14.0f * scale)) * 0.5f;
        DrawUtils::text(name, {card.left + 14.0f * scale, nameY},
                        active ? theme.textActive : theme.text, 14.0f * scale,
                        active ? Weight::SemiBold : Weight::Medium);

        if (active) {
            const float width = DrawUtils::textWidth(name, 14.0f * scale, Weight::SemiBold);
            DrawUtils::text("active", {card.left + 22.0f * scale + width, nameY + 1.0f * scale},
                            theme.menuAccent().withAlpha(0.85f), 11.5f * scale, Weight::Medium);
        }

        // Delete sits furthest from Load so a misclick does not destroy a config.
        const float buttonHeight = 22.0f * scale;
        const float buttonY = card.top + (card.height() - buttonHeight) * 0.5f;
        const float small = 58.0f * scale;

        const Rect remove{card.right - 12.0f * scale - small, buttonY,
                          card.right - 12.0f * scale, buttonY + buttonHeight};
        const Rect saveTo{remove.left - 8.0f * scale - small, buttonY, remove.left - 8.0f * scale,
                          buttonY + buttonHeight};
        const Rect loadFrom{saveTo.left - 8.0f * scale - small, buttonY, saveTo.left - 8.0f * scale,
                            buttonY + buttonHeight};

        if (renderButton(loadFrom, "Load", scale, theme.menuAccent()))
            m_tooltip = "apply this config";
        if (renderButton(saveTo, "Save", scale, Colour::rgb(0x9AA3B4)))
            m_tooltip = "overwrite it with the current state";
        if (renderButton(remove, "Delete", scale, Colour::rgb(0xFF6B60)))
            m_tooltip = "delete this config";

        m_hits.push_back({loadFrom, HitKind::ConfigLoad, nullptr, nullptr, Category::Misc, name});
        m_hits.push_back({saveTo, HitKind::ConfigSave, nullptr, nullptr, Category::Misc, name});
        m_hits.push_back({remove, HitKind::ConfigDelete, nullptr, nullptr, Category::Misc, name});

        y = card.bottom + kCardGap * scale;
    }

    DrawUtils::popClip();
}

void ClickGui::renderTooltip(const Vec2& screenSize, float scale) {
    if (m_tooltip.empty())
        return;

    const Theme& theme = Theme::get();
    const float pad = 8.0f * scale;
    const float width = DrawUtils::textWidth(m_tooltip, 12.0f * scale, Weight::Regular) + pad * 2.0f;
    const float height = 22.0f * scale;

    float x = std::min(m_cursor.x + 14.0f * scale, screenSize.x - width - 4.0f);
    float y = std::min(m_cursor.y + 18.0f * scale, screenSize.y - height - 4.0f);

    const Rect box{x, y, x + width, y + height};
    DrawUtils::shadow(box, Colour::rgb(0x000000, 0.5f), 8.0f * scale, 6.0f * scale, {0.0f, 2.0f * scale});
    DrawUtils::fill(box, Colour::rgb(0x11141B, 0.97f), 6.0f * scale);
    DrawUtils::outline(box, Colour::rgb(0xFFFFFF, 0.08f), 1.0f * scale, 6.0f * scale);
    DrawUtils::text(m_tooltip, {box.left + pad, box.top + 4.0f * scale}, theme.text, 12.0f * scale);
}

void ClickGui::renderCursor(float scale) {
    const Vec2 c = m_cursor;

    if (!DrawUtils::usingD2D()) {
        // The fallback backend has no geometry; a crosshair is the best it can do.
        DrawUtils::fill({c.x, c.y, c.x + 8.0f, c.y + 2.0f}, Colour::rgb(0xFFFFFF));
        DrawUtils::fill({c.x, c.y, c.x + 2.0f, c.y + 8.0f}, Colour::rgb(0xFFFFFF));
        return;
    }

    // A real arrow. The dot this replaced was easy to lose against the artwork
    // and gave no sense of where the click actually lands.
    const float s = scale;
    const Vec2 shape[] = {
        {c.x, c.y},
        {c.x, c.y + 15.5f * s},
        {c.x + 4.2f * s, c.y + 11.6f * s},
        {c.x + 7.0f * s, c.y + 17.4f * s},
        {c.x + 9.8f * s, c.y + 16.0f * s},
        {c.x + 7.1f * s, c.y + 10.4f * s},
        {c.x + 12.0f * s, c.y + 10.2f * s},
    };

    // Outline first, as the same shape nudged outwards, so the pointer stays
    // visible on both the pale artwork and the dark panel.
    Vec2 outline[std::size(shape)];
    for (size_t i = 0; i < std::size(shape); ++i) {
        outline[i] = {c.x + (shape[i].x - c.x) * 1.16f + 0.6f * s,
                      c.y + (shape[i].y - c.y) * 1.16f + 0.6f * s};
    }

    DrawUtils::polygon(outline, std::size(outline), Colour::rgb(0x05070C, 0.75f));
    DrawUtils::polygon(shape, std::size(shape), Colour::rgb(0xFFFFFF, 0.98f));
}

void ClickGui::onMouse(MouseEvent& event) {
    if (!m_open)
        return;

    event.cancel();

    if (event.button == MouseEvent::Button::Left && !event.down) {
        m_draggingSlider = nullptr;
        return;
    }
    if (!event.down)
        return;

    if (event.button == MouseEvent::Button::Left || event.button == MouseEvent::Button::Right)
        handleClick(event.position, event.button == MouseEvent::Button::Right);
}

void ClickGui::handleClick(const Vec2& cursor, bool right) {
    // Clicking anywhere but the name field gives up the caret.
    bool keepEditing = false;

    // Later hits are drawn on top, so test in reverse.
    for (auto it = m_hits.rbegin(); it != m_hits.rend(); ++it) {
        if (!it->area.contains(cursor))
            continue;

        auto& config = Config::get();

        switch (it->kind) {
        case HitKind::Category:
            m_page = Page::Modules;
            m_category = it->category;
            break;

        case HitKind::ConfigsTab:
            m_page = Page::Configs;
            break;

        case HitKind::Expander:
            if (it->module)
                m_expanded[it->module] = !m_expanded[it->module];
            break;

        case HitKind::Setting:
            if (it->setting && it->module)
                applySettingClick(*it->setting, *it->module, it->area, cursor, right);
            break;

        case HitKind::Module:
            if (it->module) {
                if (right)
                    m_expanded[it->module] = !m_expanded[it->module];
                else
                    it->module->toggle();
            }
            break;

        case HitKind::ConfigNameField:
            m_editingName = true;
            keepEditing = true;
            input::InputManager::get().setCapture(true);
            break;

        case HitKind::ConfigCreate:
            if (config.create(m_nameBuffer)) {
                modules::Notifications::push("Created " + config.current(),
                                             modules::Notifications::Level::Success);
                m_nameBuffer.clear();
            } else {
                modules::Notifications::push(m_nameBuffer.empty() ? "Enter a name first"
                                                                  : "That name is taken",
                                             modules::Notifications::Level::Warning);
            }
            break;

        case HitKind::ConfigLoad: {
            const bool ok = config.load(it->config);
            modules::Notifications::push(
                ok ? "Loaded " + it->config : "Could not load " + it->config,
                ok ? modules::Notifications::Level::Success : modules::Notifications::Level::Error);
            break;
        }

        case HitKind::ConfigSave: {
            const bool ok = config.save(it->config);
            modules::Notifications::push(
                ok ? "Saved " + it->config : "Could not save " + it->config,
                ok ? modules::Notifications::Level::Success : modules::Notifications::Level::Error);
            break;
        }

        case HitKind::ConfigDelete:
            if (config.remove(it->config))
                modules::Notifications::push("Deleted " + it->config,
                                             modules::Notifications::Level::Info);
            break;
        }
        break;
    }

    if (!keepEditing && m_editingName) {
        m_editingName = false;
        input::InputManager::get().setCapture(false);
    }
}

void ClickGui::applySettingClick(Setting& setting, Module& module, const Rect& row, const Vec2& cursor,
                                 bool right) {
    const float scale = DrawUtils::uiScale();

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
            input::InputManager::get().setCapture(true);
        }
        break;

    case Setting::Type::Float:
    case Setting::Type::Int: {
        m_draggingSlider = &setting;
        m_draggingSliderRect = sliderTrack(row, scale);
        const float t = std::clamp((cursor.x - m_draggingSliderRect.left) /
                                       std::max(1.0f, m_draggingSliderRect.width()),
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

    if (m_editingName) {
        event.cancel();

        switch (event.key) {
        case VK_ESCAPE:
            m_editingName = false;
            m_nameBuffer.clear();
            input::InputManager::get().setCapture(false);
            return;

        case VK_RETURN: {
            auto& config = Config::get();
            if (config.create(m_nameBuffer)) {
                modules::Notifications::push("Created " + config.current(),
                                             modules::Notifications::Level::Success);
                m_nameBuffer.clear();
                m_editingName = false;
                input::InputManager::get().setCapture(false);
            } else {
                modules::Notifications::push(
                    m_nameBuffer.empty() ? "Enter a name first" : "That name is taken",
                    modules::Notifications::Level::Warning);
            }
            return;
        }

        case VK_BACK:
            if (!m_nameBuffer.empty())
                m_nameBuffer.pop_back();
            return;

        default:
            break;
        }

        if (m_nameBuffer.size() < 32) {
            if (const char c = input::InputManager::characterFor(event.key))
                m_nameBuffer.push_back(c);
        }
        return;
    }

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
}

} // namespace aerial::gui
