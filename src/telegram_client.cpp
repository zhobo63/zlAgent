#include "pch.h"

#include "telegram_client.h"
#include "httplib.h"
#include "logger.h"
#include "event.h"

namespace agent {

using json = nlohmann::json;

// ── Construction / destruction ─────────────────────────────

TelegramClient::TelegramClient(const Config& cfg) : cfg_(cfg) {}

TelegramClient::~TelegramClient() {
    stop();
}

// ── Lifecycle ───────────────────────────────────────────────

void TelegramClient::start() {
    if (running_.load()) return;

    if (cfg_.bot_token.empty()) {
        LOG_ERROR("Telegram", "Bot token is empty — not starting poller.");
        return;
    }

#ifdef CPPHTTLIB_OPENSSL_SUPPORT
    httplib::SSLClient client("api.telegram.org");
#else
    LOG_ERROR("Telegram", "HTTPS not supported — Telegram requires SSL.");
    return;
#endif

    client.set_read_timeout(10, 0);
    client.set_write_timeout(10, 0);

    LOG_DEBUG("TelegramClient", api_path() + "/getMe");
    auto res = client.Get(api_path() + "/getMe");
    if (!res || res->status != 200) {
        LOG_ERROR("Telegram", "getMe failed (HTTP " + std::to_string(res ? res->status : 0) + ") — token may be invalid.");
        return;
    }

    try {
        LOG_DEBUG("TelegramClient", "getMe" + res->body);
        auto j = json::parse(res->body);
        if (!j.value("ok", false)) {
            LOG_ERROR("Telegram", "getMe returned ok=false: " + res->body.substr(0, 256));
            return;
        }
        std::string name = j["result"].value("first_name", "Unknown");
        LOG_INFO("Telegram", "Bot verified as @" + j["result"].value("username", "?") + " (" + name + ").");
    } catch (...) {
        LOG_WARN("Telegram", "getMe response not JSON, but HTTP 200 — proceeding.");
    }

    //send_message(8682024724, "zlAgent online");

    running_ = true;
    poll_thread_ = std::thread(&TelegramClient::polling_loop, this);
    LOG_INFO("Telegram", "Poll thread started (timeout=" + std::to_string(cfg_.poll_timeout_sec) + "s).");
}

void TelegramClient::stop() {
    running_ = false;
    if (poll_thread_.joinable()) {
        poll_thread_.join();
    }
    LOG_INFO("Telegram", "Poll thread stopped.");
}

// ── sendMessage ─────────────────────────────────────────────

bool TelegramClient::send_message(int64_t chat_id, const std::string& text) {
    if (text.empty()) return false;

    // Truncate to Telegram's 4096 character limit.
    std::string safe_text = text.substr(0, 4096);

    json body;
    body["chat_id"] = chat_id;
    body["text"]    = safe_text;

#ifdef CPPHTTLIB_OPENSSL_SUPPORT
    httplib::SSLClient client("api.telegram.org");
#else
    LOG_ERROR("Telegram", "HTTPS not supported — Telegram requires SSL.");
    return;
#endif

    client.set_read_timeout(15, 0);
    client.set_write_timeout(15, 0);

    LOG_DEBUG("TelegramClient", "POST" + api_path() + "/sendMessage");

    auto res = client.Post(api_path() + "/sendMessage", body.dump(), "application/json");

    if (!res) {
        LOG_ERROR("Telegram", "sendMessage failed: network error.");
        return false;
    }

    if (res->status != 200) {
        LOG_ERROR("Telegram", "sendMessage HTTP " + std::to_string(res->status) + ": " + res->body);
        return false;
    }

    try {
        auto j = json::parse(res->body);
        if (!j.value("ok", false)) {
            LOG_WARN("Telegram", "sendMessage ok=false: " + res->body.substr(0, 256));
            return false;
        }
    } catch (...) {
        // Non-JSON response but status 200 — treat as success.
    }

    return true;
}

// ── Polling loop ────────────────────────────────────────────

void TelegramClient::polling_loop() {
#ifdef CPPHTTLIB_OPENSSL_SUPPORT
    httplib::SSLClient client("api.telegram.org");
#else
    LOG_ERROR("Telegram", "HTTPS not supported — Telegram requires SSL.");
    return;
#endif

    static bool log_debug = false;

    client.set_read_timeout(60, 0);   // long-poll can block for up to poll_timeout_sec + margin
    client.set_write_timeout(15, 0);

    while (running_.load()) {
        std::string params = "?";
        if (last_update_id_ != 0) {
            params += "offset=" + std::to_string(last_update_id_ + 1) + "&";
        }
        params += "timeout=" + std::to_string(cfg_.poll_timeout_sec);
        params += "&limit="   + std::to_string(cfg_.max_updates_per_poll);

        auto res = client.Get(api_path() + "/getUpdates" + params);

        if (!res) {
            LOG_WARN("Telegram", "getUpdates failed: network error. Retrying in 5s...");
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        if (res->status != 200) {
            LOG_ERROR("Telegram", api_path() + "/getUpdates" + params + " " + std::to_string(res->status) + ": " + res->body.substr(0, 256));
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }
        if (!log_debug) {
            log_debug = true;
            LOG_DEBUG("TelegramClient", "getUpdates:" + res->body);
        }

        try {
            auto j = json::parse(res->body);
            if (!j.value("ok", false)) {
                LOG_WARN("Telegram", "getUpdates ok=false: " + res->body.substr(0, 256));
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            auto& updates = j["result"];
            for (const auto& update : updates) {
                handle_update(update);
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Telegram", "JSON parse error: " + std::string(e.what()));
        }

        // Brief pause to avoid tight loop when no new updates.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// ── Handle a single update ──────────────────────────────────

void TelegramClient::handle_update(const json& update) {
    int64_t update_id = update.value("update_id", 0);

    // Track the highest seen update_id.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (update_id > last_update_id_) {
            last_update_id_ = update_id;
        }
    }

    // Only forward text messages for now.
    if (!update.contains("message")) return;
    auto& msg = update["message"];
    if (!msg.contains("text") || !msg.value("text", "").size()) return;

	auto chat = msg.value("chat", json::object());
    int64_t chat_id   = chat.value("id", 0LL);

    // Filter by allowed_chat_ids if configured.
    if (!cfg_.allowed_chat_ids.empty()) {
        bool allowed = false;
        for (const auto& id : cfg_.allowed_chat_ids) {
            if (id == chat_id) { allowed = true; break; }
        }
        if (!allowed) return;
    }

    std::string text  = msg.value("text", "");
    auto from         = msg.value("from", json::object());
    std::string name  = from.value("first_name", "Unknown");

    LOG_INFO("Telegram", "[" + std::to_string(chat_id) + "] " + name + ": " + text);

    // Emit via event broker so other modules (e.g. Agent runner) can react.
    json payload;
    payload["chat_id"]   = chat_id;
    payload["text"]      = text;
    payload["from_name"] = name;
    payload["update_id"] = update_id;

    send_event("telegram.incoming", payload.dump());
}

// ── Helper ──────────────────────────────────────────────────

std::string TelegramClient::api_path() const {
    return "/bot" + cfg_.bot_token;
}

} // namespace agent
