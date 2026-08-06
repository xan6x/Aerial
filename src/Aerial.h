#pragma once

#include <atomic>

namespace aerial {

// Client lifecycle. Startup runs on a dedicated thread created by DllMain so
// nothing blocks the loader lock; shutdown runs on the eject thread.
class Aerial {
public:
    static Aerial& get();

    void startup(void* moduleHandle);
    void shutdown();

    bool running() const { return m_running; }
    void requestShutdown() { m_shutdownRequested = true; }
    bool shutdownRequested() const { return m_shutdownRequested; }

    void* moduleHandle() const { return m_module; }

private:
    Aerial() = default;

    bool verifyGameBuild();

    void* m_module = nullptr;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_shutdownRequested{false};
};

} // namespace aerial
