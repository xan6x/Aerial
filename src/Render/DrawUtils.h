#pragma once

#include <string>

#include "Utils/Math.h"

namespace aerial::render {

class DrawUtils {
public:
    static bool usingD2D();

    static void beginFrame();

    static Vec2 screenSize();

    static float uiScale();

    static void fill(const Rect& area, const Colour& colour, float radius = 0.0f);
    static void outline(const Rect& area, const Colour& colour, float thickness = 1.0f,
                        float radius = 0.0f);
    static void gradient(const Rect& area, const Colour& from, const Colour& to, bool vertical = true,
                         float radius = 0.0f);

    static void shadow(const Rect& area, const Colour& colour, float blur, float radius,
                       const Vec2& offset = {0.0f, 2.0f});

    static void blurBehind(const Rect& area, float radius, float strength);

    enum class Weight { Regular, Medium, SemiBold, Bold };
    enum class Align { Left, Centre, Right };

    static void text(const std::string& value, const Vec2& position, const Colour& colour,
                     float size = 14.0f, Weight weight = Weight::Regular, Align align = Align::Left);

    static float textWidth(const std::string& value, float size = 14.0f,
                           Weight weight = Weight::Regular);
    static float textHeight(float size = 14.0f);

    static std::string fit(const std::string& value, float maxWidth, float size = 14.0f,
                           Weight weight = Weight::Regular);

    static void image(int resourceId, const Rect& dest, float opacity = 1.0f);

    static float imageAspect(int resourceId);

    static void polygon(const Vec2* points, size_t count, const Colour& colour);

    static void pushClip(const Rect& area, float radius = 0.0f);
    static void popClip();

    static void releaseResources();

    struct Stats {
        uint32_t fills = 0;
        uint32_t fillsSkipped = 0;
        uint32_t texts = 0;
        uint32_t textsSkipped = 0;
    };
    static Stats stats();
    static const char* backendName();

    static void setScaleOverride(float scale);
    static float scale();
};

}
