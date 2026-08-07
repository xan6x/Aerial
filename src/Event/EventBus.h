#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace aerial {

struct Event {
    bool cancelled = false;

    void cancel() { cancelled = true; }
    bool isCancelled() const { return cancelled; }
};

namespace detail {

inline uint32_t nextEventTypeId() {
    static uint32_t counter = 0;
    return counter++;
}

template <typename E>
inline uint32_t eventTypeId() {
    static const uint32_t id = nextEventTypeId();
    return id;
}
}

enum EventPriority : int {
    kPriorityLowest = -200,
    kPriorityLow = -100,
    kPriorityNormal = 0,
    kPriorityHigh = 100,
    kPriorityHighest = 200,
};

class EventBus {
public:
    static EventBus& get();

    template <typename E>
    void subscribe(void* owner, std::function<void(E&)> handler, int priority = kPriorityNormal) {
        static_assert(std::is_base_of_v<Event, E>, "events must derive from Event");

        auto boxed = std::make_shared<std::function<void(E&)>>(std::move(handler));
        Entry entry;
        entry.owner = owner;
        entry.priority = priority;
        entry.storage = boxed;
        entry.invoke = [](const std::shared_ptr<void>& storage, Event& event) {
            (*std::static_pointer_cast<std::function<void(E&)>>(storage))(static_cast<E&>(event));
        };

        addEntry(detail::eventTypeId<E>(), std::move(entry));
    }

    void unsubscribe(void* owner);

    template <typename E>
    E& dispatch(E& event) {
        static_assert(std::is_base_of_v<Event, E>, "events must derive from Event");
        dispatchRaw(detail::eventTypeId<E>(), event);
        return event;
    }

    template <typename E, typename... Args>
    E post(Args&&... args) {
        E event{std::forward<Args>(args)...};
        dispatch(event);
        return event;
    }

private:
    struct Entry {
        void* owner = nullptr;
        int priority = kPriorityNormal;
        std::shared_ptr<void> storage;
        void (*invoke)(const std::shared_ptr<void>&, Event&) = nullptr;
    };

    struct PendingRemoval {
        void* owner;
    };

    EventBus() = default;

    void addEntry(uint32_t type, Entry entry);
    void dispatchRaw(uint32_t type, Event& event);
    void flushPending();

    std::unordered_map<uint32_t, std::vector<Entry>> m_handlers;

    int m_dispatchDepth = 0;
    std::vector<std::pair<uint32_t, Entry>> m_pendingAdds;
    std::vector<PendingRemoval> m_pendingRemovals;
};

}
