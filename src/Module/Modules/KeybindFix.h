#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

#include "Module/Module.h"

namespace aerial {
struct TickEvent;
}

namespace aerial::modules {

class KeybindFix final : public Module {
public:
    KeybindFix();

    static void onMoveTick(void* handler);

private:
    void onEnable() override;
    void onDisable() override;
    void onTick(TickEvent& event);

    bool shouldRestoreFor(const std::string& screen) const;

    void captureState(void* handler);
    void writeState(void* handler) const;
    bool anyCapturedHeld() const;

    BoolSetting* m_fixInventory;
    BoolSetting* m_fixPause;
    BoolSetting* m_fixChat;
    BoolSetting* m_fixAll;

    bool m_wasGui = false;
    std::string m_lastGuiScreen;
    std::atomic<bool> m_active{false};
    std::atomic<bool> m_guiOpen{false};

    bool m_haveSnap = false;
    std::array<uint8_t, 0x18> m_snapA{};
    std::array<uint8_t, 0x14> m_snapB{};
    std::array<bool, 256> m_keys{};
    bool m_controller = false;
};

}
