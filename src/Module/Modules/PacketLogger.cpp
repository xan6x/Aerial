#include "Module/Modules/PacketLogger.h"

#include <format>

#include "Event/Events.h"
#include "SDK/Context.h"
#include "Utils/Logger.h"

namespace aerial::modules {

PacketLogger::PacketLogger()
    : Module("PacketLogger", "Logs outgoing packet ids - useful while reversing", Category::Misc) {
    m_toChat = addBool("Chat", "Also print into the in-game chat", false);
    listen<PacketSendEvent>(&PacketLogger::onPacket);
}

void PacketLogger::onPacket(PacketSendEvent& event) {
    const std::string line = std::format("out packet id {} ({:#x})", event.packetId, event.packetId);
    LOG_DEBUG("Packet", "{}", line);

    if (m_toChat->value)
        sdk::Context::get().chat("\xC2\xA7" "7[Aerial] " + line);
}

} // namespace aerial::modules
