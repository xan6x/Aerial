#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

namespace aerial::security {

class Scanner {
public:
    static Scanner& get();

    void init(void* selfModule);
    void scan();

    uint64_t heartbeat() const { return m_heartbeat.load(std::memory_order_relaxed); }

private:
    Scanner() = default;

    uintptr_t m_selfBase = 0;
    uintptr_t m_selfEnd = 0;
    uintptr_t m_gameBase = 0;
    uintptr_t m_gameEnd = 0;

    uintptr_t m_textBase = 0;
    size_t m_textSize = 0;
    uint64_t m_textHash = 0;
    bool m_textReported = false;

    bool m_initialised = false;
    bool m_loaderLinked = false;
    int m_escalateTicks = 0;

    std::mutex m_scanMutex;
    std::atomic<uint64_t> m_heartbeat{0};
};

}
