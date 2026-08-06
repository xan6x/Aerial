#pragma once

#include <array>
#include <atomic>
#include <mutex>

#include "Utils/Math.h"

namespace aerial::input {

// Keyboard/mouse state for a UWP host, split across two threads on purpose.
//
// sample() reads the raw key state on the client's own thread - the same thread
// that has always been able to see the End key, so it is the one known to work
// here. poll() then runs on the render thread, diffs the snapshot and dispatches
// events, because module handlers must not run concurrently with rendering.
class InputManager {
public:
    static InputManager& get();

    // Client thread, ~60 Hz: snapshot the raw key state.
    void sample();

    // Render thread, once per frame: diff the snapshot and emit events.
    void poll();

    bool isDown(int virtualKey) const;
    bool wasPressed(int virtualKey) const;

    // Cursor position in GUI units, matching DrawUtils' coordinate space.
    Vec2 cursor() const { return m_cursor; }

    // Suppresses keybinds while the GUI is reading a key to assign. The open
    // GUI itself does NOT capture: its own bind has to keep working so the same
    // key closes it again.
    void setCapture(bool capture) { m_captured = capture; }
    bool captured() const { return m_captured; }

    static bool gameFocused();
    static const char* keyName(int virtualKey);

    // Diagnostics surfaced by the watchdog line.
    struct Stats {
        uint64_t samples = 0;
        uint64_t polls = 0;
        uint64_t transitions = 0;
        int lastKey = 0;
        int asyncDowns = 0;   // keys GetAsyncKeyState reports held
        int syncDowns = 0;    // keys GetKeyboardState reports held
    };
    Stats stats() const;

private:
    InputManager() = default;

    static constexpr int kKeyCount = 256;
    using KeyState = std::array<bool, kKeyCount>;

    mutable std::mutex m_mutex;
    KeyState m_sampled{};       // written by sample(), read by poll()

    KeyState m_down{};          // render-thread view
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

} // namespace aerial::input
