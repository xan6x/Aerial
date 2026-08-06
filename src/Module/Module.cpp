#include "Module/Module.h"

#include <algorithm>

#include "Event/Events.h"
#include "Utils/Logger.h"

namespace aerial {

const char* categoryName(Category category) {
    switch (category) {
    case Category::Combat:   return "Combat";
    case Category::Movement: return "Movement";
    case Category::Player:   return "Player";
    case Category::World:    return "World";
    case Category::Render:   return "Render";
    case Category::Misc:     return "Misc";
    }
    return "Unknown";
}

const char* categoryIcon(Category category) {
    switch (category) {
    case Category::Combat:   return "\xE2\x9A\x94";  // crossed swords
    case Category::Movement: return "\xE2\x86\x92";  // arrow
    case Category::Player:   return "\xE2\x98\xBB";  // person
    case Category::World:    return "\xE2\x97\x89";  // globe
    case Category::Render:   return "\xE2\x97\x88";  // diamond
    case Category::Misc:     return "\xE2\x98\x85";  // star
    }
    return "?";
}

Module::Module(std::string name, std::string description, Category category, int defaultKey)
    : m_name(std::move(name)), m_description(std::move(description)), m_category(category) {
    // Every module owns a keybind setting so the GUI and config treat binds
    // uniformly with the rest of its state.
    m_keybind = add<KeybindSetting>("Keybind", "Toggles this module", defaultKey);
}

Module::~Module() {
    EventBus::get().unsubscribe(this);
}

std::string Module::displayName() const {
    const std::string tag = suffix();
    return tag.empty() ? m_name : m_name + " \xC2\xA7" "7" + tag;
}

void Module::setEnabled(bool enabled) {
    if (m_enabled == enabled)
        return;

    m_enabled = enabled;
    LOG_DEBUG("Module", "{} {}", m_name, enabled ? "enabled" : "disabled");

    if (enabled)
        onEnable();
    else
        onDisable();

    ModuleToggleEvent event;
    event.module = this;
    event.enabled = enabled;
    EventBus::get().dispatch(event);
}

Setting* Module::findSetting(std::string_view name) const {
    const auto it = std::find_if(m_settings.begin(), m_settings.end(),
                                 [name](const auto& setting) { return setting->name() == name; });
    return it != m_settings.end() ? it->get() : nullptr;
}

BoolSetting* Module::addBool(std::string name, std::string description, bool value) {
    return add<BoolSetting>(std::move(name), std::move(description), value);
}

IntSetting* Module::addInt(std::string name, std::string description, int value, int min, int max) {
    return add<IntSetting>(std::move(name), std::move(description), value, min, max);
}

FloatSetting* Module::addFloat(std::string name, std::string description, float value, float min, float max,
                               float step) {
    return add<FloatSetting>(std::move(name), std::move(description), value, min, max, step);
}

EnumSetting* Module::addEnum(std::string name, std::string description, std::vector<std::string> options,
                             int defaultIndex) {
    return add<EnumSetting>(std::move(name), std::move(description), std::move(options), defaultIndex);
}

ColourSetting* Module::addColour(std::string name, std::string description, Colour value) {
    return add<ColourSetting>(std::move(name), std::move(description), value);
}

TextSetting* Module::addText(std::string name, std::string description, std::string value) {
    return add<TextSetting>(std::move(name), std::move(description), std::move(value));
}

} // namespace aerial
