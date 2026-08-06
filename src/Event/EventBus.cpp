#include "Event/EventBus.h"

#include <algorithm>

namespace aerial {

EventBus& EventBus::get() {
    static EventBus instance;
    return instance;
}

void EventBus::addEntry(uint32_t type, Entry entry) {
    if (m_dispatchDepth > 0) {
        m_pendingAdds.emplace_back(type, std::move(entry));
        return;
    }

    auto& list = m_handlers[type];
    const auto position = std::upper_bound(list.begin(), list.end(), entry.priority,
                                           [](int priority, const Entry& other) {
                                               return priority > other.priority;
                                           });
    list.insert(position, std::move(entry));
}

void EventBus::unsubscribe(void* owner) {
    if (m_dispatchDepth > 0) {
        m_pendingRemovals.push_back({owner});
        return;
    }

    for (auto& [type, list] : m_handlers) {
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [owner](const Entry& entry) { return entry.owner == owner; }),
                   list.end());
    }
}

void EventBus::dispatchRaw(uint32_t type, Event& event) {
    const auto it = m_handlers.find(type);
    if (it == m_handlers.end() || it->second.empty())
        return;

    ++m_dispatchDepth;

    // Index-based iteration: the vector cannot be resized during dispatch
    // because mutations are deferred, so this stays valid.
    const auto& list = it->second;
    for (size_t i = 0; i < list.size(); ++i) {
        const Entry& entry = list[i];
        if (entry.invoke)
            entry.invoke(entry.storage, event);
    }

    --m_dispatchDepth;
    if (m_dispatchDepth == 0)
        flushPending();
}

void EventBus::flushPending() {
    if (m_pendingRemovals.empty() && m_pendingAdds.empty())
        return;

    auto removals = std::move(m_pendingRemovals);
    m_pendingRemovals.clear();
    auto adds = std::move(m_pendingAdds);
    m_pendingAdds.clear();

    for (const auto& removal : removals)
        unsubscribe(removal.owner);
    for (auto& [type, entry] : adds)
        addEntry(type, std::move(entry));
}

} // namespace aerial
