#include "pch.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <set>
#include <sstream>
#include <stack>

#include "key_watcher.h"
#include "logger.h"
#include "utf8.h"

namespace agent {

// ============================================================================
// Cross-platform keyboard helpers (unchanged from original)
// ============================================================================

#ifdef _WIN32
#include <conio.h>
#include <windows.h>

#define getch _getch
#define kbhit _kbhit

void init_keyboard() {}
void close_keyboard() {}

#else
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

static struct termios oldt, newt;

static void init_keyboard() {
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
}

static void close_keyboard() {
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}

static int kbhit() {
    struct timeval tv;
    fd_set fds;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    return FD_ISSET(STDIN_FILENO, &fds);
}

static int getch() {
    return getchar();
}

#endif

// ============================================================================
// Static members
// ============================================================================

std::thread*                KeyWatcher::s_thread      = nullptr;
std::atomic<bool>           KeyWatcher::s_running      = false;
InterruptCallback           KeyWatcher::s_callback     = nullptr;
std::vector<std::string>    KeyWatcher::s_keywords;
KeyWatcher::History         KeyWatcher::history;

// ============================================================================
// Original API (unchanged)
// ============================================================================

void KeyWatcher::on_key(InterruptCallback cb) {
    s_callback = std::move(cb);
}

void KeyWatcher::start() {
    if (s_running.load()) return;
    s_running.store(true);
    init_keyboard();

    s_thread = new std::thread([] {
        while (s_running.load()) {
            int ch = 0;
            if (kbhit()) {
                ch = getch();
                if (s_callback) s_callback(ch);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
}

void KeyWatcher::stop() {
    if (!s_running.load()) return;
    s_running.store(false);
    if (s_thread) {
        s_thread->join();
        delete s_thread;
        s_thread = nullptr;
    }
    close_keyboard();
}

// ============================================================================
// Terminal I/O helpers — ANSI escape codes for cross-platform rendering
// ============================================================================

namespace term {

/// Get terminal width (columns). Falls back to 80.
static int get_width() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        return csbi.srWindow.Right - csbi.srWindow.Left + 1;
#else
    struct winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return ws.ws_col;
#endif
    return 80;
}

/// Get terminal height (rows). Falls back to 24.
static int get_height() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        return csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
#else
    struct winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
        return ws.ws_row;
#endif
    return 24;
}

/// Move cursor to position (row, col), 1-based.
static void move_cursor(int row, int col) {
    printf("\x1b[%d;%dH", row, col);
}

/// Save cursor position.
static void save_cursor() {
    printf("\x1b[s");
}

/// Restore cursor position.
static void restore_cursor() {
    printf("\x1b[u");
}

/// Clear from cursor to end of line.
static void clear_eol() {
    printf("\x1b[K");
}

/// Clear entire screen and move cursor to (1,1).
static void clear_screen() {
    printf("\x1b[2J\x1b[H");
}

/// Move cursor up by n rows.
static void cursor_up(int n) {
    if (n > 0) printf("\x1b[%dA", n);
}

/// Move cursor down by n rows.
static void cursor_down(int n) {
    if (n > 0) printf("\x1b[%dB", n);
}

/// Set text color: 2 = dim (for hints), 0 = reset.
static void set_color(int code) {
    printf("\x1b[%dm", code);
}

/// Flush output immediately.
static void flush() {
    fflush(stdout);
}

struct CursorPosition {
	int row;    // Y
	int col;    // X
};

#ifdef _WIN32
static CursorPosition get_cursor_position() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        return {1, 1};
    return {csbi.dwCursorPosition.Y + 1, csbi.dwCursorPosition.X + 1};
}
#else
static CursorPosition get_cursor_position() {
    // Send DSR request and read response synchronously (non-blocking)
    printf("\x1b[6n");
    fflush(stdout);
    struct timeval tv = {0, 50000}; // 50ms timeout
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) <= 0)
        return {1, 1};
    char buf[32];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n < 5 || buf[n - 1] != 'R')
        return {1, 1};
    int row = 0, col = 0;
    sscanf(buf + 2, "%d;%d", &row, &col);
    return {row, col};
}
#endif

} // namespace term

// ============================================================================
// UTF-8 helpers — compute display width of a UTF-8 string
// ============================================================================

/// Compute the display width of a single UTF-8 character (codepoint).
/// CJK and other wide characters return 2; ASCII returns 1.
static int utf8_char_width(ucs4_t cp) {
    if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) return 0; // control chars
    if (cp < 0x1100) return 1;
    // CJK, Hangul, etc. — wide characters
    if ((cp >= 0x1100 && cp <= 0x115F) ||   // Hangul Jamo
        (cp >= 0x2E80 && cp <= 0xA4CF) ||   // CJK radicals, Kangxi, etc.
        (cp >= 0xAC00 && cp <= 0xD7A3) ||   // Hangul syllables
        (cp >= 0xF900 && cp <= 0xFAFF) ||   // CJK compatibility
        (cp >= 0xFE10 && cp <= 0xFE6F) ||   // Vertical forms, small forms
        (cp >= 0xFF00 && cp <= 0xFF60) ||   // Fullwidth ASCII variants
        (cp >= 0xFFE0 && cp <= 0xFFE6)) {
        return 2;
    }
    return 1;
}

/// Compute the display width of a UTF-8 string.
static int utf8_str_width(const std::string& s) {
    int width = 0;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data());
    size_t len = s.size();
    while (len > 0) {
        ucs4_t cp;
        int n = utf8_mbtowc(&cp, p, static_cast<int>(len));
        if (n <= 0) break;
        width += utf8_char_width(cp);
        p += n;
        len -= n;
    }
    return width;
}

/// Advance `pos` bytes in the string by one display column.
static size_t utf8_advance_col(const std::string& s, size_t pos) {
    if (pos >= s.size()) return pos;
    ucs4_t cp;
    int n = utf8_mbtowc(&cp, reinterpret_cast<const unsigned char*>(s.data() + pos),
                        static_cast<int>(s.size() - pos));
    if (n <= 0) return pos + 1; // fallback: skip one byte
    return pos + n;
}

/// Go back `pos` bytes in the string by one display column.
static size_t utf8_back_col(const std::string& s, size_t pos) {
    if (pos == 0) return 0;
    // Walk backwards: find the start of the current UTF-8 character
    size_t p = pos - 1;
    while (p > 0 && (s[p] & 0xC0) == 0x80) {
        --p;
    }
    return p;
}

// Helper: get Unicode code point from Key (for callback)
static int key_to_codepoint(const Key& k) {
    if (k.size < 0 || k.size > 4) return -1; // special key or invalid
    ucs4_t cp = 0;
    utf8_mbtowc(&cp, k.code, k.size);
    return static_cast<int>(cp);
}

// Helper: get UTF-8 bytes as string from Key
static std::string key_to_utf8(const Key& k) {
    if (k.size < 0 || k.size > 4) return ""; // special key or invalid
    return std::string(reinterpret_cast<const char*>(k.code), static_cast<size_t>(k.size));
}

// Helper: convert Unicode code point to Key
static Key cp_to_key(ucs4_t cp) {
    // Cast to uint32_t so shifts work correctly even when ucs4_t is 16-bit (Windows wchar_t)
    uint32_t c = static_cast<uint32_t>(cp);
    Key k{};
    if (c < 0x80) { k.code[0] = static_cast<unsigned char>(c); k.size = 1; }
    else if (c < 0x800) {
        k.code[0] = static_cast<unsigned char>(0xC0 | ((c >> 6) & 0x1F));
        k.code[1] = static_cast<unsigned char>(0x80 | (c & 0x3F));
        k.size = 2;
    }
    else if (c < 0x10000) {
        k.code[0] = static_cast<unsigned char>(0xE0 | ((c >> 12) & 0x0F));
        k.code[1] = static_cast<unsigned char>(0x80 | ((c >> 6) & 0x3F));
        k.code[2] = static_cast<unsigned char>(0x80 | (c & 0x3F));
        k.size = 3;
    }
    else {
        k.code[0] = static_cast<unsigned char>(0xF0 | ((c >> 18) & 0x07));
        k.code[1] = static_cast<unsigned char>(0x80 | ((c >> 12) & 0x3F));
        k.code[2] = static_cast<unsigned char>(0x80 | ((c >> 6) & 0x3F));
        k.code[3] = static_cast<unsigned char>(0x80 | (c & 0x3F));
        k.size = 4;
    }
    return k;
}

// Helper: convert a UTF-8 string to vector<Key>
static std::vector<Key> utf8_to_keys(const std::string& s) {
    std::vector<Key> keys;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data());
    size_t len = s.size();
    while (len > 0) {
        ucs4_t cp;
        int n = utf8_mbtowc(&cp, p, static_cast<int>(len));
        if (n <= 0) break;
        uint32_t c = static_cast<uint32_t>(cp);
        Key k{};
        if (c < 0x80) { k.code[0] = static_cast<unsigned char>(c); k.size = 1; }
        else if (c < 0x800) {
            k.code[0] = static_cast<unsigned char>(0xC0 | ((c >> 6) & 0x1F));
            k.code[1] = static_cast<unsigned char>(0x80 | (c & 0x3F));
            k.size = 2;
        }
        else if (c < 0x10000) {
            k.code[0] = static_cast<unsigned char>(0xE0 | ((c >> 12) & 0x0F));
            k.code[1] = static_cast<unsigned char>(0x80 | ((c >> 6) & 0x3F));
            k.code[2] = static_cast<unsigned char>(0x80 | (c & 0x3F));
            k.size = 3;
        }
        else {
            k.code[0] = static_cast<unsigned char>(0xF0 | ((c >> 18) & 0x07));
            k.code[1] = static_cast<unsigned char>(0x80 | ((c >> 12) & 0x3F));
            k.code[2] = static_cast<unsigned char>(0x80 | ((c >> 6) & 0x3F));
            k.code[3] = static_cast<unsigned char>(0x80 | (c & 0x3F));
            k.size = 4;
        }
        keys.push_back(k);
        p += n; len -= n;
    }
    return keys;
}

// ============================================================================
// Multi-line buffer — stores the input text and manages cursor position
// ============================================================================

struct LineBuffer {
    // Prompt is stored as string; user input uses vector<Key> for character-level indexing
    std::string prompt;     // fixed prefix (cannot be deleted)
    std::vector<Key> text;  // user input characters, each Key = one Unicode char in UTF-8
    size_t pos;             // character offset of cursor (0..text.size())
    int row;                // display row of cursor (1-based)
    int col;                // display column of cursor (1-based, 1-indexed)
    std::string hint;       // completion hint text displayed in dim color
    std::string hint_candidates;
    size_t prompt_len = 0;  // byte length of the prompt prefix that cannot be deleted
    bool is_completion_active = false; // whether a completion menu is currently active
    // Completion menu state (only valid when is_completion_active)
    std::vector<std::string> candidates;
    int selected = 0;
    size_t page_offset = 0;
    int input_col = 0;           // cursor column when entering completion mode
    bool is_autotab = false;     // whether we're in autotab flow

    LineBuffer() : pos(0), row(1), col(1) {}

    /// Recompute the cursor's display position from character offset.
    void recompute();

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
    std::string prefix() const;

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
        if (pos > n) { pos = n; }
    }

    /// Total displayable length (text up to cursor + hint).
    size_t total_len() const { return pos + hint.size(); }

    void apply_hint() {
        auto hint_keys = utf8_to_keys(hint);
		auto keys = utf8_to_keys(hint_candidates);
        int trim_count = keys.size() - hint_keys.size();
		resize(text.size() - trim_count);
        insert(keys);
        hint.clear();
		hint_candidates.clear();
    }

    void print_hint() {
        term::set_color(2); // dim
        for (size_t i = 0; i < hint.size(); ++i) {
            if (hint[i] == '\n') {
                printf("\n");
            }
            else {
                putchar(hint[i]);
            }
        }
    }

    void clear_hint() {
        hint.clear();
        hint_candidates.clear();
    }
};

void LineBuffer::recompute() {
    int term_width = term::get_width();

    // Count display columns from prompt up to cursor position
    row = 1;
    col = 1;
    for (size_t i = 0; i < prompt.size(); ++i) {
        if (prompt[i] == '\n') { row++; col = 1; }
        else { col += utf8_char_width(static_cast<unsigned char>(prompt[i])); }
    }

    // Count display columns from user input up to cursor
    for (size_t i = 0; i < pos && i < text.size(); ++i) {
        ucs4_t cp;
        int n = utf8_mbtowc(&cp, text[i].code, text[i].size);
        if (n <= 0 || cp == '\n') { row++; col = 1; continue; }
        col += utf8_char_width(cp);
        if (col > term_width) {
            row++;
            col = 1;
        }
    }
}

void LineBuffer::insert_char(const Key& k) {
    text.insert(text.begin() + static_cast<long>(pos), k);
    pos++;
    recompute();
}

bool LineBuffer::backspace() {
    if (pos == 0) return false; // don't delete into prompt

    // Delete the character before cursor
    text.erase(text.begin() + static_cast<long>(pos - 1));
    pos--;
    recompute();
    return true;
}

void LineBuffer::move_left() {
    if (pos > 0) pos--;
}

void LineBuffer::move_right() {
    if (pos < text.size()) pos++;
}

void LineBuffer::move_up(int term_width) {
    // Find the row above and go to the corresponding column
    int current_row = 1, current_col = 1;
    for (size_t i = 0; i < prompt.size(); ++i) {
        if (prompt[i] == '\n') { current_row++; current_col = 1; }
        else { current_col += utf8_char_width(static_cast<unsigned char>(prompt[i])); }
    }

    // Count columns up to cursor position
    for (size_t i = 0; i < pos && i < text.size(); ++i) {
        ucs4_t cp;
        int n = utf8_mbtowc(&cp, text[i].code, text[i].size);
        if (n <= 0 || cp == '\n') { current_row++; current_col = 1; continue; }
        current_col += utf8_char_width(cp);
        if (current_col > term_width) {
            current_row++;
            current_col = 1;
        }
    }

    // Walk backwards to find the start of previous display line
    size_t prev_line_start = pos;
    for (size_t i = pos; i > 0; --i) {
        ucs4_t cp;
        int n = utf8_mbtowc(&cp, text[i - 1].code, text[i - 1].size);
        if (n <= 0 || cp == '\n') break;
        prev_line_start = i - 1;
    }

    // Walk backwards to find the start of previous display line
    size_t prev_prev_line_start = prev_line_start;
    for (size_t i = prev_line_start; i > 0; --i) {
        ucs4_t cp;
        int n = utf8_mbtowc(&cp, text[i - 1].code, text[i - 1].size);
        if (n <= 0 || cp == '\n') break;
        prev_prev_line_start = i - 1;
    }

    // Calculate target column within the previous display line
    int offset_in_current = current_col - 1; // 0-based column index
    size_t target_pos = prev_prev_line_start + static_cast<size_t>(offset_in_current);
    if (target_pos > prev_line_start) target_pos = prev_line_start;

    pos = target_pos;
}

void LineBuffer::move_down(int term_width) {
    // Find the row below and go to the corresponding column
    int current_row = 1, current_col = 1;
    for (size_t i = 0; i < prompt.size(); ++i) {
        if (prompt[i] == '\n') { current_row++; current_col = 1; }
        else { current_col += utf8_char_width(static_cast<unsigned char>(prompt[i])); }
    }

    // Count columns up to cursor position
    for (size_t i = 0; i < pos && i < text.size(); ++i) {
        ucs4_t cp;
        int n = utf8_mbtowc(&cp, text[i].code, text[i].size);
        if (n <= 0 || cp == '\n') { current_row++; current_col = 1; continue; }
        current_col += utf8_char_width(cp);
        if (current_col > term_width) {
            current_row++;
            current_col = 1;
        }
    }

    // Walk forward to find the end of next display line
    size_t next_line_end = pos;
    for (size_t i = pos; i < text.size(); ++i) {
        ucs4_t cp;
        int n = utf8_mbtowc(&cp, text[i].code, text[i].size);
        if (n <= 0 || cp == '\n') break;
        next_line_end = i + 1;
    }

    // Walk forward to find the end of current display line
    size_t line_end = pos;
    for (size_t i = pos; i < text.size(); ++i) {
        ucs4_t cp;
        int n = utf8_mbtowc(&cp, text[i].code, text[i].size);
        if (n <= 0 || cp == '\n') break;
        line_end = i + 1;
    }

    // Calculate target column within the next display line
    int offset_in_current = current_col - 1; // 0-based column index
    size_t target_pos = line_end + static_cast<size_t>(offset_in_current);
    if (target_pos > next_line_end) target_pos = next_line_end;

    pos = target_pos;
}

std::string LineBuffer::input() const {
    std::string result;
    for (const auto& k : text) {
        if (k.size > 0 && k.size <= 4)
            result.append(reinterpret_cast<const char*>(k.code), static_cast<size_t>(k.size));
    }
    return result;
}

std::string LineBuffer::prefix() const {
    std::string result;
    for (size_t i = 0; i < pos && i < text.size(); ++i) {
        if (text[i].size > 0 && text[i].size <= 4)
            result.append(reinterpret_cast<const char*>(text[i].code), static_cast<size_t>(text[i].size));
    }
    return result;
}

std::string LineBuffer::suffix() const {
    std::string result;
    for (size_t i = pos; i < text.size(); ++i) {
        if (text[i].size > 0 && text[i].size <= 4)
            result.append(reinterpret_cast<const char*>(text[i].code), static_cast<size_t>(text[i].size));
    }
    return result;
}

// ============================================================================
// History — stores previous inputs with deduplication
// ============================================================================




void KeyWatcher::History::add(const std::string& entry) {
    // Remove all existing entries with the same content
    auto it = std::remove_if(entries.begin(), entries.end(),
        [&entry](const std::string& e) { return e == entry; });
    entries.erase(it, entries.end());

    // Insert at front (most recent)
    entries.insert(entries.begin(), entry);

    // Keep a reasonable limit
    if (entries.size() > 500) {
        entries.resize(500);
    }
}

bool KeyWatcher::History::prev() {
    if (current_idx < 0 || current_idx >= static_cast<int>(entries.size()) - 1) {
        if (current_idx < 0) current_idx = 0;
        else return false;
        return true;
    }
    current_idx++;
    return true;
}

bool KeyWatcher::History::next() {
    if (current_idx <= 0) {
        current_idx = -1;
        return true;
    }
    current_idx--;
    return true;
}

const std::string* KeyWatcher::History::get_current() const {
    if (current_idx < 0 || current_idx >= static_cast<int>(entries.size()))
        return nullptr;
    return &entries[current_idx];
}

// ============================================================================
// Completion — build candidates, hint display, menu rendering
// ============================================================================

/// Case-insensitive prefix match.
static bool ci_starts_with(const std::string& str, const std::string& prefix) {
    if (str.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(str[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i])))
            return false;
    return true;
}

/// Normalize path separators: convert all '\\' to '/' so we can use a single rfind('/') everywhere.
static std::string normalize_path(const std::string& path) {
    std::string result = path;
    for (auto& c : result)
        if (c == '\\')
            c = '/';
    return result;
}

/// Extract the directory portion of a path.
/// "src/main.cpp" -> "src/"
/// "src/"         -> "src/"
static std::string get_path(const std::string& path) {
    std::string norm = normalize_path(path);
    if (!norm.empty() && norm.back() == '/')
        return norm;
    size_t last_sep = norm.rfind('/');
    if (last_sep != std::string::npos)
        return norm.substr(0, last_sep + 1);
    return "";
}

/// Scan a directory and collect file/directory names. Directories get trailing '/'.
static void scan_directory(const std::filesystem::path& dir, std::vector<std::string>& entries) {
    try {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_directory()) {
                entries.push_back(entry.path().filename().string() + "/");
            } else if (entry.is_regular_file()) {
                entries.push_back(entry.path().filename().string());
            }
        }
    } catch (const std::filesystem::filesystem_error&) {
        // silently skip
    }
}

/// Build the candidate pool based on path-aware logic.
static void build_candidates(const std::string& prefix, std::vector<std::string>& candidates) {
    // Normalize all separators to '/' so we can use a single rfind
    std::string norm = normalize_path(prefix);
    size_t last_sep = norm.rfind('/');

    if (last_sep != std::string::npos) {
        // Path-aware: scan the directory before the separator
        std::filesystem::path dir_path(norm.substr(0, last_sep));
        if (!dir_path.is_absolute()) {
            dir_path = std::filesystem::current_path() / dir_path;
        }
        std::string after_sep = norm.substr(last_sep + 1);

        scan_directory(dir_path, candidates);

		if (!after_sep.empty()) {
            auto it = std::remove_if(candidates.begin(), candidates.end(),
                [&after_sep](const std::string& e) {
                    return !ci_starts_with(e, after_sep);
                });
            candidates.erase(it, candidates.end());
        }
    } else {
        // No path separator: merge keywords + current directory entries
        for (const auto& kw : KeyWatcher::s_keywords) {
            if (ci_starts_with(kw, prefix)) {
                candidates.push_back(kw);
            }
        }

        std::vector<std::string> dir_entries;
        scan_directory(std::filesystem::current_path(), dir_entries);
        for (const auto& de : dir_entries) {
            if (ci_starts_with(de, prefix)) {
                candidates.push_back(de);
            }
        }
    }

    // Remove duplicates and sort alphabetically (case-insensitive)
    std::set<std::string> seen;
    auto it = std::remove_if(candidates.begin(), candidates.end(),
        [&seen](const std::string& c) { return !seen.insert(c).second; });
    candidates.erase(it, candidates.end());

    std::sort(candidates.begin(), candidates.end(),
        [](const std::string& a, const std::string& b) {
            for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
                char ca = std::tolower(static_cast<unsigned char>(a[i]));
                char cb = std::tolower(static_cast<unsigned char>(b[i]));
                if (ca != cb) return ca < cb;
            }
            return a.size() < b.size();
        });
}

/// Compute the longest common prefix of all candidates.
static std::string longest_common_prefix(const std::vector<std::string>& candidates) {
    if (candidates.empty()) return "";
    std::string lcp = candidates[0];
    for (size_t i = 1; i < candidates.size(); ++i) {
        size_t j = 0;
        for (; j < lcp.size() && j < candidates[i].size(); ++j) {
            if (std::tolower(static_cast<unsigned char>(lcp[j])) !=
                std::tolower(static_cast<unsigned char>(candidates[i][j]))) {
                break;
            }
        }
        lcp = lcp.substr(0, j);
        if (lcp.empty()) break;
    }
    return lcp;
}

// ============================================================================
// Ctrl+V paste helper (Windows only)
// ============================================================================

#ifdef _WIN32
static std::string get_clipboard_text() {
    if (!OpenClipboard(nullptr)) return "";
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    std::string result;
    if (h) {
        const wchar_t* wtext = static_cast<const wchar_t*>(GlobalLock(h));
        if (wtext) {
            int len = WideCharToMultiByte(CP_UTF8, 0, wtext, -1, nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                result.resize(len - 1);
                WideCharToMultiByte(CP_UTF8, 0, wtext, -1, &result[0], len, nullptr, nullptr);
            }
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return result;
}
#else
static std::string get_clipboard_text() {
    return ""; // not supported on POSIX in v1
}
#endif

// ============================================================================
// Completion menu — render and interact with completion options
// ============================================================================

/// Read a single key press, handling escape sequences for arrow keys etc.
Key KeyWatcher::read_key() {
#ifdef _WIN32
    INPUT_RECORD rec;
    DWORD count;
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);

    while (true) {
        if (!PeekConsoleInput(hIn, &rec, 1, &count)) return K_ESC;
        if (count == 0) continue;

        ReadConsoleInputW(hIn, &rec, 1, &count);

        if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown) {
            // ESC detection
            if (rec.Event.KeyEvent.wVirtualKeyCode == 27) {
                return K_ESC;
            }
            // Ctrl+V detection
            if (rec.Event.KeyEvent.wVirtualKeyCode == 'V' &&
                (rec.Event.KeyEvent.dwControlKeyState & LEFT_CTRL_PRESSED ||
                 rec.Event.KeyEvent.dwControlKeyState & RIGHT_CTRL_PRESSED)) {
                return K_CTRL_V;
            }

            // Alt+Enter detection
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_RETURN &&
                (rec.Event.KeyEvent.dwControlKeyState & LEFT_ALT_PRESSED ||
                 rec.Event.KeyEvent.dwControlKeyState & RIGHT_ALT_PRESSED)) {
                return K_ALT_ENTER;
            }

            // Ctrl+Enter — insert newline + enable line display mode
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_RETURN &&
                (rec.Event.KeyEvent.dwControlKeyState & LEFT_CTRL_PRESSED ||
                 rec.Event.KeyEvent.dwControlKeyState & RIGHT_CTRL_PRESSED)) {
                return K_CTRL_ENTER;
            }

            // Shift+Enter — insert newline + enable line display mode
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_RETURN &&
                (rec.Event.KeyEvent.dwControlKeyState & SHIFT_PRESSED)) {
                return K_SHIFT_ENTER;
            }

            // Ctrl+C detection (already handled by ASCII 3, but be explicit)
            if (rec.Event.KeyEvent.wVirtualKeyCode == 'C' &&
                (rec.Event.KeyEvent.dwControlKeyState & LEFT_CTRL_PRESSED ||
                 rec.Event.KeyEvent.dwControlKeyState & RIGHT_CTRL_PRESSED)) {
                return K_CTRL_C;
            }

            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_UP)    return K_UP;
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_DOWN)  return K_DOWN;
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_LEFT)  return K_LEFT;
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_RIGHT) return K_RIGHT;
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_TAB)   return K_TAB;
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_RETURN)return K_ENTER;
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_BACK)  return K_BACKSPACE;
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_DELETE)return K_DELETE;
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_PRIOR) return K_PGUP;
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_NEXT)  return K_PGDOWN;
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_HOME)  return K_HOME;
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_END)   return K_END;

            // Normal character — use UnicodeChar and convert to UTF-8
            uint32_t chr = rec.Event.KeyEvent.uChar.UnicodeChar;
            if (!chr) continue;  // no char, skip (e.g. arrow keys with modifier)

            // High surrogate pair
            if (chr >= 0xD800 && chr <= 0xDBFF) {
                uint32_t lo = 0;
                INPUT_RECORD rec2;
                DWORD count2;
                ReadConsoleInputW(hIn, &rec2, 1, &count2);
                if (rec2.EventType == KEY_EVENT &&
                    rec2.Event.KeyEvent.bKeyDown &&
                    rec2.Event.KeyEvent.uChar.UnicodeChar >= 0xDC00 &&
                    rec2.Event.KeyEvent.uChar.UnicodeChar <= 0xDFFF) {
                    lo = rec2.Event.KeyEvent.uChar.UnicodeChar;
                }
                if (lo) chr = ((chr - 0xD800) << 10) + (lo - 0xDC00) + 0x10000;
            }

            // Convert Unicode code point to UTF-8 bytes
            Key k{};
            if (chr < 0x80) {
                k.code[0] = static_cast<unsigned char>(chr);
                k.size = 1;
            } else if (chr < 0x800) {
                k.code[0] = static_cast<unsigned char>(0xC0 | ((chr >> 6) & 0x1F));
                k.code[1] = static_cast<unsigned char>(0x80 | (chr & 0x3F));
                k.size = 2;
            } else if (chr < 0x10000) {
                k.code[0] = static_cast<unsigned char>(0xE0 | ((chr >> 12) & 0x0F));
                k.code[1] = static_cast<unsigned char>(0x80 | ((chr >> 6) & 0x3F));
                k.code[2] = static_cast<unsigned char>(0x80 | (chr & 0x3F));
                k.size = 3;
            } else {
                k.code[0] = static_cast<unsigned char>(0xF0 | ((chr >> 18) & 0x07));
                k.code[1] = static_cast<unsigned char>(0x80 | ((chr >> 12) & 0x3F));
                k.code[2] = static_cast<unsigned char>(0x80 | ((chr >> 6) & 0x3F));
                k.code[3] = static_cast<unsigned char>(0x80 | (chr & 0x3F));
                k.size = 4;
            }
            return k;
        }
    }
#else
    unsigned char buf[8];
    size_t n = 0;

    if (read(STDIN_FILENO, &buf[n], 1) != 1) return K_ESC;
    n++;

    // Ctrl+V = ASCII 22
    if (buf[0] == 22) return K_CTRL_V;

    if (buf[0] == '\t')   return K_TAB;
    if (buf[0] == '\r') {
        // On POSIX, Ctrl+Enter and Enter both produce \r.
        // Detect Ctrl modifier: if the previous byte was a control char (1..26), this is Ctrl+Enter.
        static unsigned char last_byte = 0;
        bool ctrl_pressed = (last_byte >= 1 && last_byte <= 26);
        last_byte = buf[0];

        if (ctrl_pressed) {
            // Check Shift modifier: if the byte before the control char was ESC, it's Alt+Enter.
            // For Ctrl+Enter vs Shift+Enter on POSIX we can't distinguish without raw mode,
            // so we treat Ctrl+Enter as K_CTRL_ENTER and plain Enter as K_ENTER.
            return K_CTRL_ENTER;
        }
        return K_ENTER;
    }
    if (buf[0] == 127)    return K_BACKSPACE; // DEL key on Linux
    if (buf[0] == 8)      return K_BACKSPACE; // Ctrl+H = backspace

    if (buf[0] == 27) {   // ESC sequence or Alt+
        if (read(STDIN_FILENO, &buf[n], 1) != 1) return K_ESC;
        n++;

        if (buf[1] == '[') {
            if (read(STDIN_FILENO, &buf[n], 1) != 1) return K_ESC;
            n++;

            switch (buf[2]) {
                case 'A': return K_UP;
                case 'B': return K_DOWN;
                case 'C': return K_RIGHT;
                case 'D': return K_LEFT;
                case 'F': return K_END;
                case 'H': return K_HOME;
                case '~':
                    if (read(STDIN_FILENO, &buf[n], 1) != 1) return K_ESC;
                    n++;
                    switch (buf[3]) {
                        case '2': return K_DELETE;
                        case '5': return K_PGUP;
                        case '6': return K_PGDOWN;
                        default:  return K_ESC;
                    }
                default: return K_ESC;
            }
        } else if (buf[1] == 'O') {
            if (read(STDIN_FILENO, &buf[n], 1) != 1) return K_ESC;
            n++;
            switch (buf[2]) {
                case 'F': return K_END;
                case 'H': return K_HOME;
                case 'P': return K_PGUP;
                case 'Q': return K_PGDOWN;
                default:  return K_ESC;
            }
        } else if (buf[1] == '\r') {
            // Alt+Enter on some terminals
            return K_ALT_ENTER;
        }
        return K_ESC;
    }

    // Normal ASCII character
    if (buf[0] >= 32 && buf[0] < 127) {
        Key k{};
        k.code[0] = buf[0];
        k.size = 1;
        return k;
    }

    // UTF-8 multi-byte: read continuation bytes
    unsigned char c = buf[0];
    size_t expected;
    if ((c & 0xE0) == 0xC0) expected = 2;
    else if ((c & 0xF0) == 0xE0) expected = 3;
    else if ((c & 0xF8) == 0xF0) expected = 4;
    else return K_ESC;

    while (n < expected && read(STDIN_FILENO, &buf[n], 1) == 1) n++;

    Key k{};
    for (size_t i = 0; i < n; ++i) k.code[i] = buf[i];
    k.size = static_cast<int>(n);
    return k;
#endif
}

static constexpr size_t MAX_DISPLAYED = 9;

enum class AutotabResult {
    DONE,              // filled or empty — exit autotab
    SHOW_MENU,         // multiple candidates — enter completion mode
    CONTINUE_AUTOTAB,
};

static void insert_completion(LineBuffer& buf, const std::string& completion) {
	std::string prefix = buf.prefix();
	//std::string path = get_path(prefix);
	//std::string filename = completion.substr(path.length());
	//std::string to_insert = completion.substr(path.length());
    buf.resize(prefix.length());
	auto keys = utf8_to_keys(completion);
	for (const auto& k : keys) buf.insert_char(k);
}

/// Draw the completion menu below the input line.
static void draw_completion_menu(const LineBuffer& buf, int current_input_row) {
    size_t max_displayed = std::min(buf.candidates.size(), MAX_DISPLAYED);
    printf("\n");
    for (size_t i = 0; i < max_displayed; ++i) {
        size_t idx = buf.page_offset + i;
        bool is_selected = (static_cast<int>(idx - buf.page_offset) == buf.selected);
        printf("\x1b[32m%zu\x1b[0m ", idx + 1);
        printf("\x1b[%dm%s\x1b[0m", is_selected ? 37 : 2, buf.candidates[idx].c_str());
        term::set_color(0);
        printf("\n");
    }

    if (buf.candidates.size() > MAX_DISPLAYED) {
        bool can_go_up = (buf.page_offset > 0);
        bool can_go_down = (buf.page_offset + MAX_DISPLAYED < buf.candidates.size());
        printf("\x1b[2m");
        printf("candidates %zu-%zu of %zu ", buf.page_offset + 1,
               std::min(buf.page_offset + MAX_DISPLAYED, buf.candidates.size()),
               buf.candidates.size());
        if (can_go_up)
            printf("[PgUp] ");
        if (can_go_down)
            printf("[PgDn]");
        term::set_color(0);
        printf("\n");
    }

    term::flush();
}

/// Clear the completion menu lines and restore cursor.
static void clear_completion_menu(const LineBuffer& buf, int current_input_row) {
    size_t max_displayed = std::min(buf.candidates.size(), MAX_DISPLAYED);
    size_t total_menu_lines = max_displayed + (buf.candidates.size() > MAX_DISPLAYED ? 1 : 0);
    if (buf.candidates.size() > MAX_DISPLAYED)
        total_menu_lines++;

    for (size_t i = 0; i < total_menu_lines; ++i) {
        term::move_cursor(current_input_row + 1 + static_cast<int>(i), 1);
        term::clear_eol();
    }
    printf("\x1b[0m");
    term::move_cursor(current_input_row, buf.input_col);
    term::flush();
}

/// Process one step of autotab. Returns DONE when finished, SHOW_MENU when menu should be shown.
static AutotabResult process_autotab_step(LineBuffer& buf) {
    std::string prefix = buf.prefix();
    std::string path = get_path(prefix);

    // If there's a hint and it matches the current input, confirm it
    if (!buf.hint.empty()) {
        buf.apply_hint();
        buf.recompute();
        return AutotabResult::DONE;
    }

    std::vector<std::string> candidates;
    build_candidates(prefix, candidates);

    if (candidates.empty()) {
        return AutotabResult::DONE;
    }

    if (candidates.size() == 1) {
        insert_completion(buf, candidates[0]);
        return AutotabResult::DONE;
    }

    // Multiple candidates: apply longest common prefix first
    std::string lcp = longest_common_prefix(candidates);
    if (!lcp.empty() && lcp != prefix) {
        auto keys = utf8_to_keys(lcp.substr(prefix.size()));
        for (const auto& k : keys) buf.insert_char(k);
        // Rebuild candidates with the new prefix
        prefix = lcp;
        candidates.clear();
        build_candidates(lcp, candidates);
    }

    if (candidates.empty()) {
        return AutotabResult::DONE;
    }

    if (candidates.size() == 1) {
        insert_completion(buf, candidates[0]);
        return AutotabResult::CONTINUE_AUTOTAB;
    }

    // Multiple candidates remain — show menu
    buf.candidates = std::move(candidates);
    buf.selected = 0;
    buf.page_offset = 0;
    return AutotabResult::SHOW_MENU;
}


// ============================================================================
// Completion API
// ============================================================================

void KeyWatcher::add_keywords(const std::vector<std::string>& keywords) {
    for (const auto& kw : keywords) {
        if (std::find(s_keywords.begin(), s_keywords.end(), kw) == s_keywords.end()) {
            s_keywords.push_back(kw);
        }
    }
}

// ============================================================================
// readline implementation — full custom implementation
// ============================================================================

std::string KeyWatcher::readline(const char* prompt, InterruptCallback cb) {
    const char* prompt_text = (prompt ? prompt : "");
    int prompt_len = static_cast<int>(strlen(prompt_text));
    int term_width = term::get_width();

    LineBuffer buf;

    // Store prompt as string; user input uses vector<Key>
    buf.prompt = prompt_text;
    buf.prompt_len = static_cast<size_t>(prompt_len);
    buf.pos = 0;
	history.reset();

    // State for tracking whether we're browsing history
    std::string original_text;
    bool was_browsing = false;
    // Get cursor position before printing (prompt's starting row)
    auto cursor_pos = term::get_cursor_position();

    while (true) {
        // ── Autotab: keep stepping if still in autotab flow and not showing menu ──
        while (buf.is_autotab && !buf.is_completion_active) {
            AutotabResult result = process_autotab_step(buf);
            if (result == AutotabResult::DONE) {
                buf.is_autotab = false;
            } else if (result == AutotabResult::SHOW_MENU) {
                // Enter completion mode
                int H = term::get_height();
                size_t max_displayed = std::min(buf.candidates.size(), MAX_DISPLAYED);
                size_t total_menu_lines = max_displayed + (buf.candidates.size() > max_displayed ? 1 : 0);
                if (buf.candidates.size() > MAX_DISPLAYED)
                    total_menu_lines++;
                buf.is_completion_active = true;
                auto pos_before = term::get_cursor_position();
                int scroll_amount = std::max(0, static_cast<int>(pos_before.row + total_menu_lines - H));
                for (int i = 0; i < scroll_amount; i++) {
					printf("\n");
				}
                auto pos = term::get_cursor_position();
				pos.col = pos_before.col;
                term::flush();
                buf.input_col = pos.col;
                // Adjust start_row for scrolling
                if (scroll_amount > 0) {
                    cursor_pos.row = std::max(1, cursor_pos.row - scroll_amount);
                }
                int final_row = buf.row + cursor_pos.row - 1;
                clear_completion_menu(buf, final_row);
            } else if (result == AutotabResult::CONTINUE_AUTOTAB) {
                // LCP filled single candidate — continue autotab
            }
        }

        // ── Render the current line(s) ────────────────────────
        term::move_cursor(cursor_pos.row, 1);
        term::clear_eol();
        std::cout << buf.display_text();

        // Print hint in dim color
        if (!buf.hint.empty()) {
            buf.print_hint();
        }

        term::set_color(0); // reset color
        term::flush();
        buf.recompute();
        // Add offset: prompt starts at start_row, not row 1
        int final_row = buf.row + cursor_pos.row - 1;
        term::move_cursor(final_row, buf.col);

        // ── Completion menu rendering ────────────────────────
        if (buf.is_completion_active) {
            draw_completion_menu(buf, final_row);
            term::move_cursor(final_row, buf.input_col);
            term::flush();
        }

        // ── Read a key ────────────────────────────────────────
        Key k = read_key();

        // Call the callback for every key press (extra notification)
        if (cb) {
            cb(k.ch);
        }

        // ── Completion mode handling ─────────────────────────
        if (buf.is_completion_active) {
            size_t max_displayed = std::min(buf.candidates.size(), MAX_DISPLAYED);

            // Direct selection: 1-9
            bool handled = false;
            if (k.ch >= '1' && k.ch <= '9') {
                int choice = k.ch - '0';
                if (choice > 0 && static_cast<size_t>(choice) <= buf.candidates.size()) {
                    // Fill chosen candidate
                    insert_completion(buf, buf.candidates[choice - 1]);
                    // Clear menu
                    clear_completion_menu(buf, final_row);
                    buf.is_completion_active = false;
                    handled = true;
                }
            }

            if (!handled && (k == K_DOWN || k == K_RIGHT)) {
                buf.selected++;
                if (buf.selected >= static_cast<int>(max_displayed)) buf.selected = 0;
                handled = true;
            }
            else if (!handled && (k == K_UP || k == K_LEFT)) {
                buf.selected--;
                if (buf.selected < 0) buf.selected = static_cast<int>(max_displayed) - 1;
                handled = true;
            }

            // Page navigation
            if (!handled && k == K_PGDOWN && buf.candidates.size() > max_displayed) {
                buf.page_offset += max_displayed;
                if (buf.page_offset + max_displayed > buf.candidates.size())
                    buf.page_offset = buf.candidates.size() - max_displayed;
                buf.selected = 0;
                handled = true;
            }
            else if (!handled && k == K_PGUP && buf.candidates.size() > max_displayed) {
                if (buf.page_offset >= max_displayed)
                    buf.page_offset -= max_displayed;
                else
                    buf.page_offset = 0;
                buf.selected = static_cast<int>(max_displayed) - 1;
                handled = true;
            }

            // Confirm selection
            if (!handled && (k == K_ENTER || k == K_TAB)) {
                int chosen = static_cast<int>(buf.page_offset + buf.selected);
                insert_completion(buf, buf.candidates[chosen]);
                // Clear menu, keep autotab active to continue completing
                clear_completion_menu(buf, final_row);
                buf.is_completion_active = false;
                handled = true;
            }

            // Cancel
            if (!handled && (k == K_ESC || k == K_DELETE)) {
                clear_completion_menu(buf, final_row);
                buf.is_completion_active = false;
                buf.is_autotab = false; // cancelling menu also cancels autotab
                handled = true;
            }

            // Normal character input — exit menu and let main loop handle it
            if (!handled && k.ch >= 32 && k.ch < 127) {
                clear_completion_menu(buf, final_row);
                buf.is_completion_active = false;
                buf.is_autotab = false; // typing a char cancels autotab
                // fall through to normal key handling below
            }
            else if (handled) {
                continue; // menu handled the key, re-render next iteration
            }
        }

        // ── Handle special keys ───────────────────────────────

        // Enter — return the input text (without prompt)
        if (k == K_ENTER) {
            std::string result = buf.input();
            history.add(result);
            return result;
        }

        // Ctrl+C — clear and return empty string
        if (k == K_CTRL_C) { // Ctrl+C = ASCII 3
            buf.text.clear();
            buf.pos = 0;
            history.add("");
            return "";
        }

        // ESC — also return empty (cancel)
        if (k == K_ESC) {
            return "";
        }

        // Ctrl+V — paste from clipboard
        if (k == K_CTRL_V) {
            std::string clip = get_clipboard_text();
            if (!clip.empty()) {
                auto keys = utf8_to_keys(clip);
                for (auto it = keys.rbegin(); it != keys.rend(); ++it)
                    buf.text.insert(buf.text.begin() + static_cast<long>(buf.pos), *it);
                buf.pos += keys.size();
                buf.recompute();
            }
            continue;
        }

        // Alt+Enter — insert newline (multi-line mode)
        if (k == K_ALT_ENTER || k == K_CTRL_ENTER || k == K_SHIFT_ENTER) {
            Key knl{}; knl.code[0] = '\n'; knl.size = 1;
            buf.insert_char(knl);
            continue;
        }

        // Arrow keys — cursor movement
        if (k == K_LEFT) {
            // If there's a hint and we just consumed from it, move back into hint
            if (!buf.hint.empty() && buf.pos > 0) {
                size_t prev = buf.pos - 1;
                std::string moved_char = key_to_utf8(buf.text[prev]);
                buf.text.erase(buf.text.begin() + static_cast<long>(prev));
                buf.pos--;
                buf.hint.insert(0, moved_char);
                buf.recompute();
            }
            else {
                buf.move_left();
            }
            continue;
        }
        if (k == K_RIGHT) {
            // If there's a hint and we're at the end of text, consume from hint
            if (!buf.hint.empty() && buf.pos >= buf.text.size()) {
                size_t next = utf8_advance_col(buf.hint, 0);
                std::string consumed = buf.hint.substr(0, next);
                auto keys = utf8_to_keys(consumed);
                for (const auto& k : keys) buf.insert_char(k);
            }
            else {
                buf.move_right();
            }
            continue;
        }
        if (k == K_UP) {
            // If buffer is empty or we're at the beginning, browse history
            if (!history.is_browsing() && buf.pos > 0) {
                buf.move_up(term_width);
                continue;
            }
            if (!history.is_browsing()) {
                original_text = buf.input();
            }
            if (history.prev()) {
                const std::string* entry = history.get_current();
                if (entry) {
                    buf.text = utf8_to_keys(*entry);
                    buf.pos = buf.text.size(); // cursor at end of line
                    buf.recompute();
                }
            }
            continue;
        }
        if (k == K_DOWN) {
            if (history.is_browsing()) {
                if (history.next()) {
                    const std::string* entry = history.get_current();
                    if (entry) {
                        buf.text = utf8_to_keys(*entry);
                        buf.pos = buf.text.size();
                        buf.recompute();
                    }
                    else {
                        // Back to original text
                        buf.text = utf8_to_keys(original_text);
                        buf.pos = buf.text.size();
                        buf.recompute();
                    }
                }
            }
            continue;
        }

        // Tab — trigger completion (with autotab: auto-continue after filling)
        if (k == K_TAB) {
            buf.is_autotab = true;
            continue;
        }

        // Backspace — delete character before cursor
        if (k == K_BACKSPACE) {
            // If there's a hint, clear it first and recompute
            if (!buf.hint.empty()) {
                buf.clear_hint();
            }
            buf.backspace();
            continue;
        }

        // Delete key — delete character after cursor
        if (k == K_DELETE) {
            if (buf.pos < buf.text.size()) {
                buf.text.erase(buf.text.begin() + static_cast<long>(buf.pos));
                buf.recompute();
                continue;
            }
        }

        // Home — move to beginning of input (after prompt)
        if (k == K_HOME) {
            buf.pos = 0;
            buf.recompute();
            continue;
        }

        // End — move to end of buffer
        if (k == K_END) {
            buf.pos = buf.text.size();
            buf.recompute();
            continue;
        }

        // ── Normal character input ────────────────────────────
        if (k.ch >= 32 && k.ch < 127) {
            char c = k.code[0];

            // If there's a hint and the typed character matches, consume from hint
            if (!buf.hint.empty() && buf.hint[0] == c) {
                Key k{};
                k.code[0] = static_cast<unsigned char>(c);
                k.size = 1;
                buf.text.insert(buf.text.begin() + static_cast<long>(buf.pos), k);
                buf.pos++;
                buf.hint.erase(buf.hint.begin());
                buf.recompute();
            }
            else {
                // No hint or no match — insert normally and clear hint
                if (!buf.hint.empty()) {
                    buf.clear_hint();
                }
                Key k{};
                k.code[0] = static_cast<unsigned char>(c);
                k.size = 1;
                buf.insert_char(k);
            }

            // Auto-complete: check candidates after inserting a character
            std::string prefix = buf.prefix();
            std::string path = get_path(prefix);
            std::vector<std::string> candidates;
            build_candidates(prefix, candidates);

            if (!candidates.empty()) {
                // Always show hint (first candidate) — menu only on Tab
                std::string filename = prefix.substr(path.length());
                buf.hint = candidates[0].substr(filename.size());
                buf.hint_candidates = candidates[0];
            }

            continue;
        }

        // Unicode character (non-ASCII printable)
        if (k.size >= 2) {
            buf.insert_char(k);
            continue;
        }
    }
}

} // namespace agent
