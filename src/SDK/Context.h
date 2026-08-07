#pragma once

#include "SDK/ClientInstance.h"
#include "SDK/Entity.h"
#include "SDK/Level.h"
#include "SDK/Types.h"

namespace aerial::sdk {

struct Context {
    static Context& get();

    ClientInstance* client = nullptr;
    LocalPlayer* localPlayer = nullptr;
    GameMode* gameMode = nullptr;
    Level* level = nullptr;
    ScreenContext* screenContext = nullptr;

    bool inGame() const { return localPlayer != nullptr && level != nullptr; }

    Font* font() const { return client ? client->font() : nullptr; }

    void chat(const std::string& message) const;

    void reset();
};

}
