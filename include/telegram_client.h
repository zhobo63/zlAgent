#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include "json.hpp"

namespace agent {

/**
 * Telegram Bot client — long-polling getUpdates + sendMessage.
 *
 * Message flow:
 *   Telegram → getUpdates → on_event("telegram.incoming", json_payload)
 *   send_event("telegram.outgoing", json{chat_id, text}) → sendMessage → Telegram
 */
class TelegramClient {
public:
    // ── Configuration ───────────────────────────────────────
    struct Config {
        bool enabled = false;
        std::string bot_token;          // e.g. "123456:ABCdef..."
        int poll_timeout_sec = 30;      // long-poll timeout per request
        int max_updates_per_poll = 100; // limit per getUpdates call
        std::vector<int64_t> allowed_chat_ids; // empty = allow all
    };

    explicit TelegramClient(const Config& cfg);
    ~TelegramClient();

    // Start the background polling thread. Call once after construction.
    void start();

    // Stop the polling loop and join the thread.
    void stop();

    // Send a text message to a chat. Thread-safe.
    bool send_message(int64_t chat_id, const std::string& text);

    // Check if the client is currently running.
    bool is_running() const { return running_.load(); }

private:
    Config cfg_;
    std::thread poll_thread_;
    std::atomic<bool> running_{false};

    int64_t last_update_id_ = 0;   // monotonic, protected by mutex_
    std::mutex mutex_;

    // ── Internal helpers ────────────────────────────────────
    void polling_loop();

    // Parse a single update JSON and emit via event broker.
    void handle_update(const nlohmann::json& update);

    // Build the API path prefix: "/bot<token>/"
    std::string api_path() const;
};

} // namespace agent
