#pragma once

#include <string>

#include "Utils/Math.h"

namespace aerial::render {

// One drawing API, two backends.
//
//   * Direct2D, drawn onto the swap chain in Present. Rounded corners,
//     antialiasing, gradients, drop shadows, frosted-glass blur and real
//     DirectWrite typography.
//   * The game's own ScreenRenderer, used only when Direct2D cannot attach.
//     It can fill sharp untextured quads and blit a fixed-size bitmap font, so
//     radius, blur and font size are silently ignored there. Everything still
//     draws - it just looks like a 2017 client.
//
// Coordinates are in design units: the layout is authored as if the screen were
// 1000 units tall, and uiScale() converts. That keeps one set of layout
// constants working across resolutions and across both backends.
class DrawUtils {
public:
    static bool usingD2D();

    // Called once per frame before anything is drawn.
    static void beginFrame();

    // Drawable area, in the backend's own units.
    static Vec2 screenSize();

    // Multiply layout constants by this. 1.0 means "authored size".
    static float uiScale();

    // --- Shapes -------------------------------------------------------------
    static void fill(const Rect& area, const Colour& colour, float radius = 0.0f);
    static void outline(const Rect& area, const Colour& colour, float thickness = 1.0f,
                        float radius = 0.0f);
    static void gradient(const Rect& area, const Colour& from, const Colour& to, bool vertical = true,
                         float radius = 0.0f);

    // Soft drop shadow behind `area`. No-op on the fallback backend, which
    // draws a flat dark rectangle instead.
    static void shadow(const Rect& area, const Colour& colour, float blur, float radius,
                       const Vec2& offset = {0.0f, 2.0f});

    // Frosted glass: blurs whatever the game already drew behind `area`.
    static void blurBehind(const Rect& area, float radius, float strength);

    // --- Text ---------------------------------------------------------------
    enum class Weight { Regular, Medium, SemiBold, Bold };
    enum class Align { Left, Centre, Right };

    static void text(const std::string& value, const Vec2& position, const Colour& colour,
                     float size = 14.0f, Weight weight = Weight::Regular, Align align = Align::Left);

    static float textWidth(const std::string& value, float size = 14.0f,
                           Weight weight = Weight::Regular);
    static float textHeight(float size = 14.0f);

    // Shortens `value` with a trailing ellipsis until it fits `maxWidth`.
    // Clipping alone is not enough on the fallback backend, which has no
    // scissor, and a hard cut mid-word reads as a bug rather than as elision.
    static std::string fit(const std::string& value, float maxWidth, float size = 14.0f,
                           Weight weight = Weight::Regular);

    // --- Images -------------------------------------------------------------
    // Draws an embedded PNG (see src/Assets/Resources.h) into `dest`. No-op on
    // the fallback backend, which cannot sample textures.
    static void image(int resourceId, const Rect& dest, float opacity = 1.0f);

    // Aspect ratio (width / height), 0 if the image is unavailable.
    static float imageAspect(int resourceId);

    // Filled polygon. Only the Direct2D backend can draw one; the fallback
    // approximates it with the bounding box.
    static void polygon(const Vec2* points, size_t count, const Colour& colour);

    // --- Clipping -----------------------------------------------------------
    // A non-zero radius clips to a rounded rectangle, which is what keeps
    // artwork from poking out of the window's corners.
    static void pushClip(const Rect& area, float radius = 0.0f);
    static void popClip();

    // Drops the cached brush and text formats. They hold references to the
    // overlay's device context, so leaving them behind on unload keeps a whole
    // D3D11 device alive. Call before D2DOverlay::shutdown().
    static void releaseResources();

    // --- Diagnostics --------------------------------------------------------
    struct Stats {
        uint32_t fills = 0;
        uint32_t fillsSkipped = 0;
        uint32_t texts = 0;
        uint32_t textsSkipped = 0;
    };
    static Stats stats();
    static const char* backendName();

    // Overrides the GUI scale used by the fallback backend; 0 restores
    // auto-detection.
    static void setScaleOverride(float scale);
    static float scale();
};

} // namespace aerial::render
