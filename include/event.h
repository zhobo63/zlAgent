#pragma once

#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>

namespace agent {

// ── Event callback signature ───────────────────────────────
// Receives the message payload as a string.
using EventCallback = std::function<void(const std::string&)>;

// ── Unique token returned on registration ──────────────────
// Used to unregister a specific callback later.
using EventToken = size_t;

// ── EventBroker: register / emit / unregister ───────────────
class EventBroker {
public:
    // Register a callback for the given event key.
    // Returns an EventToken that can be passed to unregister().
    EventToken register_callback(const std::string& key, EventCallback cb);

    // Unregister a previously registered callback by its token.
    void unregister(EventToken token);

    // Broadcast *msg* to every callback registered under *key*.
    // Callbacks are invoked synchronously in registration order.
    // If a callback throws, the exception is caught and logged;
    // remaining callbacks still receive the message.
    void emit(const std::string& key, const std::string& msg);

private:
    struct Entry {
        EventToken token;
        EventCallback cb;
    };

    using SlotList = std::vector<Entry>;

    std::mutex mtx_;
    std::unordered_map<std::string, SlotList> slots_;
    std::atomic<EventToken> next_token_{0};
};

// ── Global singleton ───────────────────────────────────────
EventBroker& event_broker();

// ── Convenience free functions ─────────────────────────────
inline EventToken on_event(const std::string& key, EventCallback cb) {
    return event_broker().register_callback(key, cb);
}

inline void off_event(EventToken token) {
    event_broker().unregister(token);
}

inline void send_event(const std::string& key, const std::string& msg) {
    event_broker().emit(key, msg);
}

// ── RAII guard: auto-unregister on scope exit ──────────────
class EventGuard {
public:
    explicit EventGuard(EventToken token) : token_(token) {}
    ~EventGuard() { off_event(token_); }

    // Non-copyable, movable.
    EventGuard(const EventGuard&) = delete;
    EventGuard& operator=(const EventGuard&) = delete;
    EventGuard(EventGuard&& other) noexcept : token_(other.token_) {
        other.token_ = 0;
    }
    EventGuard& operator=(EventGuard&& other) noexcept {
        if (this != &other) {
            off_event(token_);
            token_ = other.token_;
            other.token_ = 0;
        }
        return *this;
    }

private:
    EventToken token_;
};

} // namespace agent
