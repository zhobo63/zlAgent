#pragma once

#include <functional>
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <filesystem>
#include <utf8.h>

namespace agent {

// ── Key codes ───────────────────────────────────────────────

/// Represents a single key press — either a UTF-8 character or a special control key.
struct Key {
	union {
		unsigned char code[4];  // UTF-8 bytes, one at a time
		uint32_t ch;           // Unicode code point on Windows
	};
	int size;                 // >0 = valid byte count in code[], <0 = special key (size == -1 means ESC)
	uint8_t char_width;       // display width: 0=control, 1=narrow, 2=wide

	constexpr Key(uint32_t c = 0, int s = 0, int cw = 0) : ch(c), size(s), char_width(cw) {}

	/// Construct a Key from a Unicode code point (UTF-8 encoded).
	static Key from_codepoint(ucs4_t cp);

	// Comparison operators for special keys
	inline bool operator==(const Key& b) const {
		return size == b.size && ch == b.ch;
	}
	inline bool operator!=(const Key& b) const { return !(*this == b); }

	// Special key codes — first byte encodes the type, size is negative
	static Key K_ZERO;
	static Key K_ESC;
	static Key K_UP;
	static Key K_DOWN;
	static Key K_LEFT;
	static Key K_RIGHT;
	static Key K_TAB;
	static Key K_ENTER;
	static Key K_BACKSPACE;
	static Key K_DELETE;
	static Key K_PGUP;
	static Key K_PGDOWN;
	static Key K_HOME;
	static Key K_END;
	static Key K_CTRL_V;
	static Key K_ALT_ENTER;
	static Key K_CTRL_C;
	static Key K_CTRL_ENTER;
	static Key K_SHIFT_ENTER;
	static Key K_SPACE;
};

using KeyCallback = std::function<void(int k)>;
using ReadlineCallback = std::function<void(const Key& k)>;

/// Global key watcher that runs in the background and detects Ctrl-C.
class KeyWatcher {
public:
	// ── Original API ────────────────────────────────────────

	static std::vector<Key> utf8_to_keys(const std::string& s);

	/// Callback invoked when Ctrl-C or ESC is detected. Reset to nullptr with `clear_callback()`.
	static void on_key(KeyCallback cb);

	/// Clear the registered key callback.
	static void clear_callback() { s_callback = nullptr; }

	/// Start watching for Ctrl-C (char code 3). Thread-safe, idempotent.
	static void start();

	/// Stop the watcher thread. Thread-safe.
	static void stop();

	// ── New readline API ────────────────────────────────────

	static Key read_key();

	/// Read a line of input with rich editing abilities.
	/// @param prompt  The prompt text displayed before the cursor.
	/// @param cb      Callback invoked for every key press (extra notification).
	///                Can be used to decide whether to abort readline early.
	/// @returns       The input string on Enter; empty string on Ctrl+C.
	static std::string readline(const char* prompt, ReadlineCallback cb);

	static void init_keyboard();

	/// Restore terminal to normal mode after readline exits.
	static void close_keyboard();

	/// Read the next key from stdin. Blocks until input is available.

	// ── Completion API ──────────────────────────────────────

	/// Add keywords to the global completion pool (shared across all readline calls).
	static void add_keywords(const std::vector<std::string>& keywords);

	struct History {
		std::vector<std::string> entries;  // newest first (index 0 = most recent)
		int current_idx = -1;              // current position (-1 = not browsing)

		/// Add an entry. Removes all existing entries with the same content to avoid duplicates.
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

	struct LineBuffer {
		// Prompt is stored as string; user input uses vector<Key> for character-level indexing
		std::string prompt;     // fixed prefix (cannot be deleted)
		std::vector<Key> text;  // user input characters, each Key = one Unicode char in UTF-8
		size_t pos;             // character offset of cursor (0..text.size())
		int row;                // display row of cursor (1-based)
		int col;                // display column of cursor (1-based, 1-indexed)
		std::string hint;       // completion hint text displayed in dim color
		/// Full completion text (may be longer than the hint shown in dim color).
		std::string hint_candidates;
		size_t prompt_len = 0;  // byte length of the prompt prefix that cannot be deleted
		bool is_completion_active = false; // whether a completion menu is currently active
		bool is_display_dirty = false;     // true when screen content changed and needs full redraw

		// Completion menu state (only valid when is_completion_active)
		std::vector<std::string> candidates;
		int selected = 0;
		/// Number of candidates to display per page in the completion menu.
		size_t page_offset = 0;

		/// Cursor column when entering completion mode (used for restoring position).
		int input_col = 0;

		int prompt_row;

		// Cached display width of the prompt (computed once, invalidated on prompt change)
		mutable int cached_prompt_col = 0;

		LineBuffer() : pos(0), row(1), col(1) {}

		/// Recompute the cursor's display position from character offset.
		void recompute();

		void set_prompt(const std::string& p);

		void set_text(const std::string& text);

		/// Insert a UTF-8 character at the cursor position.
		void insert_char(const Key& k);

		void insert(const std::vector<Key>& keys) {
			for (const auto& k : keys) insert_char(k);
		}
		void insert(const std::string& string) {
			auto keys = utf8_to_keys(string);
			insert(keys);
		}

		/// Delete the character before the cursor (backspace). Won't delete into prompt.
		bool backspace();

		/// Move cursor left by one column. Won't move past prompt boundary.
		void move_left();

		/// Move cursor right by one column.
		void move_right();

		/// Move cursor up by one row (wrap to previous line end).
		void move_up(int term_width);

		/// Move cursor down by one row (wrap to next line start).
		void move_down(int term_width);

		/// Get the user input text (after prompt) as UTF-8 string.
		std::string input() const;

		/// Get the user input before the cursor (excludes prompt) as UTF-8 string.
		std::string get_prefix() const;

		/// Get the text after the cursor as UTF-8 string.
		std::string suffix() const;

		std::string display_text() const {
			std::string s = prompt;
			for (const auto& k : text) {
				s.append(reinterpret_cast<const char*>(k.code), k.size);
			}
			return s;
		}

		void resize(size_t n) {
			text.resize(n);
			if (pos > n) pos = n;
		}

		/// Total displayable length (text up to cursor + hint).
		size_t total_len() const { return pos + hint.size(); }

		/// Find the start of the current word (backwards from cursor, delimited by space or '@').
		int prefix_start() const;

		void show_hint();

		/// Replace the hint with the full completion text, inserting it at the cursor.
		void apply_hint();

		/// Print the completion hint in dim color at the cursor position.
		void print_hint();

		/// Clear both hint and full completion text.
		void clear_hint();

		/// Erase the prompt from screen.
		void clear_prompt();

		/// Show the completion menu with the given candidates. Returns the number of visible rows.
		int show_completion_menu(std::vector<std::string>& _candidates);

		/// Hide and restore cursor after leaving completion mode.
		void hide_completion_menu(int current_input_row);

		/// Insert a selected completion into the buffer at the cursor position.
		void insert_completion(const std::string& completion);

		/// Draw (or redraw) the completion menu on screen.
		void draw_completion_menu(int current_input_row);

		/// Erase the completion menu from screen.
		void clear_completion_menu(int current_input_row);
	};

private:
	static std::thread*				 s_read_thread;
	static std::mutex                s_read_mutex;

	static std::vector<Key>          s_read_queue;

	/// Whether the watcher thread should keep running.
	static std::atomic<bool>         s_running;

	/// Callback invoked when Ctrl-C or ESC is detected.
	static KeyCallback               s_callback;

	/// Global command history shared across all readline calls.
	static History                   history;

	/// Global keyword pool for tab completion.
	static std::vector<std::string>  s_keywords;

	// ── Completion helpers (private static) ───────────────────

	/// Case-insensitive prefix match: returns true if `str` starts with `prefix`.
	static bool ci_starts_with(const std::string& str, const std::string& prefix);

	/// Normalize path separators: convert all '\\' to '/' for uniform comparison.
	static std::string normalize_path(const std::string& path);

	/// Extract the directory portion of a path (e.g. "src/main.cpp" → "src/").
	static std::string get_path(const std::string& path);

	/// Scan a directory and collect file/directory names. Directories get trailing '/'.
	static void scan_directory(const std::filesystem::path& dir, std::vector<std::string>& entries);

	/// Build the candidate pool based on path-aware logic (keywords + filesystem).
	static void build_candidates(const std::string& prefix, std::vector<std::string>& candidates);

	static void read_key_thread();
	static void push_key_queue(const Key& k);
};

} // namespace agent
