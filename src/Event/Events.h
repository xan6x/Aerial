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
} // namespace sdk

// ── Lifecycle ────────────────────────────────────────────────────────────────

// Fired once the client has a LocalPlayer and a Level — the point where it is
// safe for modules to touch the world.
struct WorldJoinEvent : Event {
    sdk::LocalPlayer* player = nullptr;
    sdk::Level* level = nullptr;
};

struct WorldLeaveEvent : Event {};

// ── Ticking ──────────────────────────────────────────────────────────────────

// Once per game tick from LocalPlayer::normalTick, before the original runs.
// The player pointer is always valid here.
struct TickEvent : Event {
    sdk::LocalPlayer* player = nullptr;
};

// GameMode::tick — the right place for click/aura logic, since attacks issued
// here land in the same tick the game processes input.
struct GameTickEvent : Event {
    sdk::GameMode* gameMode = nullptr;
    sdk::LocalPlayer* player = nullptr;
};

// ── Rendering ────────────────────────────────────────────────────────────────

// 2D overlay pass, after the game's HUD has drawn. Coordinates are in the
// game's scaled UI space (see DrawUtils::screenSize()).
struct Render2DEvent : Event {
    sdk::ScreenContext* context = nullptr;
    Vec2 screenSize;
};

// ── Combat / interaction ─────────────────────────────────────────────────────

// Cancellable: cancelling suppresses the attack entirely.
struct AttackEvent : Event {
    sdk::LocalPlayer* player = nullptr;
    sdk::Entity* target = nullptr;
};

// ── Movement ─────────────────────────────────────────────────────────────────

// Cancellable movement/rotation control. Modules mutate `input` in place;
// whatever survives the dispatch is what the game receives this tick.
struct MoveInputEvent : Event {
    sdk::MoveInput* input = nullptr;
    sdk::LocalPlayer* player = nullptr;
};

// ── Networking ───────────────────────────────────────────────────────────────

// Cancellable: cancelling drops the packet before it reaches the socket.
struct PacketSendEvent : Event {
    sdk::Packet* packet = nullptr;
    int packetId = 0;
};

// Chat line the local player is about to send; cancelling swallows it. Commands
// handled by the client's own command system cancel here.
struct ChatSendEvent : Event {
    std::string message;
};

// ── Client ───────────────────────────────────────────────────────────────────

// A module changed state. Fired after onEnable/onDisable have run, so handlers
// see the module in its new state.
struct ModuleToggleEvent : Event {
    class Module* module = nullptr;
    bool enabled = false;
};

// ── Input ────────────────────────────────────────────────────────────────────

struct KeyEvent : Event {
    int key = 0;        // virtual-key code
    bool down = false;
    bool repeat = false;
};

struct MouseEvent : Event {
    enum class Button { Left, Right, Middle, ScrollUp, ScrollDown };

    Button button = Button::Left;
    bool down = false;
    Vec2 position;

    // Wheel notches since the last poll, positive upwards. Only meaningful for
    // ScrollUp/ScrollDown; kept as a float because precision wheels report
    // fractions of a detent and a list should follow them smoothly.
    float wheel = 0.0f;
};

} // namespace aerial
