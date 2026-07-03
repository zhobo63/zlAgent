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

std::thread*               KeyWatcher::s_thread      = nullptr;
std::atomic<bool>          KeyWatcher::s_running      = false;
InterruptCallback          KeyWatcher::s_callback     = nullptr;
std::vector<std::string>   KeyWatcher::s_keywords;

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

// ============================================================================
// Multi-line buffer — stores the input text and manages cursor position
// ============================================================================

struct LineBuffer {
    std::string text;       // full multi-line text (prompt + user input)
    size_t pos;             // byte offset of cursor (0..text.size())
    int row;                // display row of cursor (1-based)
    int col;                // display column of cursor (1-based, 1-indexed)
    std::string hint;       // completion hint text displayed in dim color
    size_t prompt_len = 0;  // length of the prompt prefix that cannot be deleted

    LineBuffer() : pos(0), row(1), col(1) {}

    /// Recompute the cursor's display position from byte offset.
    void recompute();

    /// Insert a UTF-8 character at the cursor position.
    void insert_char(const std::string& utf8_bytes);

    /// Delete the character before the cursor (backspace). Won't delete into prompt.
    bool backspace();

    /// Move cursor left by one column. Won't move past prompt boundary.
    void move_left();

    /// Move cursor right by one column.
    void move_right();

    /// Move cursor up by one row (wrap to previous line end).
    void move_up(int term_width, int prompt_width);

    /// Move cursor down by one row (wrap to next line start).
    void move_down(int term_width, int prompt_width);

    /// Get the user input text (after prompt).
    std::string input() const { return text.substr(prompt_len); }

    /// Get the user input before the cursor (excludes prompt).
    std::string prefix() const { return text.substr(prompt_len, pos - prompt_len); }

    /// Get the text after the cursor.
    std::string suffix() const { return text.substr(pos); }

    /// Total displayable length (text up to cursor + hint).
    size_t total_len() const { return pos + hint.size(); }
};

void LineBuffer::recompute(int /*prompt_width*/) {
    int term_width = term::get_width();

    row = 1;
    col = 1; // start at column 1 — prompt is already in text, counted by the loop

    for (size_t i = 0; i < pos && i < text.size(); ++i) {
        if (text[i] == '\n') {
            row++;
            col = 1;
        } else {
            ucs4_t cp;
            int n = utf8_mbtowc(&cp, reinterpret_cast<const unsigned char*>(text.data() + i),
                                static_cast<int>(text.size() - i));
            if (n <= 0) { col++; continue; }
            col += utf8_char_width(cp);
            if (col > term_width) {
                row++;
                col = 1;
            }
        }
    }
}

void LineBuffer::insert_char(const std::string& utf8_bytes) {
    text.insert(pos, utf8_bytes);
    pos += utf8_bytes.size();
    recompute();
}

bool LineBuffer::backspace() {
    if (pos <= prompt_len) return false; // don't delete into prompt

    // If cursor is right after a newline, move to end of previous line
    if (text[pos - 1] == '\n') {
        pos--;
        recompute();
        return true;
    }

    // Delete the last UTF-8 character before cursor
    size_t prev = utf8_back_col(text, pos);
    text.erase(prev, pos - prev);
    pos = prev;
    recompute();
    return true;
}

void LineBuffer::move_left() {
    if (pos > prompt_len) {
        pos = utf8_back_col(text, pos);
    }
}

void LineBuffer::move_right() {
    if (pos < text.size()) {
        pos = utf8_advance_col(text, pos);
    }
}

void LineBuffer::move_up(int term_width, int prompt_width) {
    // Find the row above and go to the corresponding column
    int line_width = term_width - 1; // available width per display line

    // Walk backwards to find the start of current display line
    size_t line_start = pos;
    for (size_t i = pos; i > 0; --i) {
        if (text[i - 1] == '\n') break;
        line_start = i - 1;
    }

    // Walk backwards to find the start of previous display line
    size_t prev_line_start = line_start;
    for (size_t i = line_start; i > 0; --i) {
        if (text[i - 1] == '\n') break;
        prev_line_start = i - 1;
    }

    // Calculate target column within the previous display line
    int offset_in_current = static_cast<int>(pos - line_start);
    size_t target_pos = prev_line_start + std::min(offset_in_current,
                        static_cast<int>(text.size() - prev_line_start));

    pos = target_pos;
}

void LineBuffer::move_down(int term_width, int prompt_width) {
    // Walk forward to find the end of current display line
    size_t line_end = pos;
    for (size_t i = pos; i < text.size(); ++i) {
        if (text[i] == '\n') break;
        line_end = i + 1;
    }

    // Walk forward to find the end of next display line
    size_t next_line_end = line_end;
    for (size_t i = line_end; i < text.size(); ++i) {
        if (text[i] == '\n') break;
        next_line_end = i + 1;
    }

    int offset_in_current = static_cast<int>(pos - utf8_back_col(text, pos));
    size_t target_pos = line_end + std::min(offset_in_current,
                        static_cast<int>(text.size() - line_end));

    pos = target_pos;
}

// ============================================================================
// History — stores previous inputs with deduplication
// ============================================================================

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
};

void History::add(const std::string& entry) {
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

bool History::prev() {
    if (current_idx < 0 || current_idx >= static_cast<int>(entries.size()) - 1) {
        if (current_idx < 0) current_idx = 0;
        else return false;
        return true;
    }
    current_idx++;
    return true;
}

bool History::next() {
    if (current_idx <= 0) {
        current_idx = -1;
        return true;
    }
    current_idx--;
    return true;
}

const std::string* History::get_current() const {
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
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(str[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
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
    size_t last_sep = std::string::npos;
#ifdef _WIN32
    for (int i = 0; i < 2 && static_cast<size_t>(i) < prefix.size(); ++i) {
        char sep = (i == 0 ? '/' : '\\');
        auto pos = prefix.rfind(sep);
        if (pos != std::string::npos && pos > last_sep) {
            last_sep = pos;
        }
    }
#else
    last_sep = prefix.rfind('/');
#endif

    if (last_sep != std::string::npos) {
        // Path-aware: scan the directory before the separator
        std::filesystem::path dir_path(prefix.substr(0, last_sep));
        if (!dir_path.is_absolute()) {
            dir_path = std::filesystem::current_path() / dir_path;
        }
        std::string after_sep = prefix.substr(last_sep + 1);

        scan_directory(dir_path, candidates);

        auto it = std::remove_if(candidates.begin(), candidates.end(),
            [&after_sep](const std::string& e) {
                return !ci_starts_with(e, after_sep);
            });
        candidates.erase(it, candidates.end());
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

// ── Key codes ───────────────────────────────────────────────

enum Key {
    K_ESC       = -256,
    K_UP        = -257,
    K_DOWN      = -258,
    K_LEFT      = -259,
    K_RIGHT     = -260,
    K_TAB       = -261,
    K_ENTER     = -262,
    K_BACKSPACE = -263,
    K_DELETE    = -264,
    K_PGUP      = -265,
    K_PGDOWN    = -266,
    K_HOME      = -267,
    K_END       = -268,
    K_CTRL_V    = -269,   // Ctrl+V (paste)
    K_ALT_ENTER = -270,   // Alt+Enter (insert newline)
};

/// Read a single key press, handling escape sequences for arrow keys etc.
static Key read_key() {
#ifdef _WIN32
    INPUT_RECORD rec;
    DWORD count;
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);

    while (true) {
        if (!PeekConsoleInput(hIn, &rec, 1, &count)) return K_ESC;
        if (count == 0) continue;

        ReadConsoleInput(hIn, &rec, 1, &count);

        if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown) {
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

            // Ctrl+C detection (already handled by ASCII 3, but be explicit)
            if (rec.Event.KeyEvent.wVirtualKeyCode == 'C' &&
                (rec.Event.KeyEvent.dwControlKeyState & LEFT_CTRL_PRESSED ||
                 rec.Event.KeyEvent.dwControlKeyState & RIGHT_CTRL_PRESSED)) {
                return static_cast<Key>(3); // Ctrl+C = ASCII 3
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

            // Normal character
            if (rec.Event.KeyEvent.uChar.AsciiChar)
                return static_cast<Key>(rec.Event.KeyEvent.uChar.AsciiChar);
        }
    }
#else
    unsigned char buf[16];
    size_t n = 0;

    if (read(STDIN_FILENO, &buf[n], 1) != 1) return K_ESC;
    n++;

    // Ctrl+V = ASCII 22
    if (buf[0] == 22) return K_CTRL_V;

    if (buf[0] == '\t')   return K_TAB;
    if (buf[0] == '\r')   return K_ENTER;
    if (buf[0] == '\n')   return K_ENTER;
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
        return static_cast<Key>(buf[0]);
    }

    // UTF-8 multi-byte: read continuation bytes
    unsigned char c = buf[0];
    size_t expected;
    if ((c & 0xE0) == 0xC0) expected = 2;
    else if ((c & 0xF0) == 0xE0) expected = 3;
    else if ((c & 0xF8) == 0xF0) expected = 4;
    else return K_ESC;

    while (n < expected && read(STDIN_FILENO, &buf[n], 1) == 1) n++;

    return static_cast<Key>(buf[0]);
#endif
}

/// Render the completion menu. Returns the selected candidate index, or -1 if cancelled.
static int show_completion_menu(const std::vector<std::string>& candidates,
                                const char* prompt_text) {
    int term_width = term::get_width();
    int prompt_len = static_cast<int>(strlen(prompt_text));

    // Determine column layout based on terminal width and candidate widths
    size_t max_displayed = std::min(candidates.size(), static_cast<size_t>(9));
    int cols = 1;
    int per_col = static_cast<int>(max_displayed);

    if (max_displayed > 3) {
        // Try 3 columns
        size_t col_width = 0;
        for (size_t i = 0; i < std::min(max_displayed, size_t(9)); ++i) {
            int w = utf8_str_width(candidates[i]) + 4; // "1. " prefix
            if (w > col_width) col_width = w;
        }
        if ((col_width * 3 + 6) < term_width) {
            cols = 3;
            per_col = 3;
        } else if ((col_width * 2 + 4) < term_width) {
            cols = 2;
            per_col = (max_displayed <= 6 ? 3 : 4);
        }
    }

    int selected = 0;
    size_t page_offset = 0; // for pagination with PgUp/PgDown

    while (true) {
        // Clear the menu area and redraw
        term::save_cursor();

        // Print candidates in columns
        for (int row = 0; row < per_col; ++row) {
            if (row > 0) printf("\n");

            for (int col = 0; col < cols; ++col) {
                size_t idx = static_cast<size_t>(row + col * per_col);
                if (idx >= max_displayed) break;

                int global_idx = static_cast<int>(page_offset + idx);
                bool is_selected = (global_idx == selected);

                // Print "1. candidate" format
                printf("%s%2d. ", is_selected ? "\x1b[1m→" : "  ", global_idx + 1);
                if (is_selected) {
                    term::set_color(0); // reset bold, but keep arrow
                }

                printf("%s", candidates[idx].c_str());
                term::set_color(0); // reset

                if (col < cols - 1 && (row + (col + 1) * per_col) < max_displayed) {
                    printf("  ");
                }
            }
        }

        // Show pagination hint if there are more candidates
        if (candidates.size() > max_displayed) {
            printf("\n[use PgUp/PgDown to browse, %zu total]", candidates.size());
        }

        term::flush();
        term::restore_cursor();

        Key k = read_key();

        // Direct selection: 1-9
        if (k >= '1' && k <= '9') {
            int choice = k - '0';
            if (choice > 0 && static_cast<size_t>(choice) <= candidates.size()) {
                return choice - 1;
            }
        }

        // Navigation
        if (k == K_DOWN || k == K_RIGHT) {
            selected++;
            if (selected >= static_cast<int>(candidates.size())) selected = 0;
            continue;
        }
        if (k == K_UP || k == K_LEFT) {
            selected--;
            if (selected < 0) selected = static_cast<int>(candidates.size()) - 1;
            continue;
        }

        // Pagination
        if (k == K_PGDOWN && page_offset + max_displayed < candidates.size()) {
            page_offset += max_displayed;
            selected = static_cast<int>(page_offset);
            continue;
        }
        if (k == K_PGUP && page_offset > 0) {
            page_offset -= max_displayed;
            selected = static_cast<int>(page_offset);
            continue;
        }

        // Confirm selection
        if (k == K_ENTER || k == K_TAB) {
            return selected;
        }

        // Cancel
        if (k == K_ESC || k == K_DELETE) {
            return -1;
        }
    }
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
    History history;

    // Include the prompt in the buffer so cursor position is relative to the full text
    buf.text = prompt_text;
    buf.pos = static_cast<size_t>(prompt_len);
    buf.prompt_len = static_cast<size_t>(prompt_len);

    // State for tracking whether we're browsing history
    std::string original_text;
    bool was_browsing = false;

    while (true) {
        // ── Render the current line(s) ────────────────────────
        term::save_cursor();

        // Print everything up to cursor position
        for (size_t i = 0; i < buf.pos && i < buf.text.size(); ++i) {
            if (buf.text[i] == '\n') {
                printf("\n");
            } else {
                putchar(buf.text[i]);
            }
        }

        // Print hint in dim color
        if (!buf.hint.empty()) {
            term::set_color(2); // dim
            for (size_t i = 0; i < buf.hint.size(); ++i) {
                if (buf.hint[i] == '\n') {
                    printf("\n");
                } else {
                    putchar(buf.hint[i]);
                }
            }
        }

        term::set_color(0); // reset color
        term::flush();
        buf.recompute();
        term::move_cursor(buf.row, buf.col);
        term::restore_cursor();

        // ── Read a key ────────────────────────────────────────
        Key k = read_key();

        // Call the callback for every key press (extra notification)
        if (cb && k > 0) {
            cb(static_cast<int>(k));
        }

        // ── Handle special keys ───────────────────────────────

        // Enter — return the input text (without prompt)
        if (k == K_ENTER) {
            std::string result = buf.text.substr(prompt_len);
            history.add(result);
            return result;
        }

        // Ctrl+C — clear and return empty string
        if (k == 3) { // Ctrl+C = ASCII 3
            buf.text = prompt_text; // restore prompt only
            buf.pos = static_cast<size_t>(prompt_len);
            history.add("");
            return "";
        }

        // ESC — also return empty (cancel)
        if (k == K_ESC) {
            return "";
        }

        // Ctrl+V — paste from clipboard
        if (k == K_CTRL_V) {
#ifdef _WIN32
            std::string clip = get_clipboard_text();
            if (!clip.empty()) {
                buf.insert_char(clip, prompt_len);
            }
#else
            // Not supported on POSIX in v1
#endif
            continue;
        }

        // Alt+Enter — insert newline (multi-line mode)
        if (k == K_ALT_ENTER) {
            std::string nl = "\n";
            buf.insert_char(nl, prompt_len);
            continue;
        }

        // Arrow keys — cursor movement
        if (k == K_LEFT) {
            // If there's a hint and we just consumed from it, move back into hint
            if (!buf.hint.empty() && buf.pos > buf.prompt_len) {
                size_t prev = utf8_back_col(buf.text, buf.pos);
                std::string moved_char = buf.text.substr(prev, buf.pos - prev);
                buf.text.erase(prev, buf.pos - prev);
                buf.hint.insert(0, moved_char);
                buf.pos = prev;
                buf.recompute();
            } else {
                buf.move_left();
            }
            continue;
        }
        if (k == K_RIGHT) {
            // If there's a hint and we're at the end of text, consume from hint
            if (!buf.hint.empty() && buf.pos >= buf.text.size()) {
                size_t next = utf8_advance_col(buf.hint, 0);
                std::string consumed = buf.hint.substr(0, next);
                buf.text.insert(buf.pos, consumed);
                buf.pos += consumed.size();
                buf.hint.erase(0, consumed.size());
                buf.recompute();
            } else {
                buf.move_right();
            }
            continue;
        }
        if (k == K_UP)    {
            // If buffer is empty or we're at the beginning, browse history
            if (!history.is_browsing() && buf.pos > static_cast<size_t>(prompt_len)) {
                buf.move_up(term_width, prompt_len);
                continue;
            }
            if (!history.is_browsing()) {
                original_text = buf.text.substr(prompt_len);
            }
            if (history.prev()) {
                const std::string* entry = history.get_current();
                if (entry) {
                    buf.text = prompt_text + *entry;
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
                        buf.text = prompt_text + *entry;
                        buf.pos = buf.text.size();
                        buf.recompute();
                    } else {
                        // Back to original text
                        buf.text = prompt_text + original_text;
                        buf.pos = buf.text.size();
                        buf.recompute();
                    }
                }
            }
            continue;
        }

        // Tab — trigger completion
        if (k == K_TAB) {
            std::string prefix = buf.prefix();

            // If there's a hint and it matches the current input, confirm it
            if (!buf.hint.empty()) {
                buf.text.insert(buf.pos, buf.hint);
                buf.pos += buf.hint.size();
                buf.hint.clear();
                buf.recompute();
                continue;
            }

            std::vector<std::string> candidates;
            build_candidates(prefix, candidates);

            if (candidates.empty()) {
                continue; // 0 candidates: do nothing
            }

            if (candidates.size() == 1) {
                // 1 candidate: show hint (don't fill yet — wait for Tab or matching input)
                buf.hint = candidates[0].substr(prefix.size());
                continue;
            }

            // Multiple candidates: apply longest common prefix first
            std::string lcp = longest_common_prefix(candidates);
            if (!lcp.empty() && lcp != prefix) {
                buf.text.insert(buf.pos, lcp.substr(prefix.size()));
                buf.pos += lcp.size() - prefix.size();
                buf.recompute();

                // Rebuild candidates with the new prefix
                prefix = lcp;
                candidates.clear();
                build_candidates(lcp, candidates);
            }

            if (candidates.size() == 1) {
                // After LCP, only 1 candidate left: show hint
                buf.hint = candidates[0].substr(prefix.size());
            } else if (candidates.size() >= 2) {
                int chosen = show_completion_menu(candidates, prompt_text);
                if (chosen >= 0) {
                    buf.text.insert(buf.pos, candidates[chosen].substr(prefix.size()));
                    buf.pos += candidates[chosen].size() - prefix.size();
                    buf.recompute();
                }
            }
            continue;
        }

        // Backspace — delete character before cursor
        if (k == K_BACKSPACE) {
            // If there's a hint, clear it first and recompute
            if (!buf.hint.empty()) {
                buf.hint.clear();
            }
            buf.backspace(prompt_len);
            continue;
        }

        // Delete key — delete character after cursor
        if (k == K_DELETE) {
            if (buf.pos < buf.text.size()) {
                size_t next = utf8_advance_col(buf.text, buf.pos);
                buf.text.erase(buf.pos, next - buf.pos);
                continue;
            }
        }

        // Home — move to beginning of input (after prompt)
        if (k == K_HOME) {
            buf.pos = static_cast<size_t>(prompt_len);
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
        if (k >= 32 && k < 127) {
            char c = static_cast<char>(k);

            // If there's a hint and the typed character matches, consume from hint
            if (!buf.hint.empty() && buf.hint[0] == c) {
                buf.text.insert(buf.pos, 1, c);
                buf.pos++;
                buf.hint.erase(buf.hint.begin());
                buf.recompute();
                continue;
            }

            // No hint or no match — insert normally and clear hint
            if (!buf.hint.empty()) {
                buf.hint.clear();
            }
            std::string s(1, c);
            buf.insert_char(s, prompt_len);
            continue;
        }

        // UTF-8 multi-byte character (non-ASCII printable)
        if (k >= 127) {
            unsigned char byte = static_cast<unsigned char>(k);
            size_t expected;
            if ((byte & 0xE0) == 0xC0) expected = 2;
            else if ((byte & 0xF0) == 0xE0) expected = 3;
            else if ((byte & 0xF8) == 0xF0) expected = 4;
            else continue;

            std::string utf8_bytes(1, static_cast<char>(byte));
            while (utf8_bytes.size() < expected) {
                Key next = read_key();
                if (next < 0 || next > 255) break;
                utf8_bytes += static_cast<char>(next);
            }

            buf.insert_char(utf8_bytes, prompt_len);
            continue;
        }
    }
}

} // namespace agent
