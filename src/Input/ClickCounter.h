#pragma once

#include <chrono>
#include <deque>

namespace aerial::input {

class ClickCounter {
public:
    void update(bool down) {
        const auto now = std::chrono::steady_clock::now();
        if (down && !m_was)
            m_times.push_back(now);
        m_was = down;

        while (!m_times.empty() &&
               std::chrono::duration_cast<std::chrono::milliseconds>(now - m_times.front()).count() >
                   1000)
            m_times.pop_front();
    }

    int cps() const { return static_cast<int>(m_times.size()); }

private:
    std::deque<std::chrono::steady_clock::time_point> m_times;
    bool m_was = false;
};

}
