#include "SDK/Context.h"

#include "Utils/Logger.h"
#include "Utils/Memory.h"

namespace aerial::sdk {

Context& Context::get() {
    static Context instance;
    return instance;
}

std::string Context::currentScreenName() const {
    if (!client || !memory::isReadable(client, 0x40))
        return {};
    auto* game = client->game();
    if (!game || !memory::isReadable(game, 0xC0))
        return {};
    return game->getScreenName();
}

void Context::chat(const std::string& message) const {
    if (localPlayer && memory::isReadable(localPlayer))
        localPlayer->displayClientMessage(message);
    else
        LOG_DEBUG("Context", "chat dropped (no player): {}", message);
}

void Context::reset() {
    localPlayer = nullptr;
    gameMode = nullptr;
    level = nullptr;
    screenContext = nullptr;

}

}
