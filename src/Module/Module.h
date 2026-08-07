#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Event/EventBus.h"
#include "Module/Setting.h"

namespace aerial {

enum class Category {
    Visuals,
    Interface,
    Input,
    Client,
};

const char* categoryName(Category category);
const char* categoryIcon(Category category);

class Module {
public:
    Module(std::string name, std::string description, Category category, int defaultKey = 0);
    virtual ~Module();

    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;

    const std::string& name() const { return m_name; }
    const std::string& description() const { return m_description; }
    Category category() const { return m_category; }

    virtual std::string displayName() const;

    virtual std::string suffix() const { return {}; }

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    void toggle() { setEnabled(!m_enabled); }

    int keybind() const { return m_keybind->value; }
    void setKeybind(int key) { m_keybind->value = key; }

    virtual bool persistEnabled() const { return true; }

    virtual bool listed() const { return true; }

    const std::vector<std::unique_ptr<Setting>>& settings() const { return m_settings; }
    Setting* findSetting(std::string_view name) const;

    BoolSetting* addBool(std::string name, std::string description, bool value);
    IntSetting* addInt(std::string name, std::string description, int value, int min, int max);
    FloatSetting* addFloat(std::string name, std::string description, float value, float min, float max,
                           float step = 0.1f);
    EnumSetting* addEnum(std::string name, std::string description, std::vector<std::string> options,
                         int defaultIndex = 0);
    ColourSetting* addColour(std::string name, std::string description, Colour value);
    TextSetting* addText(std::string name, std::string description, std::string value);

protected:
    virtual void onEnable() {}
    virtual void onDisable() {}

    template <typename E, typename Self>
    void listen(void (Self::*method)(E&), int priority = kPriorityNormal) {
        auto* self = static_cast<Self*>(this);
        EventBus::get().subscribe<E>(
            this,
            [self, method](E& event) {
                if (self->enabled())
                    (self->*method)(event);
            },
            priority);
    }

    template <typename E, typename Self>
    void listenAlways(void (Self::*method)(E&), int priority = kPriorityNormal) {
        auto* self = static_cast<Self*>(this);
        EventBus::get().subscribe<E>(this, [self, method](E& event) { (self->*method)(event); }, priority);
    }

private:
    template <typename T, typename... Args>
    T* add(Args&&... args) {
        auto setting = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = setting.get();
        m_settings.push_back(std::move(setting));
        return raw;
    }

    std::string m_name;
    std::string m_description;
    Category m_category;
    bool m_enabled = false;

    std::vector<std::unique_ptr<Setting>> m_settings;
    KeybindSetting* m_keybind = nullptr;
};

}
