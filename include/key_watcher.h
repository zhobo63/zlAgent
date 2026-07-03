#pragma once

#include <functional>
#include <thread>
#include <atomic>
#include <string>
#include <vector>

namespace agent {

using InterruptCallback = std::function<void(int ch)>; // ch: 3=Ctrl-C, 27=ESC

/// Global key watcher that runs in the background and detects Ctrl-C.
class KeyWatcher {
public:
    // ── Original API ────────────────────────────────────────

    /// Register a callback to be invoked when Ctrl-C or ESC is detected.
    static void on_key(InterruptCallback cb);

    /// Start watching for Ctrl-C (char code 3). Thread-safe, idempotent.
    static void start();

    /// Stop the watcher thread. Thread-safe.
    static void stop();

    // ── New readline API ────────────────────────────────────

    /// Read a line of input with rich editing abilities.
    /// @param prompt  The prompt text displayed before the cursor.
    /// @param cb      Callback invoked for every key press (extra notification).
    ///                Can be used to decide whether to abort readline early.
    /// @returns       The input string on Enter; empty string on Ctrl+C.
    static std::string readline(const char* prompt, InterruptCallback cb);

    // ── Completion API ──────────────────────────────────────

    /// Add keywords to the global completion pool (shared across all readline calls).
    static void add_keywords(const std::vector<std::string>& keywords);

    // Completion state
    static std::vector<std::string> s_keywords;
private:
    static std::thread*       s_thread;
    static std::atomic<bool>  s_running;
    static InterruptCallback  s_callback;

};

} // namespace agent
