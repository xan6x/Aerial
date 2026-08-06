#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace aerial {

// Base for every event. `cancel()` is only meaningful for events the hook layer
// documents as cancellable — see Events.h.
struct Event {
    bool cancelled = false;

    void cancel() { cancelled = true; }
    bool isCancelled() const { return cancelled; }
};

namespace detail {
// Cheap monotonic type ids — faster than typeid and avoids RTTI in hot paths.
inline uint32_t nextEventTypeId() {
    static uint32_t counter = 0;
    return counter++;
}

template <typename E>
inline uint32_t eventTypeId() {
    static const uint32_t id = nextEventTypeId();
    return id;
}
} // namespace detail

// Priority ordering: handlers run from highest to lowest. Modules that want the
// last word on a cancellable event (e.g. a packet filter) subscribe with a high
// priority; passive observers use the default.
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

    // Removes every handler registered with this owner, across all event types.
    void unsubscribe(void* owner);

    template <typename E>
    E& dispatch(E& event) {
        static_assert(std::is_base_of_v<Event, E>, "events must derive from Event");
        dispatchRaw(detail::eventTypeId<E>(), event);
        return event;
    }

    // Convenience for temporaries: bus.post<TickEvent>(player).
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

    // Handlers may subscribe/unsubscribe while an event is being dispatched
    // (a module toggling another module from a key handler, for example), so
    // mutations are queued until the outermost dispatch finishes.
    int m_dispatchDepth = 0;
    std::vector<std::pair<uint32_t, Entry>> m_pendingAdds;
    std::vector<PendingRemoval> m_pendingRemovals;
};

} // namespace aerial
