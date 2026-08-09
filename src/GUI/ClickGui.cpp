#include "GUI/ClickGui.h"

#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

#include "Assets/Resources.h"
#include "Config/Config.h"
#include "Input/InputManager.h"
#include "Module/ModuleManager.h"
#include "Module/Modules/Modules.h"
#include "Render/DrawUtils.h"
#include "SDK/Context.h"
#include "Utils/Platform.h"
#include "Utils/Logger.h"

namespace aerial::gui {
namespace {

using render::DrawUtils;
using Weight = DrawUtils::Weight;
using Align = DrawUtils::Align;

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

constexpr float kSlide = 58.0f;
constexpr float kOpenSeconds = 0.24f;
constexpr float kCloseSeconds = 0.15f;

constexpr float kContentSlide = 22.0f;
constexpr float kContentDelay = 0.18f;

const Category kCategories[] = {Category::Visuals, Category::Interface, Category::Input,
                                Category::Client};

constexpr float kPickerSV = 96.0f;
constexpr float kPickerBar = 12.0f;
constexpr float kPickerGap = 6.0f;

float colourPickerHeight(float scale) {
    return (4.0f + kPickerSV + kPickerGap + kPickerBar + kPickerGap + kPickerBar + 6.0f) * scale;
}

void rgbToHsv(const Colour& c, float& h, float& s, float& v) {
    const float mx = std::max({c.r, c.g, c.b});
    const float mn = std::min({c.r, c.g, c.b});
    const float d = mx - mn;
    v = mx;
    s = mx <= 0.0f ? 0.0f : d / mx;
    if (d <= 1e-6f) {
        h = 0.0f;
        return;
    }
    if (mx == c.r)
        h = 60.0f * std::fmod((c.g - c.b) / d, 6.0f);
    else if (mx == c.g)
        h = 60.0f * ((c.b - c.r) / d + 2.0f);
    else
        h = 60.0f * ((c.r - c.g) / d + 4.0f);
    if (h < 0.0f)
        h += 360.0f;
}

std::string formatValue(const Setting& setting) {
    if (setting.type() == Setting::Type::Float) {
        const auto& value = static_cast<const FloatSetting&>(setting);
        return value.step >= 1.0f ? std::format("{:.0f}", value.value) : std::format("{:.2f}", value.value);
    }
    return std::to_string(static_cast<const IntSetting&>(setting).value);
}

float cardHeightFor(float scale) {
    const float name = DrawUtils::textHeight(15.0f * scale);
    const float description = DrawUtils::textHeight(11.5f * scale);
    return std::max(kCardHeight * scale, name + description + 16.0f * scale);
}

float settingHeightFor(float scale) {
    return std::max(kSettingHeight * scale, DrawUtils::textHeight(12.5f * scale) + 16.0f * scale);
}

float settingHeightFor(const Setting& setting, float scale) {
    const float row = settingHeightFor(scale);
    if (setting.type() != Setting::Type::List)
        return row;

    const auto& list = static_cast<const ListSetting&>(setting);
    return row * (2.0f + static_cast<float>(list.entries.size()));
}

Rect valuePill(const Rect& row, const std::string& value, float scale) {
    const float width =
        std::max(38.0f * scale, DrawUtils::textWidth(value, 12.5f * scale, Weight::Medium) + 18.0f * scale);
    const float height = 21.0f * scale;
    const float centreY = row.top + row.height() * 0.5f;
    return {row.right - kPadding * scale - width, centreY - height * 0.5f,
            row.right - kPadding * scale, centreY + height * 0.5f};
}

Rect sliderTrack(const Rect& row, float scale) {
    const float width = 108.0f * scale;
    const float y = row.top + row.height() * 0.5f;
    const float height = 4.0f * scale;
    return {row.right - kPadding * scale - width, y - height * 0.5f, row.right - kPadding * scale,
            y + height * 0.5f};
}

Rect sliderGrab(const Rect& track, const Rect& row, float scale) {
    return {track.left - 9.0f * scale, row.top, track.right + 9.0f * scale, row.bottom};
}

std::string lowered(std::string value) {
    for (char& c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

bool matchesSearch(const Module& module, const std::string& query) {
    const std::string needle = lowered(query);
    return lowered(module.name()).find(needle) != std::string::npos ||
           lowered(module.description()).find(needle) != std::string::npos;
}

float easeOut(float t) {
    const float inverted = 1.0f - t;
    return 1.0f - inverted * inverted * inverted;
}

float delayed(float t, float delay) {
    return std::clamp((t - delay) / std::max(0.01f, 1.0f - delay), 0.0f, 1.0f);
}

}

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
    if (m_search.empty())
        return ModuleManager::get().byCategory(m_category);

    std::vector<Module*> found;
    for (const auto& module : ModuleManager::get().modules()) {
        if (matchesSearch(*module, m_search))
            found.push_back(module.get());
    }
    return found;
}

void ClickGui::dragSliderTo(float x) {
    if (!m_draggingSlider)
        return;

    const float t = std::clamp((x - m_draggingSliderRect.left) /
                                   std::max(1.0f, m_draggingSliderRect.width()),
                               0.0f, 1.0f);

    if (auto* asFloat = setting_cast<FloatSetting>(m_draggingSlider))
        asFloat->setFraction(t);
    else if (auto* asInt = setting_cast<IntSetting>(m_draggingSlider))
        asInt->setFraction(t);
}

ClickGui::ScrollState& ClickGui::activeScroll() {
    return m_page == Page::Configs ? m_configScroll : m_moduleScroll;
}

void ClickGui::open() {
    auto& context = sdk::Context::get();

    const std::string screen = context.currentScreenName();
    if (screen != "start_screen" && screen != "hud_screen") {
        LOG_DEBUG("ClickGui", "refused to open on screen '{}'", screen);
        return;
    }

    m_open = true;

    if (context.client && context.inGame()) {
        context.client->releaseMouse();
        m_releasedMouse = true;
    }
}

void ClickGui::close() {
    commitText();

    m_open = false;
    m_draggingSlider = nullptr;
    m_draggingColour = 0;
    m_openColour = nullptr;
    m_bindingModule = nullptr;
    m_editingName = false;
    m_searching = false;
    m_search.clear();

    input::InputManager::get().setCapture(false);

    regrabMouse();
}

void ClickGui::regrabMouse() {
    if (!m_releasedMouse)
        return;

    auto* client = sdk::Context::get().client;
    if (!client)
        return;

    if (!platform::gameFocused())
        return;

    client->grabMouse();
    m_releasedMouse = false;
}

int ClickGui::activeCharacter() const {
    const auto* module = ModuleManager::get().find("ClickGui");
    if (!module)
        return 0;

    const auto* choice = setting_cast<const EnumSetting>(module->findSetting("Character"));
    if (!choice || choice->is("None"))
        return 0;

    return choice->is("Asuka") ? AERIAL_ASSET_ASUKA : AERIAL_ASSET_AYANAMI;
}

ClickGui::Layout ClickGui::computeLayout(const Vec2& screenSize, float amount) const {
    Layout layout;
    layout.scale = DrawUtils::uiScale();

    const float fit = std::min(1.0f, (screenSize.y - 40.0f) / (kWindowHeight * layout.scale));
    layout.scale *= fit;

    const float width = kWindowWidth * layout.scale;
    const float height = kWindowHeight * layout.scale;
    const float left = (screenSize.x - width) * 0.5f;

    const float top =
        (screenSize.y - height) * 0.5f + (1.0f - amount) * kSlide * layout.scale;

    layout.window = {left, top, left + width, top + height};
    layout.header = {left, top, left + width, top + kHeaderHeight * layout.scale};
    layout.rail = {left, layout.header.bottom, left + kRailWidth * layout.scale, layout.window.bottom};
    layout.content = {layout.rail.right, layout.header.bottom, layout.window.right,
                      layout.window.bottom};

    layout.cardsRight = layout.content.right;
    return layout;
}

void ClickGui::render(Render2DEvent& event) {

    const float step = frameDelta() / (m_open ? kOpenSeconds : kCloseSeconds);
    m_transition = std::clamp(m_transition + (m_open ? step : -step), 0.0f, 1.0f);

    if (!m_open)
        regrabMouse();

    if (!m_open && m_transition <= 0.0f) {

        m_hits.clear();
        return;
    }

    const float amount = easeOut(m_transition);
    const float contents = easeOut(delayed(m_transition, kContentDelay));

    m_cursor = input::InputManager::get().cursor();
    m_tooltip.clear();
    m_hits.clear();

    const Layout layout = computeLayout(event.screenSize, amount);
    m_contentSlide = (1.0f - contents) * kContentSlide * layout.scale;

    DrawUtils::fill({0.0f, 0.0f, event.screenSize.x, event.screenSize.y},
                    Colour::rgb(0x05070C, 0.55f * amount));

    renderChrome(layout, amount);
    renderCharacter(layout, amount);
    renderRail(layout, amount);

    const size_t contentHits = m_hits.size();

    if (m_page == Page::Configs)
        renderConfigs(layout, amount);
    else
        renderCards(layout, amount);

    const Rect& view = layout.content;
    m_hits.erase(std::remove_if(m_hits.begin() + static_cast<ptrdiff_t>(contentHits), m_hits.end(),
                                [&view](const Hit& hit) {
                                    return hit.area.bottom <= view.top || hit.area.top >= view.bottom;
                                }),
                 m_hits.end());

    if (m_draggingSlider) {
        if (input::InputManager::get().isDown(VK_LBUTTON))
            dragSliderTo(m_cursor.x);
        else
            m_draggingSlider = nullptr;
    }

    if (m_draggingColour) {
        if (input::InputManager::get().isDown(VK_LBUTTON))
            updateColourDrag(m_cursor);
        else
            m_draggingColour = 0;
    }

    renderTooltip(event.screenSize, layout.scale);
    renderCursor(layout.scale);
}

void ClickGui::renderChrome(const Layout& layout, float amount) {
    const Theme& theme = Theme::get();
    const float scale = layout.scale;

    const float radius = kRadius * scale;

    DrawUtils::shadow(layout.window, Colour::rgb(0x000000, 0.55f * amount), 22.0f * scale, radius,
                      {0.0f, 8.0f * scale});

    DrawUtils::blurBehind(layout.window, radius, 18.0f);
    DrawUtils::fill(layout.window, theme.background.withAlpha(theme.background.a * amount), radius);
    DrawUtils::outline(layout.window, Colour::rgb(0xFFFFFF, 0.09f * amount), 1.0f * scale, radius);

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

    const float hintSize = 12.0f * scale;
    const float hintY = title.y + 6.0f * scale;
    const float hintRight = layout.header.right - kPadding * 1.4f * scale;

    const Colour hintText = theme.textDim.withAlpha(0.75f * amount);
    const Colour hintKey = theme.text.withAlpha(0.9f * amount);

    float x = hintRight;
    auto segment = [&](const std::string& value, const Colour& colour, Weight weight, float gap) {
        DrawUtils::text(value, {x, hintY}, colour, hintSize, weight, Align::Right);
        x -= DrawUtils::textWidth(value, hintSize, weight) + gap;
    };

    segment("eject", hintText, Weight::Regular, 5.0f * scale);
    segment("End", hintKey, Weight::SemiBold, 9.0f * scale);
    segment("\xE2\x80\xA2", theme.textDim.withAlpha(0.4f * amount), Weight::Regular, 9.0f * scale);
    segment("close", hintText, Weight::Regular, 5.0f * scale);
    segment(input::InputManager::keyName([] {
                auto* gui = ModuleManager::get().find("ClickGui");
                return gui ? gui->keybind() : 0;
            }()),
            hintKey, Weight::SemiBold, 0.0f);

    const float hintWidth = hintRight - x;

    const float fieldHeight = 26.0f * scale;
    const float fieldRight = layout.header.right - kPadding * 2.2f * scale - hintWidth;
    const float fieldLeft = std::max(title.x + titleWidth + 70.0f * scale,
                                     fieldRight - 260.0f * scale);
    const float centreY = layout.header.top + layout.header.height() * 0.5f;

    if (fieldRight - fieldLeft > 60.0f * scale)
        renderSearch({fieldLeft, centreY - fieldHeight * 0.5f, fieldRight, centreY + fieldHeight * 0.5f},
                     scale, amount);
}

void ClickGui::renderSearch(const Rect& field, float scale, float amount) {
    const Theme& theme = Theme::get();
    const bool hovered = field.contains(m_cursor);
    const float radius = field.height() * 0.5f;

    DrawUtils::fill(field, Colour::rgb(0x000000, m_searching ? 0.34f : (hovered ? 0.26f : 0.18f)),
                    radius);
    DrawUtils::outline(field,
                       m_searching ? theme.menuAccent().withAlpha(0.55f * amount)
                                   : Colour::rgb(0xFFFFFF, (hovered ? 0.14f : 0.07f) * amount),
                       1.0f * scale, radius);

    const float lens = 4.6f * scale;
    const Vec2 eye{field.left + 13.0f * scale, field.top + field.height() * 0.5f};
    const Colour icon = (m_searching ? theme.text : theme.textDim).withAlpha(0.85f * amount);
    DrawUtils::outline({eye.x - lens, eye.y - lens, eye.x + lens, eye.y + lens}, icon, 1.4f * scale,
                       lens);
    DrawUtils::fill({eye.x + lens * 0.6f, eye.y + lens * 0.6f, eye.x + lens * 1.7f,
                     eye.y + lens * 1.7f},
                    icon, 1.0f * scale);

    const float textLeft = eye.x + lens + 8.0f * scale;
    const float textY = field.top + (field.height() - DrawUtils::textHeight(12.5f * scale)) * 0.5f;

    const bool empty = m_search.empty();
    const float textRight = field.right - (empty ? 12.0f * scale : 56.0f * scale);
    const std::string shown = empty ? "Search modules" : m_search;

    DrawUtils::text(DrawUtils::fit(shown, textRight - textLeft, 12.5f * scale), {textLeft, textY},
                    empty ? theme.textDim.withAlpha(0.7f * amount) : theme.text.withAlpha(amount),
                    12.5f * scale, Weight::Regular);

    if (m_searching) {
        const float phase = std::fmod(clockSeconds(), 1.0f);
        if (phase < 0.55f) {
            const float caretX = textLeft + DrawUtils::textWidth(m_search, 12.5f * scale);
            DrawUtils::fill({caretX + 1.0f * scale, field.top + 6.0f * scale, caretX + 2.2f * scale,
                             field.bottom - 6.0f * scale},
                            theme.textActive.withAlpha(amount));
        }
    }

    m_hits.push_back({field, HitKind::SearchField, nullptr, nullptr, Category::Client, {}});

    if (!empty) {
        const float side = 14.0f * scale;
        const Rect clear{field.right - 10.0f * scale - side,
                         field.top + (field.height() - side) * 0.5f, field.right - 10.0f * scale,
                         field.top + (field.height() + side) * 0.5f};

        const bool over = clear.contains(m_cursor);
        DrawUtils::fill(clear, Colour::rgb(0xFFFFFF, over ? 0.16f : 0.09f), side * 0.5f);
        DrawUtils::text("x",
                        {clear.left + side * 0.5f,
                         clear.top + (side - DrawUtils::textHeight(11.0f * scale)) * 0.5f},
                        (over ? theme.textActive : theme.textDim).withAlpha(amount), 11.0f * scale,
                        Weight::SemiBold, Align::Centre);

        m_hits.push_back({clear, HitKind::SearchClear, nullptr, nullptr, Category::Client, {}});

        const size_t found = visibleModules().size();
        DrawUtils::text(std::format("{}", found),
                        {clear.left - 6.0f * scale, textY},
                        theme.textDim.withAlpha(0.75f * amount), 11.5f * scale, Weight::Medium,
                        Align::Right);
    }
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
        module ? setting_cast<const FloatSetting>(module->findSetting("Character opacity")) : nullptr;

    const float height = layout.content.height();
    const float width = height * aspect;
    const Rect area{layout.content.right - width, layout.window.bottom - height,
                    layout.content.right, layout.window.bottom};

    DrawUtils::pushClip(layout.window, kRadius * layout.scale);
    DrawUtils::pushClip(layout.content);

    DrawUtils::image(resource, area, (opacity ? opacity->value : 1.0f) * amount);

    DrawUtils::popClip();
    DrawUtils::popClip();
}

void ClickGui::renderRail(const Layout& layout, float amount) {
    const Theme& theme = Theme::get();
    const float scale = layout.scale;

    DrawUtils::fill({layout.rail.right - 0.5f * scale, layout.rail.top + 6.0f * scale,
                     layout.rail.right + 0.5f * scale, layout.rail.bottom - 10.0f * scale},
                    Colour::rgb(0xFFFFFF, 0.06f * amount));

    float y = layout.rail.top + kPadding * scale + m_contentSlide;
    const float itemHeight = kRailItemHeight * scale;

    for (Category category : kCategories) {
        const auto modules = ModuleManager::get().byCategory(category);

        const Rect item{layout.rail.left + kPadding * scale, y,
                        layout.rail.right - kPadding * 0.5f * scale, y + itemHeight};

        const bool selected = m_page == Page::Modules && m_search.empty() && category == m_category;
        const bool hovered = item.contains(m_cursor);
        const bool empty = modules.empty();

        Animated& highlight = animation(&kCategories[static_cast<int>(category)]);
        highlight.set(selected ? 1.0f : (hovered && !empty ? 0.4f : 0.0f));
        const float glow = highlight.update(theme.animationSpeed);

        if (glow > 0.01f) {
            DrawUtils::fill(item, Colour::rgb(0xFFFFFF, 0.07f * glow * amount), 8.0f * scale);

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

    m_hits.push_back({item, HitKind::ConfigsTab, nullptr, nullptr, Category::Client, {}});
}

void ClickGui::renderCards(const Layout& layout, float amount) {
    const Theme& theme = Theme::get();
    const float scale = layout.scale;
    const auto modules = visibleModules();

    DrawUtils::pushClip(layout.content);

    if (modules.empty()) {
        DrawUtils::text(m_search.empty() ? "Nothing here yet" : "No module matches that",
                        {layout.content.left + (layout.cardsRight - layout.content.left) * 0.5f,
                         layout.content.top + layout.content.height() * 0.5f - 10.0f * scale},
                        theme.textDim.withAlpha(0.6f * amount), 14.0f * scale, Weight::Regular,
                        Align::Centre);
        m_moduleScroll.max = 0.0f;
        m_moduleScroll.reset();
        DrawUtils::popClip();
        return;
    }

    m_moduleScroll.position.set(std::clamp(m_moduleScroll.position.target(), 0.0f, m_moduleScroll.max));
    const float scrolled = m_moduleScroll.position.update(theme.animationSpeed);

    const float top = layout.content.top + kPadding * scale;
    float y = top - scrolled + m_contentSlide;
    int index = 0;

    const float right = layout.cardsRight - kPadding * 1.4f * scale -
                        (m_moduleScroll.max > 0.5f ? 9.0f * scale : 0.0f);

    for (Module* module : modules) {
        const float cardHeight = cardHeightFor(scale);
        const bool expanded = m_expanded[module];

        float settingsHeight = 0.0f;
        if (expanded) {
            for (const auto& setting : module->settings()) {
                if (!setting->visible())
                    continue;
                settingsHeight += settingHeightFor(*setting, scale);
                if (setting.get() == static_cast<Setting*>(m_openColour))
                    settingsHeight += colourPickerHeight(scale);
            }
            if (settingsHeight > 0.0f)
                settingsHeight += 8.0f * scale;
        }

        const Rect group{layout.content.left + kPadding * 0.5f * scale, y, right,
                         y + cardHeight + settingsHeight};
        const Rect card{group.left, group.top, group.right, group.top + cardHeight};

        const bool visible =
            group.bottom > layout.content.top && group.top < layout.content.bottom;
        const int cardIndex = index++;

        if (visible) {
            auto& hover = m_hoverAnimations.try_emplace(module, Animated{0.0f}).first->second;
            hover.set(card.contains(m_cursor) ? 1.0f : 0.0f);
            const float lift = hover.update(theme.animationSpeed);

            const float radius = kCardRadius * scale;
            DrawUtils::fill(group, Colour::rgb(0xFFFFFF, 0.045f + 0.035f * lift), radius);
            DrawUtils::outline(group, Colour::rgb(0xFFFFFF, 0.05f + 0.04f * lift), 1.0f * scale,
                               radius);

            renderCard(*module, card, scale, cardIndex);

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
        }

        y = group.bottom + kCardGap * scale;
    }

    const float contentHeight = (y + scrolled - m_contentSlide) - top + kPadding * scale;
    m_moduleScroll.max = std::max(0.0f, contentHeight - layout.content.height());

    renderScrollbar(layout.content, m_moduleScroll, contentHeight, scale, amount);
    DrawUtils::popClip();
}

void ClickGui::renderScrollbar(const Rect& view, const ScrollState& scroll, float contentHeight,
                               float scale, float amount) {
    if (scroll.max <= 0.5f || contentHeight <= 0.0f)
        return;

    const float width = 3.0f * scale;
    const float inset = 6.0f * scale;
    const Rect track{view.right - inset - width, view.top + inset, view.right - inset,
                     view.bottom - inset};

    if (track.height() <= 0.0f)
        return;

    DrawUtils::fill(track, Colour::rgb(0xFFFFFF, 0.05f * amount), width * 0.5f);

    const float fraction = std::clamp(view.height() / contentHeight, 0.0f, 1.0f);
    const float thumbHeight = std::max(26.0f * scale, track.height() * fraction);
    const float travel = track.height() - thumbHeight;
    const float progress = std::clamp(scroll.position.value() / scroll.max, 0.0f, 1.0f);
    const float thumbTop = track.top + travel * progress;

    DrawUtils::fill({track.left, thumbTop, track.right, thumbTop + thumbHeight},
                    Colour::rgb(0xFFFFFF, 0.22f * amount), width * 0.5f);
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

    if (active > 0.01f) {
        DrawUtils::gradient({card.left, card.top, card.left + card.width() * 0.55f, card.bottom},
                            accent.withAlpha(0.16f * active), accent.withAlpha(0.0f), false, radius);
        DrawUtils::fill({card.left, card.top + radius * 0.6f, card.left + 3.0f * scale,
                         card.bottom - radius * 0.6f},
                        accent.withAlpha(active), 1.5f * scale);
    }

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

    Rect track;
    Rect grab;

    switch (setting.type()) {
    case Setting::Type::Bool: {
        const auto& toggle = static_cast<BoolSetting&>(setting);
        Animated& state = animation(&setting);
        state.set(toggle.value ? 1.0f : 0.0f);
        const float on = state.update(theme.animationSpeed);

        const float w = 26.0f * scale;
        const float h = 14.0f * scale;
        const Rect pill{row.right - kPadding * scale - w, row.top + row.height() * 0.5f - h * 0.5f,
                        row.right - kPadding * scale, row.top + row.height() * 0.5f + h * 0.5f};

        DrawUtils::fill(pill, Colour::rgb(0x2A303C).lerp(theme.menuAccent(), on), h * 0.5f);
        const float r = h * 0.5f - 2.0f * scale;
        const float cx = pill.left + r + 2.0f * scale + (pill.width() - r * 2.0f - 4.0f * scale) * on;
        const float cy = pill.top + h * 0.5f;
        DrawUtils::fill({cx - r, cy - r, cx + r, cy + r}, Colour::rgb(0xFFFFFF, 0.95f), r);
        break;
    }
    case Setting::Type::Float:
    case Setting::Type::Int: {
        const float fraction = setting.type() == Setting::Type::Float
                                   ? static_cast<FloatSetting&>(setting).fraction()
                                   : static_cast<IntSetting&>(setting).fraction();

        track = sliderTrack(row, scale);
        grab = sliderGrab(track, row, scale);

        const bool dragging = m_draggingSlider == &setting;
        const bool overGrab = grab.contains(m_cursor);

        DrawUtils::fill(track, Colour::rgb(0x2A303C), track.height() * 0.5f);

        const float filled = track.left + track.width() * fraction;
        DrawUtils::fill({track.left, track.top, filled, track.bottom}, theme.accent,
                        track.height() * 0.5f);

        Animated& lift = animation(&setting);
        lift.set(dragging ? 1.0f : (overGrab ? 0.5f : 0.0f));
        const float grown = lift.update(theme.animationSpeed);

        const float knob = (6.0f + 2.2f * grown) * scale;
        const float cy = track.top + track.height() * 0.5f;

        if (grown > 0.01f) {
            DrawUtils::fill({filled - knob - 3.5f * scale, cy - knob - 3.5f * scale,
                             filled + knob + 3.5f * scale, cy + knob + 3.5f * scale},
                            theme.accent.withAlpha(0.20f * grown), knob + 3.5f * scale);
        }
        DrawUtils::fill({filled - knob, cy - knob, filled + knob, cy + knob},
                        Colour::rgb(0xFFFFFF, 0.96f), knob);

        DrawUtils::text(formatValue(setting), {track.left - 12.0f * scale, label.y},
                        dragging ? theme.textActive : theme.text, 12.5f * scale, Weight::Medium,
                        Align::Right);

        if (hovered && !dragging)
            m_tooltip = "drag to set";
        break;
    }
    case Setting::Type::Enum: {
        const auto& choice = static_cast<EnumSetting&>(setting);
        const Rect pill = valuePill(row, choice.selected(), scale);

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
    case Setting::Type::List:
        return renderList(static_cast<ListSetting&>(setting), module, row, scale);

    case Setting::Type::Text: {
        const auto& textValue = static_cast<TextSetting&>(setting);
        const bool editing = m_editingText == &setting;

        const Rect field{row.left + row.width() * 0.40f, row.top + 5.0f * scale,
                         row.right - kPadding * scale, row.bottom - 5.0f * scale};

        DrawUtils::fill(field, Colour::rgb(0x000000, editing ? 0.35f : 0.22f), 5.0f * scale);
        DrawUtils::outline(field, Colour::rgb(0xFFFFFF, editing ? 0.20f : 0.07f), 1.0f * scale,
                           5.0f * scale);

        const std::string& value = editing ? m_textBuffer : textValue.value;
        const bool empty = value.empty();
        const float inner = field.width() - 14.0f * scale;
        const float textY = field.top + (field.height() - DrawUtils::textHeight(12.0f * scale)) * 0.5f;

        DrawUtils::text(DrawUtils::fit(empty && !editing ? "click to edit" : value, inner,
                                       12.0f * scale),
                        {field.left + 7.0f * scale, textY}, empty ? theme.textDim : theme.text,
                        12.0f * scale, Weight::Regular);

        if (editing && std::fmod(clockSeconds(), 1.0f) < 0.55f) {
            const float caretX =
                field.left + 7.0f * scale +
                std::min(DrawUtils::textWidth(value, 12.0f * scale), inner);
            DrawUtils::fill({caretX + 1.0f * scale, field.top + 4.0f * scale, caretX + 2.5f * scale,
                             field.bottom - 4.0f * scale},
                            theme.textActive);
        }
        break;
    }
    }

    float extra = 0.0f;
    if (setting.type() == Setting::Type::Colour &&
        &setting == static_cast<Setting*>(m_openColour))
        extra = renderColourPicker(static_cast<ColourSetting&>(setting), row, scale);

    m_hits.push_back({row, HitKind::Setting, &module, &setting, m_category, {}, track, grab});
    return row.height() + extra;
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

    m_configScroll.position.set(std::clamp(m_configScroll.position.target(), 0.0f, m_configScroll.max));
    const float scrolled = m_configScroll.position.update(theme.animationSpeed);

    const float left = layout.content.left + kPadding * 0.5f * scale;
    const float right = layout.cardsRight - kPadding * 1.4f * scale -
                        (m_configScroll.max > 0.5f ? 9.0f * scale : 0.0f);
    const float top = layout.content.top + kPadding * scale;
    float y = top - scrolled + m_contentSlide;

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

    m_hits.push_back({field, HitKind::ConfigNameField, nullptr, nullptr, Category::Client, {}});
    m_hits.push_back({createButton, HitKind::ConfigCreate, nullptr, nullptr, Category::Client, {}});

    y = newRow.bottom + kCardGap * scale * 1.6f;

    const auto names = config.list();
    if (names.empty()) {
        DrawUtils::text("No saved configs yet",
                        {left + (right - left) * 0.5f, y + 20.0f * scale},
                        theme.textDim.withAlpha(0.7f * amount), 13.0f * scale, Weight::Regular,
                        Align::Centre);
        m_configScroll.max = 0.0f;
        m_configScroll.reset();
        DrawUtils::popClip();
        return;
    }

    for (const std::string& name : names) {
        const Rect card{left, y, right, y + 42.0f * scale};

        if (card.bottom <= layout.content.top || card.top >= layout.content.bottom) {
            y = card.bottom + kCardGap * scale;
            continue;
        }

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

        m_hits.push_back({loadFrom, HitKind::ConfigLoad, nullptr, nullptr, Category::Client, name});
        m_hits.push_back({saveTo, HitKind::ConfigSave, nullptr, nullptr, Category::Client, name});
        m_hits.push_back({remove, HitKind::ConfigDelete, nullptr, nullptr, Category::Client, name});

        y = card.bottom + kCardGap * scale;
    }

    const float contentHeight = (y + scrolled - m_contentSlide) - top + kPadding * scale;
    m_configScroll.max = std::max(0.0f, contentHeight - layout.content.height());

    renderScrollbar(layout.content, m_configScroll, contentHeight, scale, amount);
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

        DrawUtils::fill({c.x, c.y, c.x + 8.0f, c.y + 2.0f}, Colour::rgb(0xFFFFFF));
        DrawUtils::fill({c.x, c.y, c.x + 2.0f, c.y + 8.0f}, Colour::rgb(0xFFFFFF));
        return;
    }

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

    if (event.button == MouseEvent::Button::ScrollUp ||
        event.button == MouseEvent::Button::ScrollDown) {

        const float step = settingHeightFor(DrawUtils::uiScale());
        const float notches = std::clamp(event.wheel, -3.0f, 3.0f);
        activeScroll().nudge(-notches * step);
        return;
    }

    if (event.button == MouseEvent::Button::Left && !event.down) {
        m_draggingSlider = nullptr;
        m_draggingColour = 0;
        return;
    }
    if (!event.down)
        return;

    if (event.button == MouseEvent::Button::Left || event.button == MouseEvent::Button::Right)
        handleClick(event.position, event.button == MouseEvent::Button::Right);
}

void ClickGui::handleClick(const Vec2& cursor, bool right) {

    bool keepEditing = false;
    bool keepSearching = false;
    bool keepText = false;

    for (auto it = m_hits.rbegin(); it != m_hits.rend(); ++it) {
        if (!it->area.contains(cursor))
            continue;

        auto& config = Config::get();

        switch (it->kind) {
        case HitKind::Category:
            m_page = Page::Modules;

            m_search.clear();
            m_searching = false;
            m_category = it->category;
            m_moduleScroll.reset();
            break;

        case HitKind::ConfigsTab:
            m_page = Page::Configs;
            m_configScroll.reset();
            break;

        case HitKind::SearchField:
            m_page = Page::Modules;
            m_searching = true;
            keepSearching = true;
            input::InputManager::get().setCapture(true);
            break;

        case HitKind::SearchClear:
            m_search.clear();
            m_moduleScroll.reset();
            break;

        case HitKind::Expander:
            if (it->module)
                m_expanded[it->module] = !m_expanded[it->module];
            break;

        case HitKind::Setting:
            if (it->setting && it->module) {
                keepText = it->setting->type() == Setting::Type::Text;
                applySettingClick(*it, cursor, right);
            }
            break;

        case HitKind::ColourSV:
            m_draggingColour = 1;
            updateColourDrag(cursor);
            break;

        case HitKind::ColourHue:
            m_draggingColour = 2;
            updateColourDrag(cursor);
            break;

        case HitKind::ColourAlpha:
            m_draggingColour = 3;
            updateColourDrag(cursor);
            break;

        case HitKind::ListCell: {
            if (!it->setting)
                break;

            auto& list = *static_cast<ListSetting*>(it->setting);
            if (it->index < 0 || it->index >= static_cast<int>(list.entries.size()))
                break;

            const ListSetting::Entry& entry = list.entries[static_cast<size_t>(it->index)];
            const std::string& current = it->side == 0 ? entry.from : entry.to;

            if (right) {
                commitText();
                (it->side == 0 ? list.entries[static_cast<size_t>(it->index)].from
                               : list.entries[static_cast<size_t>(it->index)].to)
                    .clear();
                break;
            }

            beginEditing(list, it->index, it->side, current);
            keepText = true;
            break;
        }

        case HitKind::ListRemove: {
            if (!it->setting)
                break;

            auto& list = *static_cast<ListSetting*>(it->setting);
            if (it->index < 0 || it->index >= static_cast<int>(list.entries.size()))
                break;

            if (m_editingText == it->setting) {
                m_editingText = nullptr;
                m_editingIndex = -1;
                m_textBuffer.clear();
                input::InputManager::get().setCapture(false);
            }
            list.entries.erase(list.entries.begin() + it->index);
            break;
        }

        case HitKind::ListAdd: {
            if (!it->setting)
                break;

            auto& list = *static_cast<ListSetting*>(it->setting);
            commitText();
            list.entries.emplace_back();
            beginEditing(list, static_cast<int>(list.entries.size()) - 1, 0, {});
            keepText = true;
            break;
        }

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

    if (!keepEditing && m_editingName)
        m_editingName = false;
    if (!keepSearching && m_searching)
        m_searching = false;
    if (!keepText)
        commitText();

    if (!m_editingName && !m_searching && !m_bindingModule && !m_editingText)
        input::InputManager::get().setCapture(false);
}

void ClickGui::applySettingClick(const Hit& hit, const Vec2& cursor, bool right) {
    Setting& setting = *hit.setting;
    Module& module = *hit.module;

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
    case Setting::Type::Int:

        if (!hit.grab.contains(cursor))
            break;

        m_draggingSlider = &setting;
        m_draggingSliderRect = hit.track;
        dragSliderTo(cursor.x);
        break;

    case Setting::Type::Text:
        if (right) {
            static_cast<TextSetting&>(setting).value.clear();
            if (m_editingText == &setting)
                m_editingText = nullptr;
            break;
        }

        beginEditing(setting, -1, 0, static_cast<TextSetting&>(setting).value);
        break;

    case Setting::Type::Colour:
        if (m_openColour == &static_cast<ColourSetting&>(setting))
            m_openColour = nullptr;
        else
            openColourPicker(static_cast<ColourSetting&>(setting));
        break;
    }
}

void ClickGui::openColourPicker(ColourSetting& setting) {
    m_openColour = &setting;
    rgbToHsv(setting.value, m_pickerHue, m_pickerSat, m_pickerVal);
    m_pickerAlpha = setting.value.a;
}

void ClickGui::commitPickerColour() {
    if (!m_openColour)
        return;
    m_openColour->value = Colour::hsv(m_pickerHue, m_pickerSat, m_pickerVal, m_pickerAlpha);
}

void ClickGui::updateColourDrag(const Vec2& cursor) {
    if (m_draggingColour == 1) {
        const Rect& sv = m_colourSVRect;
        m_pickerSat = std::clamp((cursor.x - sv.left) / std::max(1.0f, sv.width()), 0.0f, 1.0f);
        m_pickerVal =
            std::clamp(1.0f - (cursor.y - sv.top) / std::max(1.0f, sv.height()), 0.0f, 1.0f);
    } else if (m_draggingColour == 2) {
        const Rect& hue = m_colourHueRect;
        m_pickerHue = std::clamp((cursor.x - hue.left) / std::max(1.0f, hue.width()), 0.0f, 1.0f) *
                      360.0f;
    } else if (m_draggingColour == 3) {
        const Rect& alpha = m_colourAlphaRect;
        m_pickerAlpha = std::clamp((cursor.x - alpha.left) / std::max(1.0f, alpha.width()), 0.0f, 1.0f);
    } else {
        return;
    }
    commitPickerColour();
}

float ClickGui::renderColourPicker(ColourSetting& setting, const Rect& row, float scale) {
    const float pad = kPadding * scale;
    const float gap = kPickerGap * scale;
    const float top = row.bottom + 4.0f * scale;
    const float left = row.left + pad;
    const float right = row.right - pad;

    const Rect sv{left, top, right, top + kPickerSV * scale};
    const Rect hue{left, sv.bottom + gap, right, sv.bottom + gap + kPickerBar * scale};
    const Rect alpha{left, hue.bottom + gap, right, hue.bottom + gap + kPickerBar * scale};

    m_colourSVRect = sv;
    m_colourHueRect = hue;
    m_colourAlphaRect = alpha;

    const float radius = 5.0f * scale;

    DrawUtils::fill(sv, Colour::hsv(m_pickerHue, 1.0f, 1.0f), radius);
    DrawUtils::gradient(sv, Colour::rgb(0xFFFFFF, 1.0f), Colour::rgb(0xFFFFFF, 0.0f), false, radius);
    DrawUtils::gradient(sv, Colour::rgb(0x000000, 0.0f), Colour::rgb(0x000000, 1.0f), true, radius);
    DrawUtils::outline(sv, Colour::rgb(0xFFFFFF, 0.15f), 1.0f * scale, radius);

    const float mx = sv.left + std::clamp(m_pickerSat, 0.0f, 1.0f) * sv.width();
    const float my = sv.top + (1.0f - std::clamp(m_pickerVal, 0.0f, 1.0f)) * sv.height();
    const float mr = 4.0f * scale;
    DrawUtils::outline({mx - mr, my - mr, mx + mr, my + mr}, Colour::rgb(0xFFFFFF, 0.95f), 1.5f * scale,
                       mr);

    const float segW = hue.width() / 6.0f;
    for (int i = 0; i < 6; ++i) {
        const Rect seg{hue.left + segW * static_cast<float>(i), hue.top,
                       hue.left + segW * static_cast<float>(i + 1), hue.bottom};
        DrawUtils::gradient(seg, Colour::hsv(static_cast<float>(i) * 60.0f, 1.0f, 1.0f),
                            Colour::hsv(static_cast<float>(i + 1) * 60.0f, 1.0f, 1.0f), false, 0.0f);
    }
    DrawUtils::outline(hue, Colour::rgb(0xFFFFFF, 0.15f), 1.0f * scale, hue.height() * 0.5f);
    const float hx = hue.left + (m_pickerHue / 360.0f) * hue.width();
    DrawUtils::fill({hx - 1.5f * scale, hue.top - 2.0f * scale, hx + 1.5f * scale, hue.bottom + 2.0f * scale},
                    Colour::rgb(0xFFFFFF, 0.95f), 1.5f * scale);

    const Colour full = Colour::hsv(m_pickerHue, m_pickerSat, m_pickerVal, 1.0f);
    DrawUtils::gradient(alpha, full.withAlpha(0.0f), full.withAlpha(1.0f), false, alpha.height() * 0.5f);
    DrawUtils::outline(alpha, Colour::rgb(0xFFFFFF, 0.15f), 1.0f * scale, alpha.height() * 0.5f);
    const float ax = alpha.left + std::clamp(m_pickerAlpha, 0.0f, 1.0f) * alpha.width();
    DrawUtils::fill({ax - 1.5f * scale, alpha.top - 2.0f * scale, ax + 1.5f * scale,
                     alpha.bottom + 2.0f * scale},
                    Colour::rgb(0xFFFFFF, 0.95f), 1.5f * scale);

    m_hits.push_back({sv, HitKind::ColourSV, nullptr, &setting, m_category, {}});
    m_hits.push_back({hue, HitKind::ColourHue, nullptr, &setting, m_category, {}});
    m_hits.push_back({alpha, HitKind::ColourAlpha, nullptr, &setting, m_category, {}});

    return colourPickerHeight(scale);
}

float ClickGui::renderList(ListSetting& list, Module& module, const Rect& header, float scale) {
    const Theme& theme = Theme::get();
    const float rowHeight = settingHeightFor(scale);
    const float fontSize = 12.0f * scale;

    const auto cell = [&](const Rect& area, const std::string& text, const std::string& hint,
                          bool editing) {
        DrawUtils::fill(area, Colour::rgb(0x000000, editing ? 0.35f : 0.22f), 5.0f * scale);
        DrawUtils::outline(area, Colour::rgb(0xFFFFFF, editing ? 0.20f : 0.07f), 1.0f * scale,
                           5.0f * scale);

        const bool empty = text.empty();
        const float inner = area.width() - 14.0f * scale;
        const float textY = area.top + (area.height() - DrawUtils::textHeight(fontSize)) * 0.5f;

        if (!empty || !editing) {
            DrawUtils::text(DrawUtils::fit(empty ? hint : text, inner, fontSize),
                            {area.left + 7.0f * scale, textY}, empty ? theme.textDim : theme.text,
                            fontSize, Weight::Regular);
        }

        if (editing && std::fmod(clockSeconds(), 1.0f) < 0.55f) {
            const float caretX = area.left + 7.0f * scale +
                                 std::min(DrawUtils::textWidth(text, fontSize), inner);
            DrawUtils::fill({caretX + 1.0f * scale, area.top + 4.0f * scale, caretX + 2.5f * scale,
                             area.bottom - 4.0f * scale},
                            theme.textActive);
        }
    };

    float y = header.bottom;
    const float left = header.left + 24.0f * scale;
    const float buttonSize = 17.0f * scale;
    const float arrow = 16.0f * scale;

    for (size_t i = 0; i < list.entries.size(); ++i) {
        ListSetting::Entry& entry = list.entries[i];
        const int index = static_cast<int>(i);

        const float top = y + 4.0f * scale;
        const float bottom = y + rowHeight - 4.0f * scale;
        const float centreY = (top + bottom) * 0.5f;

        const Rect remove{header.right - kPadding * scale - buttonSize, centreY - buttonSize * 0.5f,
                          header.right - kPadding * scale, centreY + buttonSize * 0.5f};

        const float fieldsRight = remove.left - 7.0f * scale;
        const float half = (fieldsRight - left - arrow) * 0.5f;

        const Rect from{left, top, left + half, bottom};
        const Rect to{from.right + arrow, top, fieldsRight, bottom};

        const bool editingRow = m_editingText == &list && m_editingIndex == index;
        const bool editingFrom = editingRow && m_editingSide == 0;
        const bool editingTo = editingRow && m_editingSide == 1;

        cell(from, editingFrom ? m_textBuffer : entry.from, list.fromHint, editingFrom);
        cell(to, editingTo ? m_textBuffer : entry.to, list.toHint, editingTo);

        DrawUtils::text("->", {from.right + arrow * 0.5f, centreY - DrawUtils::textHeight(fontSize) * 0.5f},
                        theme.textDim, fontSize, Weight::Regular, Align::Centre);

        const bool overRemove = remove.contains(m_cursor);
        DrawUtils::fill(remove, Colour::rgb(0xE05A5A, overRemove ? 0.30f : 0.14f),
                        buttonSize * 0.5f);
        DrawUtils::text("x",
                        {remove.left + remove.width() * 0.5f,
                         remove.top + (remove.height() - DrawUtils::textHeight(fontSize)) * 0.5f},
                        Colour::rgb(0xFFB4B4, overRemove ? 1.0f : 0.75f), fontSize, Weight::SemiBold,
                        Align::Centre);

        m_hits.push_back({from, HitKind::ListCell, &module, &list, m_category, {}, {}, {}, index, 0});
        m_hits.push_back({to, HitKind::ListCell, &module, &list, m_category, {}, {}, {}, index, 1});
        m_hits.push_back(
            {remove, HitKind::ListRemove, &module, &list, m_category, {}, {}, {}, index, 0});

        y += rowHeight;
    }

    const Rect add{left, y + 4.0f * scale, header.right - kPadding * scale,
                   y + rowHeight - 4.0f * scale};
    const bool overAdd = add.contains(m_cursor);

    DrawUtils::fill(add, Colour::rgb(0xFFFFFF, overAdd ? 0.10f : 0.05f), 5.0f * scale);
    DrawUtils::outline(add, Colour::rgb(0xFFFFFF, overAdd ? 0.16f : 0.07f), 1.0f * scale,
                       5.0f * scale);
    DrawUtils::text("+ add",
                    {add.left + add.width() * 0.5f,
                     add.top + (add.height() - DrawUtils::textHeight(fontSize)) * 0.5f},
                    overAdd ? theme.text : theme.textDim, fontSize, Weight::Medium, Align::Centre);

    m_hits.push_back({add, HitKind::ListAdd, &module, &list, m_category, {}, {}, {}, -1, 0});

    y += rowHeight;
    return y - header.top;
}

void ClickGui::beginEditing(Setting& setting, int index, int side, std::string current) {
    commitText();

    m_editingText = &setting;
    m_editingIndex = index;
    m_editingSide = side;
    m_textBuffer = std::move(current);
    input::InputManager::get().setCapture(true);
}

void ClickGui::commitText() {
    if (!m_editingText)
        return;

    if (m_editingText->type() == Setting::Type::Text) {
        static_cast<TextSetting*>(m_editingText)->value = m_textBuffer;
    } else if (m_editingText->type() == Setting::Type::List) {
        auto& list = *static_cast<ListSetting*>(m_editingText);
        if (m_editingIndex >= 0 && m_editingIndex < static_cast<int>(list.entries.size())) {
            ListSetting::Entry& entry = list.entries[static_cast<size_t>(m_editingIndex)];
            (m_editingSide == 0 ? entry.from : entry.to) = m_textBuffer;
        }
    }

    m_editingText = nullptr;
    m_editingIndex = -1;
    m_editingSide = 0;
    m_textBuffer.clear();
    input::InputManager::get().setCapture(false);
}

void ClickGui::popCharacter(std::string& text) {
    if (text.empty())
        return;

    size_t last = text.size() - 1;
    while (last > 0 && (static_cast<uint8_t>(text[last]) & 0xC0) == 0x80)
        --last;
    text.erase(last);
}

void ClickGui::onKey(KeyEvent& event) {
    if (!m_open || !event.down)
        return;

    if (m_searching) {
        event.cancel();

        switch (event.key) {
        case VK_ESCAPE:

            m_searching = false;
            m_search.clear();
            m_moduleScroll.reset();
            input::InputManager::get().setCapture(false);
            return;

        case VK_RETURN:
            m_searching = false;
            input::InputManager::get().setCapture(false);
            return;

        case VK_BACK:
            if (!m_search.empty()) {
                m_search.pop_back();
                m_moduleScroll.reset();
            }
            return;

        default:
            break;
        }

        if (m_search.size() < 24) {
            const std::string typed = input::InputManager::characterFor(event.key);
            if (!typed.empty()) {
                m_search += typed;
                m_moduleScroll.reset();
            }
        }
        return;
    }

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

        if (m_nameBuffer.size() < 32)
            m_nameBuffer += input::InputManager::characterFor(event.key);
        return;
    }

    if (m_editingText) {
        event.cancel();

        switch (event.key) {
        case VK_ESCAPE:
            m_editingText = nullptr;
            input::InputManager::get().setCapture(false);
            return;

        case VK_RETURN:
        case VK_TAB: {
            Setting* const setting = m_editingText;
            const int index = m_editingIndex;
            const bool toSecond = setting->type() == Setting::Type::List && m_editingSide == 0;

            commitText();

            if (toSecond) {
                auto& list = *static_cast<ListSetting*>(setting);
                if (index >= 0 && index < static_cast<int>(list.entries.size()))
                    beginEditing(*setting, index, 1, list.entries[static_cast<size_t>(index)].to);
            }
            return;
        }

        case VK_BACK:
            popCharacter(m_textBuffer);
            return;

        default:
            break;
        }

        if (m_textBuffer.size() < 240)
            m_textBuffer += input::InputManager::characterFor(event.key);
        return;
    }

    if (m_bindingModule) {
        m_bindingModule->setKeybind(event.key == VK_ESCAPE ? 0 : event.key);
        m_bindingModule = nullptr;
        input::InputManager::get().setCapture(false);
        event.cancel();
        return;
    }

    ScrollState& scroll = activeScroll();
    const float page = settingHeightFor(DrawUtils::uiScale()) * 6.0f;

    switch (event.key) {
    case VK_PRIOR:
        scroll.nudge(-page);
        event.cancel();
        return;
    case VK_NEXT:
        scroll.nudge(page);
        event.cancel();
        return;
    case VK_HOME:
        scroll.position.set(0.0f);
        event.cancel();
        return;
    case VK_END:

        break;
    default:
        break;
    }

    if (event.key == VK_ESCAPE) {
        close();
        event.cancel();
    }
}

}
