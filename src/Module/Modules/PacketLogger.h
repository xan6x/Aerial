#pragma once

#include "Module/Module.h"

namespace aerial {
struct PacketSendEvent;
}

namespace aerial::modules {

class PacketLogger final : public Module {
public:
    PacketLogger();

private:
    void onPacket(PacketSendEvent& event);

    BoolSetting* m_toChat;
};

} // namespace aerial::modules
