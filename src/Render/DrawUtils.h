#pragma once

#include <string>

#include "SDK/Types.h"
#include "Utils/Math.h"

namespace aerial::render {

// 2D drawing in the game's scaled UI space, on top of MC's own renderer:
//   * rectangles go through ScreenRenderer::fill (verified signature)
//   * text goes through Font::drawCached, the same call the HUD uses
//
// Only valid from inside a Render2DEvent handler — the tessellator state the
// game sets up for the HUD pass is what makes these calls safe.
class DrawUtils {
public:
    // Called by the render hook before dispatching Render2DEvent.
    static void beginFrame();

    // Size of the UI viewport in GUI units (not pixels).
    static Vec2 screenSize();

    // The GUI scale factor Aerial derives the viewport from. Override it if the
    // auto-detected value is wrong for a given window/scale combination; 0
    // restores auto-detection.
    static void setScaleOverride(float scale);
    static float scale();

    // --- Shapes -------------------------------------------------------------
    static void fill(const Rect& area, const Colour& colour);
    static void outline(const Rect& area, const Colour& colour, float thickness = 1.0f);

    // Vertical/horizontal two-stop gradient, drawn as `steps` strips.
    static void gradientVertical(const Rect& area, const Colour& top, const Colour& bottom, int steps = 24);
    static void gradientHorizontal(const Rect& area, const Colour& left, const Colour& right, int steps = 24);

    // Rectangle with rounded corners, approximated with horizontal strips.
    static void roundedFill(const Rect& area, const Colour& colour, float radius, int segments = 6);

    // --- Text ---------------------------------------------------------------
    static void text(const std::string& value, const Vec2& position, const Colour& colour,
                     float scale = 1.0f, bool shadow = true);
    static void textCentred(const std::string& value, const Vec2& centre, const Colour& colour,
                            float scale = 1.0f, bool shadow = true);
    static void textRight(const std::string& value, const Vec2& rightAnchor, const Colour& colour,
                          float scale = 1.0f, bool shadow = true);

    static float textWidth(const std::string& value, float scale = 1.0f);
    static float textHeight(float scale = 1.0f);

    // True when a font and renderer are available this frame.
    static bool ready();

    // --- Bring-up instrumentation -------------------------------------------
    // Drawing rides on the game's own 2D pass, and the client has no way to see
    // whether a call actually reached the screen. These probes draw a labelled
    // bar from each candidate point in the frame; whichever bar is visible in
    // game is the point Render2DEvent should be dispatched from.
    enum class ProbeSite { BeforeGameRender = 0, AfterGameRender = 1, AfterScreenRender = 2 };

    static void probe(ProbeSite site);
    static void setProbeEnabled(bool enabled);
    static bool probeEnabled();

    struct Stats {
        uint32_t fills = 0;
        uint32_t fillsSkipped = 0;   // ScreenRenderer::singleton() returned null
        uint32_t texts = 0;
        uint32_t textsSkipped = 0;   // no Font available
    };
    static Stats stats();
};

} // namespace aerial::render
