#pragma once

#include <functional>
#include <thread>
#include <atomic>

namespace agent {

using InterruptCallback = std::function<void(int ch)>; // ch: 3=Ctrl-C, 27=ESC

/// Global key watcher that runs in the background and detects Ctrl-C.
class KeyWatcher {
public:
    /// Register a callback to be invoked when Ctrl-C or ESC is detected.
    static void on_key(InterruptCallback cb);

    /// Start watching for Ctrl-C (char code 3). Thread-safe, idempotent.
    static void start();

    /// Stop the watcher thread. Thread-safe.
    static void stop();
    
private:
    static std::thread*       s_thread;
    static std::atomic<bool>  s_running;
    static InterruptCallback  s_callback;
};

} // namespace agent
