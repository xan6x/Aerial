#pragma once

#include <deque>
#include <string>
#include <vector>

#include "GUI/Theme.h"
#include "Module/Module.h"

// Every module Aerial ships. Adding one is three steps:
//   1. declare it here,
//   2. implement it in a .cpp under Module/Modules/,
//   3. add<YourModule>() in ModuleManager::registerAll().

namespace aerial {

struct KeyEvent;
struct ModuleToggleEvent;
struct MouseEvent;
struct PacketSendEvent;
struct Render2DEvent;
struct TickEvent;

namespace modules {

// ── Render ───────────────────────────────────────────────────────────────────
class Watermark final : public Module {
public:
    Watermark();

private:
    void onRender(Render2DEvent& event);

    EnumSetting* m_style;
    BoolSetting* m_showFps;
    ColourSetting* m_colour;
};

class ArrayList final : public Module {
public:
    ArrayList();

private:
    void onRender(Render2DEvent& event);

    EnumSetting* m_sort;
    BoolSetting* m_rainbow;
    BoolSetting* m_background;
};

// Toast popups with a soft two-note chime. Other code posts through the static
// push(), so anything in the client can raise a notification:
//
//     modules::Notifications::push("Config saved", Notifications::Level::Success);
class Notifications final : public Module {
public:
    enum class Level { Info, Success, Warning, Error };

    Notifications();

    static void push(std::string text, Level level = Level::Info);

private:
    struct Toast {
        std::string text;
        Level level = Level::Info;
        float born = 0.0f;
        gui::Animated slide{0.0f};
        bool expiring = false;
    };

    void onRender(Render2DEvent& event);
    void onModuleToggle(ModuleToggleEvent& event);

    void add(std::string text, Level level);
    void playChime(bool rising);
    Colour colourFor(Level level) const;

    std::deque<Toast> m_toasts;

    BoolSetting* m_sound;
    FloatSetting* m_volume;
    FloatSetting* m_duration;
    IntSetting* m_maxVisible;
    EnumSetting* m_corner;
    BoolSetting* m_moduleToggles;

    std::vector<uint8_t> m_chimeUp;
    std::vector<uint8_t> m_chimeDown;
    float m_builtVolume = -1.0f;
};

// ── Misc ─────────────────────────────────────────────────────────────────────
class ClickGuiModule final : public Module {
public:
    ClickGuiModule();
    bool persistEnabled() const override { return false; }

protected:
    void onEnable() override;
    void onDisable() override;

private:
    void onRender(Render2DEvent& event);
    void onMouse(MouseEvent& event);
    void onKey(KeyEvent& event);
};

// Enabled means vanilla behaviour (vsync on). Turning it off presents without
// waiting for the refresh, which uncaps the frame rate.
class VSync final : public Module {
public:
    VSync();
    std::string suffix() const override;

protected:
    void onEnable() override;
    void onDisable() override;

private:
    void onRender(Render2DEvent& event);
    void apply(bool enabled);

    bool m_warned = false;
};

class PacketLogger final : public Module {
public:
    PacketLogger();

private:
    void onPacket(PacketSendEvent& event);

    BoolSetting* m_toChat;
};

} // namespace modules
} // namespace aerial
