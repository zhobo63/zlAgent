#include "pch.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <set>
#include <sstream>
#include <stack>

#include "key_watcher.h"
#include "tui.h"
#include "logger.h"

namespace agent {


Key Key::K_ZERO(0, 0);
Key Key::K_ESC(27, -1);
Key Key::K_UP(38, -2);
Key Key::K_DOWN(40, -3);
Key Key::K_LEFT(37, -4);
Key Key::K_RIGHT(39, -5);
Key Key::K_TAB(9, -6);
Key Key::K_ENTER(13, -7);
Key Key::K_BACKSPACE(8, -8);
Key Key::K_DELETE(46, -9);
Key Key::K_PGUP(33, -10);
Key Key::K_PGDOWN(34, -11);
Key Key::K_HOME(36, -12);
Key Key::K_END(35, -13);
Key Key::K_CTRL_V(22, -14);   // Ctrl+V (paste)
Key Key::K_ALT_ENTER(13, -15);   // Alt+Enter (insert newline)
Key Key::K_CTRL_C(3, -16);   // Ctrl+C (interrupt)
Key Key::K_CTRL_ENTER(13, -17);    // Ctrl+Enter (insert \n + enable line display)
Key Key::K_SHIFT_ENTER(13, -18);    // Shift+Enter (insert \n + enable line display)
Key Key::K_SPACE(32, 1);

// ============================================================================
// Cross-platform keyboard helpers (unchanged from original)
// ============================================================================

#ifdef _WIN32
#include <conio.h>
#include <windows.h>

#define getch _getch
#define kbhit _kbhit

static DWORD s_console_mode = 0;

void KeyWatcher::init_keyboard() {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (GetConsoleMode(hIn, &s_console_mode)) {
        // Disable ENABLE_PROCESSED_INPUT so Ctrl+C is delivered as a normal
        // KEY_EVENT instead of throwing an SEH exception (0x40010005).
        // s_console_mode &= ~(ENABLE_PROCESSED_INPUT);
        // s_console_mode &= ~(ENABLE_QUICK_EDIT_MODE | ENABLE_EXTENDED_FLAGS);
        DWORD console_mode = ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT;
        SetConsoleMode(hIn, console_mode);
    }
}
void KeyWatcher::close_keyboard() {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (s_console_mode) {
        SetConsoleMode(hIn, s_console_mode);
        s_console_mode = 0;
    }
}

#else
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

static struct termios oldt, newt;

void KeyWatcher::init_keyboard() {
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
}

void KeyWatcher::close_keyboard() {
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
KeyCallback KeyWatcher::s_callback     = nullptr;
std::vector<std::string>    KeyWatcher::s_keywords;
KeyWatcher::History         KeyWatcher::history;

// ============================================================================
// Original API (unchanged)
// ============================================================================

void send_enter() {
#ifdef _WIN32
    // Inject an Enter key into the console input buffer so that read_key()
    // returns immediately, allowing the thread to check s_running and exit.
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    INPUT_RECORD rec{};
    rec.EventType = KEY_EVENT;
    rec.Event.KeyEvent.bKeyDown = TRUE;
    rec.Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
    rec.Event.KeyEvent.uChar.AsciiChar = '\r';
    DWORD written;
    WriteConsoleInputW(hIn, &rec, 1, &written);
#endif
}

void KeyWatcher::on_key(KeyCallback cb) {
    s_callback = std::move(cb);
}

void KeyWatcher::start() {
    if (s_running.load()) return;
    s_running.store(true);

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

	//send_enter();  // wake up the thread if it's blocked on read_key(
    if (s_thread) {
        s_thread->join();
        delete s_thread;
        s_thread = nullptr;
    }
}

// ============================================================================
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

// Helper: convert a code point to Key (UTF-8 encoded)
Key Key::from_codepoint(ucs4_t cp) {
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
std::vector<Key> KeyWatcher::utf8_to_keys(const std::string& s) {
	std::vector<Key> keys;
	const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data());
	size_t len = s.size();
	while (len > 0) {
		ucs4_t cp;
		int n = utf8_mbtowc(&cp, p, static_cast<int>(len));
		if (n <= 0) break;
		keys.push_back(Key::from_codepoint(cp));
		p += n; len -= n;
	}
	return keys;
}

// ============================================================================
// Multi-line buffer — stores the input text and manages cursor position
// ============================================================================

void KeyWatcher::LineBuffer::recompute() {
    int term_width = TUI::getTerminalWidth();

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

void KeyWatcher::LineBuffer::insert_char(const Key& k) {
    text.insert(text.begin() + static_cast<long>(pos), k);
    pos++;
    recompute();
}

bool KeyWatcher::LineBuffer::backspace() {
    if (pos == 0) return false; // don't delete into prompt

    // Delete the character before cursor
    text.erase(text.begin() + static_cast<long>(pos - 1));
    pos--;
    recompute();
    return true;
}

void KeyWatcher::LineBuffer::move_left() {
    if (pos > 0) pos--;
}

void KeyWatcher::LineBuffer::move_right() {
    if (pos < text.size()) pos++;
}

void KeyWatcher::LineBuffer::move_up(int term_width) {
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

void KeyWatcher::LineBuffer::move_down(int term_width) {
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

std::string KeyWatcher::LineBuffer::input() const {
    std::string result;
    for (const auto& k : text) {
        if (k.size > 0 && k.size <= 4)
            result.append(reinterpret_cast<const char*>(k.code), static_cast<size_t>(k.size));
    }
    return result;
}

std::string KeyWatcher::LineBuffer::get_prefix() const {
    int start = prefix_start();

    std::string result;
    for (size_t i = start; i < pos && i < text.size(); ++i) {
        if (text[i].size > 0 && text[i].size <= 4)
            result.append(reinterpret_cast<const char*>(text[i].code), static_cast<size_t>(text[i].size));
    }
    return result;
}

std::string KeyWatcher::LineBuffer::suffix() const {
    std::string result;
    for (size_t i = pos; i < text.size(); ++i) {
        if (text[i].size > 0 && text[i].size <= 4)
            result.append(reinterpret_cast<const char*>(text[i].code), static_cast<size_t>(text[i].size));
    }
    return result;
}

void KeyWatcher::LineBuffer::print_hint() {
	TUI::setAnsiCode(2); // dim
    for (size_t i = 0; i < hint.size(); ++i) {
        if (hint[i] == '\n') {
            printf("\n");
        }
        else {
            putchar(hint[i]);
        }
    }
}

int KeyWatcher::LineBuffer::prefix_start() const {
    int prefix_start = pos;
    for (int i = static_cast<int>(pos) - 1; i >= 0; i--) {
        auto& k = text[i];
        if (k.ch == ' ' || k.ch == '@') break;
        prefix_start = i;
    }
    return prefix_start;
}

void KeyWatcher::LineBuffer::apply_hint() {
    auto hint_keys = utf8_to_keys(hint);
    auto keys = utf8_to_keys(hint_candidates);
    int trim_count = static_cast<int>(keys.size()) - static_cast<int>(hint_keys.size());
    resize(text.size() - trim_count);
    insert(keys);
    hint.clear();
    hint_candidates.clear();
}

void KeyWatcher::LineBuffer::clear_hint() {
    hint.clear();
    hint_candidates.clear();
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
// Completion helpers
// ============================================================================

bool KeyWatcher::ci_starts_with(const std::string& str, const std::string& prefix) {
	if (str.size() < prefix.size()) return false;
	for (size_t i = 0; i < prefix.size(); ++i)
		if (std::tolower(static_cast<unsigned char>(str[i])) !=
			std::tolower(static_cast<unsigned char>(prefix[i])))
			return false;
	return true;
}

std::string KeyWatcher::normalize_path(const std::string& path) {
	std::string result = path;
	for (auto& c : result)
		if (c == '\\')
			c = '/';
	return result;
}

std::string KeyWatcher::get_path(const std::string& path) {
	std::string norm = normalize_path(path);
	if (!norm.empty() && norm.back() == '/')
		return norm;
	size_t last_sep = norm.rfind('/');
	if (last_sep != std::string::npos)
		return norm.substr(0, last_sep + 1);
	return "";
}

void KeyWatcher::scan_directory(const std::filesystem::path& dir, std::vector<std::string>& entries) {
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

void KeyWatcher::build_candidates(const std::string& prefix, std::vector<std::string>& candidates) {
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
		for (const auto& kw : s_keywords) {
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
        //if (!PeekConsoleInput(hIn, &rec, 1, &count)) return K_ESC;
        //if (count == 0) continue;

        ReadConsoleInputW(hIn, &rec, 1, &count);

        if (rec.EventType == MENU_EVENT) {
            LOG_DEBUG("ReadConsoleInputW", "Menu event detected:" + std::to_string(rec.Event.MenuEvent.dwCommandId));
        }

        if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown) {
            // ESC detection
            if (rec.Event.KeyEvent.wVirtualKeyCode == 27) {
                return Key::K_ESC;
            }
            // Ctrl+V detection
            if (rec.Event.KeyEvent.wVirtualKeyCode == 'V' &&
                (rec.Event.KeyEvent.dwControlKeyState & LEFT_CTRL_PRESSED ||
                 rec.Event.KeyEvent.dwControlKeyState & RIGHT_CTRL_PRESSED)) {
                return Key::K_CTRL_V;
            }

            // Alt+Enter detection
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_RETURN &&
                (rec.Event.KeyEvent.dwControlKeyState & LEFT_ALT_PRESSED ||
                 rec.Event.KeyEvent.dwControlKeyState & RIGHT_ALT_PRESSED)) {
                return Key::K_ALT_ENTER;
            }

            // Ctrl+Enter — insert newline + enable line display mode
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_RETURN &&
                (rec.Event.KeyEvent.dwControlKeyState & LEFT_CTRL_PRESSED ||
                 rec.Event.KeyEvent.dwControlKeyState & RIGHT_CTRL_PRESSED)) {
                return Key::K_CTRL_ENTER;
            }

            // Shift+Enter — insert newline + enable line display mode
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_RETURN &&
                (rec.Event.KeyEvent.dwControlKeyState & SHIFT_PRESSED)) {
                return Key::K_SHIFT_ENTER;
            }

            // Ctrl+C detection (already handled by ASCII 3, but be explicit)
            if (rec.Event.KeyEvent.wVirtualKeyCode == 'C' &&
                (rec.Event.KeyEvent.dwControlKeyState & LEFT_CTRL_PRESSED ||
                 rec.Event.KeyEvent.dwControlKeyState & RIGHT_CTRL_PRESSED)) {
                return Key::K_CTRL_C;
            }

            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_UP)    return Key::K_UP;
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_DOWN)  return Key::K_DOWN;
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_LEFT)  return Key::K_LEFT;
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_RIGHT) return Key::K_RIGHT;
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_TAB)   return Key::K_TAB;
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_RETURN)return Key::K_ENTER;
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_BACK)  return Key::K_BACKSPACE;
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_DELETE && rec.Event.KeyEvent.uChar.AsciiChar == 0) return Key::K_DELETE;
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_PRIOR) return Key::K_PGUP;
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_NEXT)  return Key::K_PGDOWN;
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_HOME)  return Key::K_HOME;
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_END)   return Key::K_END;

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

    if (read(STDIN_FILENO, &buf[n], 1) != 1) return Key::K_ESC;
    n++;

    // Ctrl+V = ASCII 22
    if (buf[0] == 22) return Key::K_CTRL_V;
    if (buf[0] == 3) return Key::K_CTRL_C;

    if (buf[0] == '\t')   return Key::K_TAB;
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
            return Key::K_CTRL_ENTER;
        }
        return Key::K_ENTER;
    }
    if (buf[0] == 127)    return Key::K_BACKSPACE; // DEL key on Linux
    if (buf[0] == 8)      return Key::K_BACKSPACE; // Ctrl+H = backspace

    if (buf[0] == 27) {   // ESC sequence or Alt+
        if (read(STDIN_FILENO, &buf[n], 1) != 1) return Key::K_ESC;
        n++;

        if (buf[1] == '[') {
            if (read(STDIN_FILENO, &buf[n], 1) != 1) return Key::K_ESC;
            n++;

            switch (buf[2]) {
                case 'A': return Key::K_UP;
                case 'B': return Key::K_DOWN;
                case 'C': return Key::K_RIGHT;
                case 'D': return Key::K_LEFT;
                case 'F': return Key::K_END;
                case 'H': return Key::K_HOME;
                case '~':
                    if (read(STDIN_FILENO, &buf[n], 1) != 1) return Key::K_ESC;
                    n++;
                    switch (buf[3]) {
                        case '2': return Key::K_DELETE;
                        case '5': return Key::K_PGUP;
                        case '6': return Key::K_PGDOWN;
                        default:  return Key::K_ESC;
                    }
                default: return Key::K_ESC;
            }
        } else if (buf[1] == 'O') {
            if (read(STDIN_FILENO, &buf[n], 1) != 1) return Key::K_ESC;
            n++;
            switch (buf[2]) {
                case 'F': return Key::K_END;
                case 'H': return Key::K_HOME;
                case 'P': return Key::K_PGUP;
                case 'Q': return Key::K_PGDOWN;
                default:  return Key::K_ESC;
            }
        } else if (buf[1] == '\r') {
            // Alt+Enter on some terminals
            return Key::K_ALT_ENTER;
        }
        return Key::K_ESC;
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
    else return Key::K_ESC;

    while (n < expected && read(STDIN_FILENO, &buf[n], 1) == 1) n++;

    Key k{};
    for (size_t i = 0; i < n; ++i) k.code[i] = buf[i];
    k.size = static_cast<int>(n);
    return k;
#endif
}

static constexpr size_t MAX_DISPLAYED = 9;



void KeyWatcher::LineBuffer::insert_completion(const std::string& completion) {
    int start = prefix_start();
	std::string prefix = get_prefix();
	std::string path = KeyWatcher::get_path(prefix);
    resize(start + path.length());
	auto keys = utf8_to_keys(completion);
	for (const auto& k : keys) insert_char(k);
    clear_hint();
    is_completion_active = false;
}

/// Draw the completion menu below the input line.
void KeyWatcher::LineBuffer::draw_completion_menu(int current_input_row) {
    size_t max_displayed = std::min(candidates.size(), MAX_DISPLAYED);
    printf("\n");
    for (size_t i = 0; i < max_displayed; ++i) {
        size_t idx = page_offset + i;
        bool is_selected = (static_cast<int>(idx - page_offset) == selected);
        printf("\x1b[32m%zu\x1b[0m ", idx + 1);
        	printf("\x1b[%dm%s\x1b[0m", is_selected ? 37 : 2, candidates[idx].c_str());
        	TUI::setAnsiCode(0);
        	printf("\n");
    }

    if (candidates.size() > MAX_DISPLAYED) {
        bool can_go_up = (page_offset > 0);
        bool can_go_down = (page_offset + MAX_DISPLAYED < candidates.size());
        printf("\x1b[2m");
        printf("candidates %zu-%zu of %zu ", page_offset + 1,
            std::min(page_offset + MAX_DISPLAYED, candidates.size()),
            candidates.size());
        if (can_go_up)
            printf("[PgUp] ");
        if (can_go_down)
            printf("[PgDn]");
        TUI::setAnsiCode(0);
        printf("\n");
    }
    TUI::flush();
}

/// Clear the completion menu lines and restore cursor.
void KeyWatcher::LineBuffer::clear_completion_menu(int current_input_row) {
    size_t max_displayed = std::min(candidates.size(), MAX_DISPLAYED);
    size_t total_menu_lines = max_displayed + (candidates.size() > MAX_DISPLAYED ? 1 : 0);
    if (candidates.size() > MAX_DISPLAYED)
        total_menu_lines++;

    for (size_t i = 0; i < total_menu_lines; ++i) {
	TUI::setCursorPos(current_input_row + 1 + static_cast<int>(i), 1);
	TUI::clearLine();
    }
    printf("\x1b[0m");
    TUI::setCursorPos(current_input_row, input_col);
    TUI::flush();
}

int KeyWatcher::LineBuffer::show_completion_menu(std::vector<std::string> &_candidates)
{
    candidates = std::move(_candidates);
    selected = 0;
    page_offset = 0;
    is_completion_active = true;

    int H = TUI::getTerminalHeight();
    size_t max_displayed = std::min(candidates.size(), MAX_DISPLAYED);
    size_t total_menu_lines = max_displayed + (candidates.size() > MAX_DISPLAYED ? 2 : 1);
    if (candidates.size() > MAX_DISPLAYED)
        total_menu_lines++;
    	auto pos_before = TUI::getCursorPos();
    int scroll_amount = std::max(0, static_cast<int>(pos_before.row + total_menu_lines - H));
    for (int i = 0; i < scroll_amount; i++) {
        printf("\n");
    }
    	auto pos = TUI::getCursorPos();
    	pos.col = pos_before.col;
    TUI::flush();
    input_col = pos.col;
    if (scroll_amount > 0) {
        pos.row = std::max(1, pos_before.row - scroll_amount);
    }
    return pos.row;
}

void KeyWatcher::LineBuffer::hide_completion_menu(int current_input_row)
{
    is_completion_active = false;
    clear_completion_menu(current_input_row);
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

std::string KeyWatcher::readline(const char* prompt, ReadlineCallback cb) {
    const char* prompt_text = (prompt ? prompt : "");
    int prompt_len = static_cast<int>(strlen(prompt_text));
	int term_width = TUI::getTerminalWidth();

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
    	auto cursor_pos = TUI::getCursorPos();

    while (true) {
        // ── Render the current line(s) ────────────────────────
        	TUI::setCursorPos(cursor_pos.row, 1);
        	TUI::clearLine();
        	std::cout << buf.display_text();

        // Print hint in dim color
        if (!buf.hint.empty()) {
            buf.print_hint();
        }

        	TUI::setAnsiCode(0); // reset color
        	TUI::flush();
        buf.recompute();
        // Add offset: prompt starts at start_row, not row 1
        	int final_row = buf.row + cursor_pos.row - 1;
        	TUI::setCursorPos(final_row, buf.col);

        // ── Completion menu rendering ────────────────────────
        if (buf.is_completion_active) {
            		buf.draw_completion_menu(final_row);
            	TUI::setCursorPos(final_row, buf.input_col);
            	TUI::flush();
        }

        // ── Read a key ────────────────────────────────────────
        Key k = read_key();

        // Call the callback for every key press (extra notification)
        if (cb) {
            cb(k);
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
                    buf.insert_completion(buf.candidates[choice - 1]);
                    // Clear menu
                    buf.clear_completion_menu(final_row);
                    handled = true;
                }
            }

            if (!handled && (k == Key::K_DOWN || k == Key::K_RIGHT)) {
                buf.selected++;
                if (buf.selected >= static_cast<int>(max_displayed)) buf.selected = 0;
                handled = true;
            }
            else if (!handled && (k == Key::K_UP || k == Key::K_LEFT)) {
                buf.selected--;
                if (buf.selected < 0) buf.selected = static_cast<int>(max_displayed) - 1;
                handled = true;
            }

            // Page navigation
            if (!handled && k == Key::K_PGDOWN && buf.candidates.size() > max_displayed) {
                buf.page_offset += max_displayed;
                if (buf.page_offset + max_displayed > buf.candidates.size())
                    buf.page_offset = buf.candidates.size() - max_displayed;
                buf.selected = 0;
                handled = true;
            }
            else if (!handled && k == Key::K_PGUP && buf.candidates.size() > max_displayed) {
                if (buf.page_offset >= max_displayed)
                    buf.page_offset -= max_displayed;
                else
                    buf.page_offset = 0;
                buf.selected = static_cast<int>(max_displayed) - 1;
                handled = true;
            }

            // Confirm selection
            if (!handled && (k == Key::K_ENTER || k == Key::K_TAB)) {
                int chosen = static_cast<int>(buf.page_offset + buf.selected);
                buf.insert_completion(buf.candidates[chosen]);
                buf.hide_completion_menu(final_row);
                handled = true;
            }

            // Cancel
            if (!handled && (k == Key::K_ESC || k == Key::K_DELETE)) {
                buf.hide_completion_menu(final_row);
                handled = true;
            }

            // Normal character input — exit menu and let main loop handle it
            if (!handled && k.ch >= 32 && k.ch < 127) {
                // fall through to normal key handling below
            }
            else if (handled) {
                continue; // menu handled the key, re-render next iteration
            }
        }

        // ── Handle special keys ───────────────────────────────

        if (k == Key::K_ENTER) {
            std::string result = buf.input();
            history.add(result);
            return result;
        }

        if (k == Key::K_CTRL_C) {
            buf.text.clear();
            buf.pos = 0;
            history.add("");
            return "";
        }

        if (k == Key::K_ESC) {
            return "";
        }

        if (k == Key::K_CTRL_V) {
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

        if (k == Key::K_ALT_ENTER || k == Key::K_CTRL_ENTER || k == Key::K_SHIFT_ENTER) {
            Key knl{}; knl.code[0] = '\n'; knl.size = 1;
            buf.insert_char(knl);
            continue;
        }

        if (k == Key::K_LEFT) {
            buf.move_left();            
            continue;
        }
        if (k == Key::K_RIGHT) {
            buf.move_right();            
            continue;
        }
        if (k == Key::K_UP) {
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
        if (k == Key::K_DOWN) {
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

        if (k == Key::K_TAB) {
            std::string prefix = buf.get_prefix();
            std::vector<std::string> candidates;
            KeyWatcher::build_candidates(prefix, candidates);

            if (!candidates.empty()) {
                if (candidates.size() == 1) {
                    buf.insert_completion(candidates[0]);
                } else {
                    // Multiple candidates — show menu
                    cursor_pos.row = buf.show_completion_menu(candidates);
                }
            }
            continue;
        }

        if (k == Key::K_BACKSPACE) {
            // If there's a hint, clear it first and recompute
            if (!buf.hint.empty()) {
                buf.clear_hint();
            }
            buf.backspace();
            buf.hide_completion_menu(cursor_pos.row);
            continue;
        }

        if (k == Key::K_DELETE) {
            if (buf.pos < buf.text.size()) {
                buf.text.erase(buf.text.begin() + static_cast<long>(buf.pos));
                buf.recompute();
            }
            continue;
        }

        if (k == Key::K_HOME) {
            buf.pos = 0;
            buf.recompute();
            continue;
        }

        if (k == Key::K_END) {
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
                        std::string prefix = buf.get_prefix();
                        std::string path = KeyWatcher::get_path(prefix);
                        std::vector<std::string> candidates;
                        KeyWatcher::build_candidates(prefix, candidates);

            if (!candidates.empty()) {
                // Always show hint (first candidate) — menu only on Tab
                std::string filename = prefix.substr(path.length());
                buf.hint = candidates[0].substr(filename.size());
                buf.hint_candidates = candidates[0];
                if (buf.is_completion_active) {
                    buf.clear_completion_menu(cursor_pos.row);
                    buf.candidates = std::move(candidates);
                    buf.page_offset = 0;
                }
            }

            continue;
        }

        // Unicode character (non-ASCII printable)
        if (k.size >= 2) {
            buf.clear_hint();
            buf.hide_completion_menu(cursor_pos.row);
            buf.insert_char(k);
            continue;
        }
    }
}

} // namespace agent
