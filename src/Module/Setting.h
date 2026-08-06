#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "Utils/Math.h"

namespace aerial {

// A module setting: the unit the ClickGUI renders and the config file stores.
// Concrete settings expose a plain `value` member, so module code reads them
// with zero ceremony:
//
//     if (m_autoBlock->value) ...
//     const float reach = m_range->value;
//
class Setting {
public:
    enum class Type { Bool, Int, Float, Enum, Colour, Keybind, Text };

    Setting(Type type, std::string name, std::string description)
        : m_type(type), m_name(std::move(name)), m_description(std::move(description)) {}
    virtual ~Setting() = default;

    Type type() const { return m_type; }
    const std::string& name() const { return m_name; }
    const std::string& description() const { return m_description; }

    // Hides the setting in the ClickGUI while the predicate is false. Chainable:
    //     addFloat("Range", ...)->onlyIf([this] { return m_mode->is("Custom"); });
    Setting* onlyIf(std::function<bool()> predicate) {
        m_visible = std::move(predicate);
        return this;
    }
    bool visible() const { return !m_visible || m_visible(); }

    virtual void resetToDefault() = 0;

private:
    Type m_type;
    std::string m_name;
    std::string m_description;
    std::function<bool()> m_visible;
};

class BoolSetting final : public Setting {
public:
    BoolSetting(std::string name, std::string description, bool defaultValue)
        : Setting(Type::Bool, std::move(name), std::move(description)),
          value(defaultValue), defaultValue(defaultValue) {}

    void resetToDefault() override { value = defaultValue; }
    void toggle() { value = !value; }

    bool value;
    bool defaultValue;
};

class IntSetting final : public Setting {
public:
    IntSetting(std::string name, std::string description, int defaultValue, int min, int max)
        : Setting(Type::Int, std::move(name), std::move(description)),
          value(defaultValue), defaultValue(defaultValue), min(min), max(max) {}

    void resetToDefault() override { value = defaultValue; }
    void set(int v) { value = std::clamp(v, min, max); }
    float fraction() const { return max > min ? static_cast<float>(value - min) / (max - min) : 0.0f; }
    void setFraction(float t) { set(min + static_cast<int>(std::lround(t * (max - min)))); }

    int value;
    int defaultValue;
    int min;
    int max;
};

class FloatSetting final : public Setting {
public:
    FloatSetting(std::string name, std::string description, float defaultValue, float min, float max,
                 float step = 0.1f)
        : Setting(Type::Float, std::move(name), std::move(description)),
          value(defaultValue), defaultValue(defaultValue), min(min), max(max), step(step) {}

    void resetToDefault() override { value = defaultValue; }

    void set(float v) {
        if (step > 0.0f)
            v = min + std::round((v - min) / step) * step;
        value = std::clamp(v, min, max);
    }

    float fraction() const { return max > min ? (value - min) / (max - min) : 0.0f; }
    void setFraction(float t) { set(min + t * (max - min)); }

    float value;
    float defaultValue;
    float min;
    float max;
    float step;
};

class EnumSetting final : public Setting {
public:
    EnumSetting(std::string name, std::string description, std::vector<std::string> options, int defaultIndex)
        : Setting(Type::Enum, std::move(name), std::move(description)),
          value(defaultIndex), defaultValue(defaultIndex), options(std::move(options)) {}

    void resetToDefault() override { value = defaultValue; }

    const std::string& selected() const { return options[static_cast<size_t>(value)]; }
    bool is(std::string_view option) const { return selected() == option; }

    void cycle(int direction = 1) {
        const int count = static_cast<int>(options.size());
        value = ((value + direction) % count + count) % count;
    }

    int value;
    int defaultValue;
    std::vector<std::string> options;
};

class ColourSetting final : public Setting {
public:
    ColourSetting(std::string name, std::string description, Colour defaultValue)
        : Setting(Type::Colour, std::move(name), std::move(description)),
          value(defaultValue), defaultValue(defaultValue) {}

    void resetToDefault() override { value = defaultValue; }

    Colour value;
    Colour defaultValue;
};

class KeybindSetting final : public Setting {
public:
    KeybindSetting(std::string name, std::string description, int defaultKey)
        : Setting(Type::Keybind, std::move(name), std::move(description)),
          value(defaultKey), defaultValue(defaultKey) {}

    void resetToDefault() override { value = defaultValue; }

    int value;  // virtual-key code, 0 = unbound
    int defaultValue;
};

class TextSetting final : public Setting {
public:
    TextSetting(std::string name, std::string description, std::string defaultValue)
        : Setting(Type::Text, std::move(name), std::move(description)),
          value(defaultValue), defaultValue(std::move(defaultValue)) {}

    void resetToDefault() override { value = defaultValue; }

    std::string value;
    std::string defaultValue;
};

} // namespace aerial
