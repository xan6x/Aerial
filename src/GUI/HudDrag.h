#pragma once

#include "Utils/Math.h"

namespace aerial {

class FloatSetting;

namespace hud {

class Draggable {
public:
    void bind(FloatSetting* x, FloatSetting* y) {
        m_x = x;
        m_y = y;
    }

    Vec2 place(Vec2 size, Vec2 screen);

    bool dragging() const { return m_dragging; }

private:
    FloatSetting* m_x = nullptr;
    FloatSetting* m_y = nullptr;
    bool m_dragging = false;
    Vec2 m_grab{};
};

}
}
