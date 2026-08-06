#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Event/EventBus.h"
#include "Module/Setting.h"

namespace aerial {

enum class Category {
    Combat,
    Movement,
    Player,
    World,
    Render,
    Misc,
};

const char* categoryName(Category category);
const char* categoryIcon(Category category);

// Base class for every feature.
//
//   class Sprint final : public Module {
//   public:
//       Sprint() : Module("Sprint", "Always sprint", Category::Movement) {
//           m_multiplier = addFloat("Multiplier", "Speed multiplier", 1.0f, 1.0f, 2.0f);
//           listen<TickEvent>(&Sprint::onTick);
//       }
//   private:
//       FloatSetting* m_multiplier;
//       void onTick(TickEvent& e) { ... }
//   };
//
// Handlers registered through listen() only fire while the module is enabled,
// and are torn down automatically when it is destroyed.
class Module {
public:
    Module(std::string name, std::string description, Category category, int defaultKey = 0);
    virtual ~Module();

    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;

    const std::string& name() const { return m_name; }
    const std::string& description() const { return m_description; }
    Category category() const { return m_category; }

    // Shown in the array list; override to append state, e.g. "Speed [Vanilla]".
    virtual std::string displayName() const;

    // Short state suffix used by the default displayName().
    virtual std::string suffix() const { return {}; }

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    void toggle() { setEnabled(!m_enabled); }

    int keybind() const { return m_keybind->value; }
    void setKeybind(int key) { m_keybind->value = key; }

    // Modules that should not be persisted as "on" across restarts (e.g. the
    // ClickGUI itself) override this.
    virtual bool persistEnabled() const { return true; }

    const std::vector<std::unique_ptr<Setting>>& settings() const { return m_settings; }
    Setting* findSetting(std::string_view name) const;

    // ── Setting registration (call from the constructor) ─────────────────────
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

    // Subscribes a member function to an event for this module's lifetime.
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

    // Same, but fires even while the module is disabled — for modules that need
    // to observe state continuously (keybind listeners, HUD elements).
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

} // namespace aerial
