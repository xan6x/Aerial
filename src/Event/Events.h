#pragma once

#include <string>

#include "Event/EventBus.h"
#include "Utils/Math.h"

namespace aerial {

namespace sdk {
class ClientInstance;
class Entity;
class GameMode;
class Level;
class LocalPlayer;
class Packet;
class ScreenContext;
struct MoveInput;
}

struct WorldJoinEvent : Event {
    sdk::LocalPlayer* player = nullptr;
    sdk::Level* level = nullptr;
};

struct WorldLeaveEvent : Event {};

struct TickEvent : Event {
    sdk::LocalPlayer* player = nullptr;
};

struct GameTickEvent : Event {
    sdk::GameMode* gameMode = nullptr;
    sdk::LocalPlayer* player = nullptr;
};

struct Render2DEvent : Event {
    sdk::ScreenContext* context = nullptr;
    Vec2 screenSize;
};

struct AttackEvent : Event {
    sdk::LocalPlayer* player = nullptr;
    sdk::Entity* target = nullptr;
};

struct MoveInputEvent : Event {
    sdk::MoveInput* input = nullptr;
    sdk::LocalPlayer* player = nullptr;
};

struct PacketSendEvent : Event {
    sdk::Packet* packet = nullptr;
    int packetId = 0;
};

struct ChatSendEvent : Event {
    std::string message;
};

struct ModuleToggleEvent : Event {
    class Module* module = nullptr;
    bool enabled = false;
};

struct KeyEvent : Event {
    int key = 0;
    bool down = false;
    bool repeat = false;
};

struct MouseEvent : Event {
    enum class Button { Left, Right, Middle, ScrollUp, ScrollDown };

    Button button = Button::Left;
    bool down = false;
    Vec2 position;

    float wheel = 0.0f;
};

}
