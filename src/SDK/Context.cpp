#include "SDK/Context.h"

#include "Utils/Logger.h"
#include "Utils/Memory.h"

namespace aerial::sdk {

Context& Context::get() {
    static Context instance;
    return instance;
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
    // `client` outlives worlds — it is only cleared when the game shuts down.
}

} // namespace aerial::sdk
