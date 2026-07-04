#pragma once

#include <functional>
#include <thread>
#include <atomic>
#include <string>
#include <vector>

namespace agent {

// ── Key codes ───────────────────────────────────────────────

struct Key {
    union {
        unsigned char code[4];  // UTF-8 bytes, one at a time
        uint32_t ch = 0;           // Unicode code point on Windows
    };
    int size;                 // >0 = valid byte count in code[], <0 = special key (size == -1 means ESC)

    // Comparison operators for special keys
    inline bool operator==(const Key& b) const {
        return size == b.size && ch == b.ch;
    }
    inline bool operator!=(const Key& b) const { return !( *this == b); }
};

using InterruptCallback = std::function<void(int ch)>;

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

    static Key read_key();

    // ── Completion API ──────────────────────────────────────

    /// Add keywords to the global completion pool (shared across all readline calls).
    static void add_keywords(const std::vector<std::string>& keywords);

    // Completion state
    static std::vector<std::string> s_keywords;

    struct History {
        std::vector<std::string> entries;  // newest first (index 0 = most recent)
        int current_idx = -1;              // current position (-1 = not browsing)

        /// Add an entry. Removes all existing entries with the same content.
        void add(const std::string& entry);

        /// Move to previous (older) entry. Returns true if moved.
        bool prev();

        /// Move to next (newer) entry. Returns true if moved.
        bool next();

        /// Get current entry or nullptr if not browsing.
        const std::string* get_current() const;

        /// Check if we are currently browsing history.
        bool is_browsing() const { return current_idx >= 0; }

		void reset() { current_idx = -1; }
    };

    // Special key codes — first byte encodes the type, size is negative
    static constexpr Key K_ZERO = { {0}, 0 };
    static constexpr Key K_ESC = { {27}, -1 };
    static constexpr Key K_UP = { {38}, -2 };
    static constexpr Key K_DOWN = { {40}, -3 };
    static constexpr Key K_LEFT = { {37}, -4 };
    static constexpr Key K_RIGHT = { {39}, -5 };
    static constexpr Key K_TAB = { {9}, -6 };
    static constexpr Key K_ENTER = { {13}, -7 };
    static constexpr Key K_BACKSPACE = { {8}, -8 };
    static constexpr Key K_DELETE = { {46}, -9 };
    static constexpr Key K_PGUP = { {33}, -10 };
    static constexpr Key K_PGDOWN = { {34}, -11 };
    static constexpr Key K_HOME = { {36}, -12 };
    static constexpr Key K_END = { {35}, -13 };
    static constexpr Key K_CTRL_V = { {22}, -14 };   // Ctrl+V (paste)
    static constexpr Key K_ALT_ENTER = { {13}, -15 };   // Alt+Enter (insert newline)
    static constexpr Key K_CTRL_C = { {3}, -16 };   // Ctrl+C (interrupt)
    static constexpr Key K_CTRL_ENTER = { {13}, -17 };    // Ctrl+Enter (insert \n + enable line display)
    static constexpr Key K_SHIFT_ENTER = { {13}, -18 };    // Shift+Enter (insert \n + enable line display)

private:
    static std::thread*       s_thread;
    static std::atomic<bool>  s_running;
    static InterruptCallback  s_callback;
    static History history;
};

} // namespace agent
