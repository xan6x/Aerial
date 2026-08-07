#pragma once

#include <array>
#include <atomic>
#include <mutex>

#include "Utils/Math.h"

namespace aerial::input {

class InputManager {
public:
    static InputManager& get();

    void sample();

    void poll();

    bool isDown(int virtualKey) const;
    bool wasPressed(int virtualKey) const;

    Vec2 cursor() const { return m_cursor; }

    void setCapture(bool capture) { m_captured = capture; }
    bool captured() const { return m_captured; }

    void installMessageHook();
    void removeMessageHook();
    bool messageHooked() const;

    void setSwallowInput(bool swallow);

    void feedWheel(int notches);

    static bool gameFocused();
    static const char* keyName(int virtualKey);

    static char characterFor(int virtualKey);

    struct Stats {
        uint64_t samples = 0;
        uint64_t polls = 0;
        uint64_t transitions = 0;
        int lastKey = 0;
        int asyncDowns = 0;
        int syncDowns = 0;

        uint64_t hookCalls = 0;
        uint64_t wheelEvents = 0;
        uint32_t lastPointerMessage = 0;
    };
    Stats stats() const;

private:
    InputManager() = default;

    static constexpr int kKeyCount = 256;
    using KeyState = std::array<bool, kKeyCount>;

    mutable std::mutex m_mutex;
    KeyState m_sampled{};

    KeyState m_down{};
    KeyState m_previous{};
    Vec2 m_cursor;
    bool m_captured = false;

    std::atomic<uint64_t> m_samples{0};
    std::atomic<uint64_t> m_polls{0};
    std::atomic<uint64_t> m_transitions{0};
    std::atomic<int> m_lastKey{0};
    std::atomic<int> m_asyncDowns{0};
    std::atomic<int> m_syncDowns{0};
};

}
