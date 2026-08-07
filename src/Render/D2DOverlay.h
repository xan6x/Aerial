#pragma once

#include <functional>

#include "Utils/Math.h"

struct ID2D1DeviceContext;
struct ID2D1Factory1;
struct ID3D11RenderTargetView;
struct IDWriteFactory;
struct IDXGISwapChain;

namespace aerial::render {

// Direct2D overlay drawn straight onto the game's swap chain.
//
// The game's own 2D renderer can only fill axis-aligned untextured quads and
// blit a bitmap font, which is a hard ceiling on how the menu can look - no
// rounded corners, no antialiasing, no real typography. Direct2D removes that
// ceiling, at the cost of having to reach the swap chain first.
//
// Capture works the way every overlay does it: DXGI's vtable is shared by every
// swap chain in the process, so creating a throwaway one is enough to learn the
// address of Present and ResizeBuffers. A composition swap chain is used for
// that because it needs no window, which matters inside the UWP AppContainer.
// Detouring those addresses then intercepts the game's real swap chain, which
// was created long before injection.
class D2DOverlay {
public:
    static D2DOverlay& get();

    // Hooks Present/ResizeBuffers. Returns false if DXGI could not be reached at
    // all; the client then stays on the game's renderer.
    bool install();
    void shutdown();

    // Called inside Present, with the render target bound and ready.
    void setFrameCallback(std::function<void()> callback);

    // True once a device context and target bitmap exist and the overlay is
    // switched on.
    bool ready() const { return m_ready && m_enabled; }

    // Runtime switch between the Direct2D overlay and the game's renderer.
    // While off, Present is left completely alone - nothing is drawn and no
    // pipeline state is touched - which makes it a one-click test for whether
    // the overlay is behind a rendering problem.
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool enabled() const { return m_enabled; }

    // Back buffer size in pixels.
    Vec2 size() const { return m_size; }

    ID2D1DeviceContext* context() const { return m_context; }
    IDWriteFactory* dwrite() const { return m_dwrite; }

    // Why the overlay is unavailable, for the log and the watchdog line.
    const char* status() const { return m_status; }

private:
    D2DOverlay() = default;

    friend struct D2DHooks;

    bool createTarget(IDXGISwapChain* swapChain);
    void releaseTarget();
    void onPresent(IDXGISwapChain* swapChain);
    void onResize();

    // Blends the overlay texture over the game's back buffer using the game's
    // own device, restoring its pipeline state afterwards.
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
    const char* m_status = "not installed";
};

} // namespace aerial::render
