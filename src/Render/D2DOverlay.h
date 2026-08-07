#pragma once

#include <atomic>
#include <functional>

#include "Utils/Math.h"

struct ID2D1DeviceContext;
struct ID2D1Factory1;
struct ID3D11RenderTargetView;
struct IDWriteFactory;
struct IDXGISwapChain;

namespace aerial::render {

class D2DOverlay {
public:
    static D2DOverlay& get();

    bool install();
    void shutdown();

    void setFrameCallback(std::function<void()> callback);

    bool ready() const { return m_ready && m_enabled && !m_abandoned; }

    void setEnabled(bool enabled) {
        if (enabled)
            m_abandoned = false;
        m_enabled = enabled;
    }
    bool enabled() const { return m_enabled; }

    void abandon(const char* why);
    bool abandoned() const { return m_abandoned; }

    Vec2 size() const { return m_size; }

    ID2D1DeviceContext* context() const { return m_context; }
    IDWriteFactory* dwrite() const { return m_dwrite; }

    const char* status() const { return m_status; }

private:
    D2DOverlay() = default;

    friend struct D2DHooks;

    bool createTarget(IDXGISwapChain* swapChain);
    void releaseTarget();
    void onPresent(IDXGISwapChain* swapChain);
    void onResize();

    void composite(IDXGISwapChain* swapChain);
    ID3D11RenderTargetView* currentTarget(IDXGISwapChain* swapChain);

    ID2D1Factory1* m_factory = nullptr;
    ID2D1DeviceContext* m_context = nullptr;
    IDWriteFactory* m_dwrite = nullptr;
    void* m_targetBitmap = nullptr;

    std::function<void()> m_frameCallback;

    Vec2 m_size;
    bool m_ready = false;
    bool m_enabled = true;
    bool m_installed = false;
    bool m_failed = false;

    std::atomic<bool> m_abandoned{false};

    const char* m_status = "not installed";
};

}
