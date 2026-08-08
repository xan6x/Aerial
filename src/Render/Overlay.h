#pragma once

#include <atomic>
#include <cstdint>
#include <functional>

#include "Utils/Math.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11Texture2D;
struct IDXGISwapChain;

namespace aerial::render {

class Overlay {
public:
    static Overlay& get();

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

    ID3D11Device* device() const { return m_device; }
    ID3D11DeviceContext* context() const { return m_context; }

    uint64_t generation() const { return m_generation; }

    const char* status() const { return m_status; }

    uint64_t presentCount() const;

private:
    Overlay() = default;

    friend struct OverlayHooks;

    ID3D11RenderTargetView* currentTarget(IDXGISwapChain* swapChain);
    void releaseTarget();
    void onPresent(IDXGISwapChain* swapChain);
    void onResize();

    std::function<void()> m_frameCallback;

    Vec2 m_size;

    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    ID3D11RenderTargetView* m_target = nullptr;
    ID3D11Texture2D* m_targetTexture = nullptr;

    IDXGISwapChain* m_chain = nullptr;
    uint64_t m_generation = 0;
    bool m_ready = false;
    bool m_enabled = true;
    bool m_installed = false;

    std::atomic<bool> m_abandoned{false};

    const char* m_status = "not installed";
};

}
