#include "GUI/HudDrag.h"

#include <Windows.h>

#include <algorithm>
#include <string>

#include "Input/InputManager.h"
#include "Module/Setting.h"
#include "SDK/Context.h"

namespace aerial::hud {
namespace {

bool cursorFree() {
    if (!input::InputManager::gameFocused())
        return false;
    const std::string screen = sdk::Context::get().currentScreenName();
    return !screen.empty() && screen != "hud_screen" && screen != "start_screen";
}

}

Vec2 Draggable::place(Vec2 size, Vec2 screen) {
    if (!m_x || !m_y || screen.x <= 0.0f || screen.y <= 0.0f)
        return {0.0f, 0.0f};

    if (m_x->value < 0.0f || m_y->value < 0.0f) {
        const float margin = 20.0f;
        m_x->value = std::clamp((screen.x - size.x - margin) / screen.x, 0.0f, 1.0f);
        m_y->value = std::clamp(margin / screen.y, 0.0f, 1.0f);
    }

    Vec2 pos{m_x->value * screen.x, m_y->value * screen.y};

    if (input::InputManager::get().isDown(VK_RBUTTON) && cursorFree()) {
        const Vec2 cur = input::InputManager::get().cursor();
        const bool inside = cur.x >= pos.x && cur.x <= pos.x + size.x && cur.y >= pos.y &&
                            cur.y <= pos.y + size.y;
        if (!m_dragging && inside) {
            m_dragging = true;
            m_grab = {cur.x - pos.x, cur.y - pos.y};
        }
        if (m_dragging)
            pos = {cur.x - m_grab.x, cur.y - m_grab.y};
    } else {
        m_dragging = false;
    }

    pos.x = std::clamp(pos.x, 0.0f, std::max(0.0f, screen.x - size.x));
    pos.y = std::clamp(pos.y, 0.0f, std::max(0.0f, screen.y - size.y));

    m_x->value = pos.x / screen.x;
    m_y->value = pos.y / screen.y;
    return pos;
}

}
