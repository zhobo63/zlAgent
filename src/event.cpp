#include "pch.h"

#include "event.h"
#include "logger.h"

namespace agent {

// ── EventBroker ────────────────────────────────────────────

EventToken EventBroker::register_callback(const std::string& key, EventCallback cb) {
    std::lock_guard<std::mutex> lock(mtx_);
    EventToken token = next_token_.fetch_add(1);
    slots_[key].push_back({token, std::move(cb)});
    return token;
}

void EventBroker::unregister(EventToken token) {
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& [k, list] : slots_) {
        auto it = std::find_if(list.begin(), list.end(),
                              [token](const Entry& e) { return e.token == token; });
        if (it != list.end()) {
            list.erase(it);
            break;
        }
    }
}

void EventBroker::emit(const std::string& key, const std::string& msg) {
    SlotList local_copy;

    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = slots_.find(key);
        if (it == slots_.end()) return;
        // Snapshot the list so callbacks can safely modify registrations.
        local_copy = it->second;
    }

    for (const auto& entry : local_copy) {
        try {
            entry.cb(msg);
        } catch (const std::exception& e) {
            LOG_ERROR("EventBroker", "Callback threw: " + std::string(e.what()));
        } catch (...) {
            LOG_ERROR("EventBroker", "Callback threw unknown exception");
        }
    }
}

// ── Global singleton ───────────────────────────────────────

EventBroker& event_broker() {
    static EventBroker instance;
    return instance;
}

} // namespace agent
