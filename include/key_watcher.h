#pragma once

#include <atomic>

namespace agent {

/// Global key watcher that runs in the background and detects Ctrl-C.
class KeyWatcher {
public:
    /// Start watching for Ctrl-C (char code 3). Thread-safe, idempotent.
    static void start();

    /// Stop the watcher thread. Thread-safe.
    static void stop();

    /// Check if Ctrl-C was detected since last check. Returns true and resets flag.
    static bool was_interrupted();

private:
    static std::thread*       s_thread;
    static std::atomic<bool>  s_running;
    static std::atomic<bool>  s_interrupted;
};

} // namespace agent
