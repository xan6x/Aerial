#pragma once

#include "SDK/ClientInstance.h"
#include "SDK/Entity.h"
#include "SDK/Level.h"
#include "SDK/Types.h"

namespace aerial::sdk {

// Live pointers into the game, captured from hooks rather than resolved through
// globals. Every pointer here is refreshed by the hook that owns it and cleared
// when the corresponding object goes away, so module code can rely on a simple
// null check:
//
//     auto* player = Context::get().localPlayer;
//     if (!player) return;
//
// Only ever read these from the game's own threads (tick and render hooks).
struct Context {
    static Context& get();

    ClientInstance* client = nullptr;
    LocalPlayer* localPlayer = nullptr;
    GameMode* gameMode = nullptr;
    Level* level = nullptr;
    ScreenContext* screenContext = nullptr;

    // True once a tick has run with a player and a level present.
    bool inGame() const { return localPlayer != nullptr && level != nullptr; }

    Font* font() const { return client ? client->font() : nullptr; }

    // Prints a line into the local chat. No-op outside a world.
    void chat(const std::string& message) const;

    // Drops every captured pointer; called when leaving a world.
    void reset();
};

} // namespace aerial::sdk
