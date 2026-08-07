#pragma once

#include <atomic>

struct IDXGISwapChain;

namespace aerial::render {

class MotionBlur {
public:
    static MotionBlur& get();

    void setEnabled(bool enabled);
    void setAmount(float amount) { m_amount.store(amount, std::memory_order_relaxed); }
    void setOpacity(float opacity) { m_opacity.store(opacity, std::memory_order_relaxed); }

    void onPresent(IDXGISwapChain* swapChain);
    void onResize();
    void shutdown();

    bool enabled() const { return m_enabled.load(std::memory_order_relaxed); }
    bool failed() const { return m_failed.load(std::memory_order_relaxed); }

    const char* status() const { return m_status.load(std::memory_order_relaxed); }

private:
    MotionBlur() = default;

    bool ensureResources(IDXGISwapChain* swapChain);
    bool ensureBackBuffer(IDXGISwapChain* swapChain);
    void releaseResources();
    void fail(const char* why);

    float blendFactor();

    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_failed{false};
    std::atomic<float> m_amount{0.40f};
    std::atomic<float> m_opacity{1.0f};
    std::atomic<const char*> m_status{"idle"};

    bool m_primed = false;
    long long m_lastTick = 0;
};

}
