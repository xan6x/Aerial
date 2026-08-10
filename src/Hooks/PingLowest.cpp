#include "Hooks/PingLowest.h"

#include <cstdint>

#include "SDK/Offsets.h"
#include "Utils/Logger.h"
#include "Utils/Memory.h"

namespace aerial::hooks {
namespace {

constexpr uint8_t kLastPingDisp = 0x50;
constexpr uint8_t kLowestPingDisp = 0x58;

bool g_applied = false;

}

void applyLowestPing() {
    if (g_applied)
        return;

    const uintptr_t addr = memory::rva(offsets::func::RakNetNetworkPeer_pingDispByte);
    if (!memory::isReadable(reinterpret_cast<void*>(addr), 1))
        return;

    if (*reinterpret_cast<uint8_t*>(addr) != kLastPingDisp) {
        LOG_WARN("PingLowest", "unexpected disp byte, skipping");
        return;
    }

    if (memory::patch(addr, &kLowestPingDisp, 1)) {
        g_applied = true;
        LOG_INFO("PingLowest", "ping now reports GetLowestPing");
    }
}

void revertLowestPing() {
    if (!g_applied)
        return;

    const uintptr_t addr = memory::rva(offsets::func::RakNetNetworkPeer_pingDispByte);
    if (memory::patch(addr, &kLastPingDisp, 1))
        g_applied = false;
}

}
