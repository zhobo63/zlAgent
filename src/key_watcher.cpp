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
Key Key::K_SPACE(32, 1, 1);

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
        DWORD console_mode;
        // Disable ENABLE_PROCESSED_INPUT so Ctrl+C is delivered as a normal
        // KEY_EVENT instead of throwing an SEH exception (0x40010005).
        console_mode = s_console_mode &= ~(ENABLE_PROCESSED_INPUT);
        // s_console_mode &= ~(ENABLE_QUICK_EDIT_MODE | ENABLE_EXTENDED_FLAGS);
        // DWORD console_mode = ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT;
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
#include <sys/ioctl.h>

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

std::atomic<bool>           KeyWatcher::s_running      = false;
KeyCallback KeyWatcher::s_callback     = nullptr;
std::vector<std::string>    KeyWatcher::s_keywords;
KeyWatcher::History         KeyWatcher::history;

std::thread* KeyWatcher::s_read_thread = nullptr;
std::mutex KeyWatcher::s_read_mutex;
std::vector<Key> KeyWatcher::s_read_queue;

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
#else
    // Push a carriage return ('\r') into the tty input buffer so that
    // kbhit()/getch() or read_key() returns immediately.
    char c = '\r';
    ioctl(STDIN_FILENO, TIOCSTI, &c);
#endif
}

void KeyWatcher::on_key(KeyCallback cb) {
    s_callback = std::move(cb);
}

void KeyWatcher::start() {
    if (s_running.load()) return;
    s_running.store(true);

    s_read_thread = new std::thread([] {
        read_key_thread();
    });
}

void KeyWatcher::stop() {
    if (!s_running.load()) return;
    s_running.store(false);

	if (s_read_thread) {
        s_read_thread->join();
        delete s_read_thread;
        s_read_thread = nullptr;
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
	k.char_width = utf8_char_width(c);
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

static int compute_prompt_width(const std::string& prompt) {
    int col = 1;
    const unsigned char* bp = reinterpret_cast<const unsigned char*>(prompt.data());
    size_t len = prompt.size();
    while (len > 0 && *bp) {
        if (*bp >= 0x80 && *bp <= 0xBF) { bp++; len--; continue; }
        ucs4_t cp;
        int n = utf8_mbtowc(&cp, bp, static_cast<int>(len));
        if (n <= 0) { col++; bp++; len--; continue; }
        col += utf8_char_width(cp);
        bp += n;
        len -= n;
    }
    return col;
}

void KeyWatcher::LineBuffer::set_prompt(const std::string& p) {
    prompt = p;
    cached_prompt_col = compute_prompt_width(p);
}

static void cal_display_pos(const std::vector<Key> &text, int pos, int& col, int& row) {
    int term_width = TUI::getTerminalWidth();
    for (size_t i = 0; i < pos && i < text.size(); ++i) {
        if (text[i].char_width == 0 || text[i].ch == '\n') { 
            row++; col = 1; 
            continue; 
        }
        col += text[i].char_width;
        if (col > term_width) {
            row++;
            col = 1;
        }
    }
}

static int get_display_pos(const std::vector<Key>& text, 
    int prompt_col,
    int prompt_row, int col, int row)
{
    int pos = 0;
    if (row < prompt_row)
        return pos;
    int term_width = TUI::getTerminalWidth();
    int r = prompt_row;
    int c = prompt_col;
    for (size_t i = 0; i < text.size(); i++) {
        if (r >= row && c >= col)
            return i;
    
        if (text[i].char_width == 0 || text[i].ch == '\n') {
            r++; c = 1;
            continue;
        }
        c += text[i].char_width;
        if (c > term_width) {
            r++;
            c = 1;
        }
    }
    return text.size();
}

void KeyWatcher::LineBuffer::recompute() {
    int term_width = TUI::getTerminalWidth();

    // Prompt is single-line; input starts right after it on the same line.
    row = 1;
    col = cached_prompt_col;

    // Count display columns from user input up to cursor
    for (size_t i = 0; i < pos && i < text.size(); ++i) {
        if (text[i].char_width == 0 || text[i].ch == '\n') { row++; col = 1; continue; }
        col += text[i].char_width;
        if (col > term_width) {
            row++;
            col = 1;
        }
    }
    int term_height = TUI::getTerminalHeight();
    int new_line = (prompt_row + row - 1) - term_height;

    if (new_line>0) {
        prompt_row -= new_line;
        for (int i = 0; i < new_line; i++) {
            printf("\n");
        }
    }
}

void KeyWatcher::LineBuffer::insert_char(const Key& k) {
    text.insert(text.begin() + static_cast<long>(pos), k);
    pos++;
    is_display_dirty = true;
}

bool KeyWatcher::LineBuffer::backspace() {
    if (pos == 0) return false; // don't delete into prompt

    // Delete the character before cursor
    text.erase(text.begin() + static_cast<long>(pos - 1));
    pos--;
    is_display_dirty = true;
    draw_pos = pos;
    return true;
}

void KeyWatcher::LineBuffer::move_left() {
    if (pos > 0) {
        pos--;
        draw_pos = pos;
    }
}

void KeyWatcher::LineBuffer::move_right() {
    if (pos < text.size()) {
        pos++;
        draw_pos = pos;
    }
}

bool KeyWatcher::LineBuffer::move_up(int term_width) {
    auto cp = TUI::getCursorPos();
    if (cp.row <= prompt_row)
        return false;
    cp.row--;
    pos = get_display_pos(text, cached_prompt_col, prompt_row, cp.col, cp.row);
    draw_pos = pos;
    return true;
}

bool KeyWatcher::LineBuffer::move_down(int term_width) {
    auto cp = TUI::getCursorPos();
    int old = pos;
    pos = get_display_pos(text, cached_prompt_col, prompt_row, cp.col, cp.row + 1);
    draw_pos = pos;
    return old != pos;
}

void KeyWatcher::LineBuffer::move_home()
{
    if (pos < text.size() && pos>0 && text.size() > 0 && text[pos].ch == '\n')
        pos--;
    for (; pos > 0; pos--) {
        if (pos >= text.size())
            continue;
        auto& k = text[pos];
        if (k.ch == '\n') {
            pos++;
            break;
        }
    }
    draw_pos = pos;
}
void KeyWatcher::LineBuffer::move_end()
{
    for (; pos < text.size(); pos++) {
        auto& k = text[pos];
        if (k.ch == '\n')
            break;
    }
    draw_pos = pos;
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

std::string KeyWatcher::LineBuffer::display_text() const {
    std::string s = prompt;
    for (const auto& k : text) {
        s.append(reinterpret_cast<const char*>(k.code), k.size);
    }
    return s;
}

void KeyWatcher::LineBuffer::clear_prompt()
{
    // Build a single ANSI string: position → erase each line and move down → restore cursor.

    std::cout << TUI::cursor_pos(prompt_row, 1) <<
        TUI::ANSI_CLEAR_TO_END <<
        TUI::cursor_pos(prompt_row, 1) << TUI::ANSI_RESET;
}

void KeyWatcher::LineBuffer::clear()
{
    int draw_row = 1;
    int draw_col = 1;
    if (draw_pos >= 0) {
        draw_col = cached_prompt_col;
        cal_display_pos(text, draw_pos, draw_col, draw_row);
    }
    std::cout << TUI::cursor_pos(prompt_row + draw_row - 1, draw_col) <<
        TUI::ANSI_CLEAR_TO_END <<
        TUI::cursor_pos(prompt_row + draw_row - 1, draw_col) << TUI::ANSI_RESET;
}

void KeyWatcher::LineBuffer::draw()
{
    if (draw_pos < 0) {
        std::cout << prompt;
        draw_pos = 0;
    }
    std::string draw_text;
    size_t p;
    for (p = draw_pos; p < text.size(); p++) {
        auto& k = text[p];
        draw_text.append(reinterpret_cast<const char*>(k.code), k.size);
    }
    std::cout << draw_text;
    draw_pos = pos;
}

void KeyWatcher::LineBuffer::resize(size_t n) {
    text.resize(n);
    if (pos > n) {
        pos = n;
        draw_pos = n;
    }
}


void KeyWatcher::LineBuffer::set_text(const std::string& _text)
{
    text = utf8_to_keys(_text);
    pos = text.size();
    is_display_dirty = true;
    draw_pos = 0;
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
        if (k.ch == ' ' || k.ch == '@' || k.ch == '\n') break;
        prefix_start = i;
    }
    return prefix_start;
}

void KeyWatcher::LineBuffer::show_hint()
{
    std::string prefix = get_prefix();
    // Determine if this is command mode (starts with / and no second /)
    bool is_command = (prefix[0] == '/' && prefix.find('/', 1) == std::string::npos);

    std::string filename;
    if (is_command) {
        // Command mode: entire string is the "filename"
        filename = prefix;
    }
    else {
        // Path mode: extract portion after last / or from start
        std::string path = KeyWatcher::get_path(prefix);
        filename = prefix.substr(path.length());
    }
    if (!filename.empty()) {
        if (selected >= candidates.size()) {
            selected = 0;
        }
        hint = candidates[selected].substr(filename.size());
        hint_candidates = candidates[selected];
        is_display_dirty = true;
    }

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
// Completion menu — render and interact with completion options
// ============================================================================

static constexpr size_t MAX_DISPLAYED = 9;

void KeyWatcher::LineBuffer::insert_completion(const std::string& completion) {
    int start = prefix_start();
    std::string prefix = get_prefix();
    // Determine if this is command mode (starts with / and no second /)
    bool is_command = (prefix.size() > 0 && prefix[0] == '/' && prefix.find('/', 1) == std::string::npos);
    int path_len = is_command ? 0 : KeyWatcher::get_path(prefix).length();
    resize(start + path_len);
    auto keys = utf8_to_keys(completion);
    for (const auto& k : keys) insert_char(k);
    clear_hint();
    is_completion_active = false;
}

/// Draw the completion menu below the input line.
void KeyWatcher::LineBuffer::draw_completion_menu(int current_input_row) {
    size_t max_displayed = std::min(candidates.size(), MAX_DISPLAYED);

    // Build the entire menu in a single string to minimize I/O and flicker.
    std::string out;
    out += "\n";
    for (size_t i = 0; i < max_displayed; ++i) {
        size_t idx = page_offset + i;
        if (idx >= candidates.size())
            break;
        bool is_selected = (static_cast<int>(idx - page_offset) == selected);
        out += "\x1b[32m";
        out += std::to_string(i + 1);
        out += "\x1b[0m ";
        out += "\x1b[";
        out += is_selected ? "37" : "2";
        out += "m";
        out += candidates[idx];
        out += "\x1b[0m";
        if (i < max_displayed - 1)
            out += "\n";
    }

    if (candidates.size() > MAX_DISPLAYED) {
        bool can_go_up = (page_offset > 0);
        bool can_go_down = (page_offset + MAX_DISPLAYED < candidates.size());
        out += "\n\x1b[2m";
        out += "candidates ";
        out += std::to_string(page_offset + 1);
        out += "-";
        out += std::to_string(std::min(page_offset + MAX_DISPLAYED, candidates.size()));
        out += " of ";
        out += std::to_string(candidates.size());
        out += " ";
        if (can_go_up)
            out += "[PgUp] ";
        if (can_go_down)
            out += "[PgDn]";
    }

    printf("%s", out.c_str());
}

int KeyWatcher::LineBuffer::show_completion_menu(std::vector<std::string>& _candidates)
{
    candidates = std::move(_candidates);
    selected = 0;
    page_offset = 0;
    is_completion_active = true;
    is_display_dirty = true; // menu opened, need redraw

    int H = TUI::getTerminalHeight();
    size_t max_displayed = std::min(candidates.size(), MAX_DISPLAYED);
    // 1 line per candidate + 1 info line when paginated
    size_t total_menu_lines = max_displayed;
    if (candidates.size() > MAX_DISPLAYED)
        total_menu_lines++;
    auto pos_before = TUI::getCursorPos();
    int scroll_amount = std::max(0, static_cast<int>(pos_before.row + total_menu_lines - H));
    // Build a single string with all newlines instead of N printf calls
    if (scroll_amount > 0) {
        TUI::setCursorPos(H, 1);
        prompt_row -= scroll_amount;
        std::string cmd(scroll_amount, '\n');
        std::cout << cmd;
        draw_pos = -1;
    }
    auto pos = TUI::getCursorPos();
    pos.col = pos_before.col;
    input_col = pos.col;
    if (scroll_amount > 0) {
        pos.row = std::max(1, pos_before.row - scroll_amount);
    }
    return pos.row;
}

void KeyWatcher::LineBuffer::hide_completion_menu(int current_input_row)
{
    is_completion_active = false;
    is_display_dirty = true; // menu closed, need redraw
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

	if (last_sep == 0 && prefix.size() > 0) {
		// Command completion: starts with / and no path separator after it
		// e.g., "/h", "/help", "/status" -> match against keywords only
		for (const auto& kw : s_keywords) {
			if (ci_starts_with(kw, prefix)) {
				candidates.push_back(kw);
			}
		}
	} else if (last_sep != std::string::npos) {
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
static std::wstring get_clipboard_text() {
    if (!OpenClipboard(nullptr)) return L"";
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    std::wstring result;
    if (h) {
        const wchar_t* wtext = static_cast<const wchar_t*>(GlobalLock(h));
        if (wtext) {
            result = wtext;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return result;
}
#else
static std::wstring get_clipboard_text() {
    return ""; // not supported on POSIX in v1
}
#endif


Key KeyWatcher::read_key()
{
    while (!s_read_queue.size() && s_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!s_read_queue.size()) return Key::K_ZERO;
    s_read_mutex.lock();
    Key k = *s_read_queue.begin();
    s_read_queue.erase(s_read_queue.begin());
    s_read_mutex.unlock();
    return k;
}

void KeyWatcher::push_key_queue(const Key& k)
{
    s_read_mutex.lock();
    s_read_queue.push_back(k);
    s_read_mutex.unlock();
}

/// Read a single key press, handling escape sequences for arrow keys etc.
void KeyWatcher::read_key_thread() {
#ifdef _WIN32
    INPUT_RECORD rec;
    DWORD count;
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);

    while (s_running.load()) {
        ReadConsoleInputW(hIn, &rec, 1, &count);

        if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown) {
            if (s_callback) {
                s_callback(rec.Event.KeyEvent.uChar.AsciiChar);
            }
            // ESC detection
            if (rec.Event.KeyEvent.wVirtualKeyCode == 27) {
                push_key_queue(Key::K_ESC);
            }
            // Ctrl+V detection
            else if (rec.Event.KeyEvent.wVirtualKeyCode == 'V' &&
                (rec.Event.KeyEvent.dwControlKeyState & LEFT_CTRL_PRESSED ||
                 rec.Event.KeyEvent.dwControlKeyState & RIGHT_CTRL_PRESSED)) {
                push_key_queue(Key::K_CTRL_V);
            }
            // Alt+Enter detection
            else if (rec.Event.KeyEvent.wVirtualKeyCode == VK_RETURN &&
                (rec.Event.KeyEvent.dwControlKeyState & LEFT_ALT_PRESSED ||
                 rec.Event.KeyEvent.dwControlKeyState & RIGHT_ALT_PRESSED)) {
                push_key_queue(Key::K_ALT_ENTER);
            }
            // Ctrl+Enter — insert newline + enable line display mode
            else if (rec.Event.KeyEvent.wVirtualKeyCode == VK_RETURN &&
                (rec.Event.KeyEvent.dwControlKeyState & LEFT_CTRL_PRESSED ||
                 rec.Event.KeyEvent.dwControlKeyState & RIGHT_CTRL_PRESSED)) {
                push_key_queue(Key::K_CTRL_ENTER);
            }

            // Shift+Enter — insert newline + enable line display mode
            else if (rec.Event.KeyEvent.wVirtualKeyCode == VK_RETURN &&
                (rec.Event.KeyEvent.dwControlKeyState & SHIFT_PRESSED)) {
                push_key_queue(Key::K_SHIFT_ENTER);
            }

            // Ctrl+C detection (already handled by ASCII 3, but be explicit)
            else if (rec.Event.KeyEvent.wVirtualKeyCode == 'C' &&
                (rec.Event.KeyEvent.dwControlKeyState & LEFT_CTRL_PRESSED ||
                 rec.Event.KeyEvent.dwControlKeyState & RIGHT_CTRL_PRESSED)) {
                push_key_queue(Key::K_CTRL_C);
            }

            else if (rec.Event.KeyEvent.wVirtualKeyCode == VK_UP)    push_key_queue(Key::K_UP);
            else if (rec.Event.KeyEvent.wVirtualKeyCode == VK_DOWN)  push_key_queue(Key::K_DOWN);
            else if (rec.Event.KeyEvent.wVirtualKeyCode == VK_LEFT)  push_key_queue(Key::K_LEFT);
            else if (rec.Event.KeyEvent.wVirtualKeyCode == VK_RIGHT) push_key_queue(Key::K_RIGHT);
            else if (rec.Event.KeyEvent.wVirtualKeyCode == VK_TAB)   push_key_queue(Key::K_TAB);
            else if (rec.Event.KeyEvent.wVirtualKeyCode == VK_RETURN)push_key_queue(Key::K_ENTER);
            else if (rec.Event.KeyEvent.wVirtualKeyCode == VK_BACK)  push_key_queue(Key::K_BACKSPACE);
            else if (rec.Event.KeyEvent.wVirtualKeyCode == VK_DELETE && rec.Event.KeyEvent.uChar.AsciiChar == 0) push_key_queue(Key::K_DELETE);
            else if (rec.Event.KeyEvent.wVirtualKeyCode == VK_PRIOR) push_key_queue(Key::K_PGUP);
            else if (rec.Event.KeyEvent.wVirtualKeyCode == VK_NEXT)  push_key_queue(Key::K_PGDOWN);
            else if (rec.Event.KeyEvent.wVirtualKeyCode == VK_HOME)  push_key_queue(Key::K_HOME);
            else if (rec.Event.KeyEvent.wVirtualKeyCode == VK_END)   push_key_queue(Key::K_END);

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
            push_key_queue(Key::from_codepoint(chr));
        }
    }
#else
    // Linux: use select() to check for input, then drain all available bytes.
    // This prevents lost input during fast typing — same principle as Windows.
    fd_set fds;
    struct timeval tv;

    while (s_running.load()) {
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);

        // Block until there's something to read
        tv.tv_sec = 0;
        tv.tv_usec = 10000; // 10ms timeout
        if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) <= 0)
            continue;

        // Drain all available bytes as fast as possible
        unsigned char buf[256];
        ssize_t total = read(STDIN_FILENO, buf, sizeof(buf));
        if (total <= 0) continue;

        size_t pos = 0;
        while (pos < static_cast<size_t>(total)) {
            unsigned char c = buf[pos++];

            if (s_callback) {
                s_callback(c);
            }

            // Ctrl+V = ASCII 22
            if (c == 22) { push_key_queue(Key::K_CTRL_V); continue; }
            // Ctrl+C = ASCII 3
            if (c == 3) { push_key_queue(Key::K_CTRL_C); continue; }
            // Tab
            if (c == '\t') { push_key_queue(Key::K_TAB); continue; }
            // Backspace: DEL(127) or Ctrl+H(8)
            if (c == 127 || c == 8) { push_key_queue(Key::K_BACKSPACE); continue; }

            // Enter / Ctrl+Enter / Shift+Enter
            if (c == '\r') {
                // On POSIX, Ctrl+Enter and Enter both produce \r.
                // We treat plain Enter as K_ENTER for simplicity.
                push_key_queue(Key::K_ENTER);
                continue;
            }

            // ESC sequence or Alt+
            if (c == 27) {
                if (pos >= static_cast<size_t>(total)) { push_key_queue(Key::K_ESC); break; }
                unsigned char c2 = buf[pos++];

                if (c2 == '[') {
                    if (pos >= static_cast<size_t>(total)) { push_key_queue(Key::K_ESC); break; }
                    unsigned char c3 = buf[pos++];

                    switch (c3) {
                        case 'A': push_key_queue(Key::K_UP); break;
                        case 'B': push_key_queue(Key::K_DOWN); break;
                        case 'C': push_key_queue(Key::K_RIGHT); break;
                        case 'D': push_key_queue(Key::K_LEFT); break;
                        case 'F': push_key_queue(Key::K_END); break;
                        case 'H': push_key_queue(Key::K_HOME); break;
                        case '~':
                            if (pos >= static_cast<size_t>(total)) { push_key_queue(Key::K_ESC); break; }
                            unsigned char c4 = buf[pos++];
                            switch (c4) {
                                case '2': push_key_queue(Key::K_DELETE); break;
                                case '5': push_key_queue(Key::K_PGUP); break;
                                case '6': push_key_queue(Key::K_PGDOWN); break;
                                default:  push_key_queue(Key::K_ESC); break;
                            }
                            break;
                        default: push_key_queue(Key::K_ESC); break;
                    }
                } else if (c2 == 'O') {
                    if (pos >= static_cast<size_t>(total)) { push_key_queue(Key::K_ESC); break; }
                    unsigned char c3 = buf[pos++];
                    switch (c3) {
                        case 'F': push_key_queue(Key::K_END); break;
                        case 'H': push_key_queue(Key::K_HOME); break;
                        case 'P': push_key_queue(Key::K_PGUP); break;
                        case 'Q': push_key_queue(Key::K_PGDOWN); break;
                        default:  push_key_queue(Key::K_ESC); break;
                    }
                } else if (c2 == '\r') {
                    // Alt+Enter on some terminals
                    push_key_queue(Key::K_ALT_ENTER);
                } else {
                    push_key_queue(Key::K_ESC);
                }
            }
            // Normal ASCII character
            else if (c >= 32 && c < 127) {
                Key k{};
                k.code[0] = c;
                k.size = 1;
                push_key_queue(k);
            }
            // UTF-8 multi-byte: read continuation bytes from the drained buffer
            else if ((c & 0x80)) {
                size_t expected;
                if ((c & 0xE0) == 0xC0) expected = 2;
                else if ((c & 0xF0) == 0xE0) expected = 3;
                else if ((c & 0xF8) == 0xF0) expected = 4;
                else { push_key_queue(Key::K_ESC); continue; }

                size_t have = 1;
                while (have < expected && pos < static_cast<size_t>(total)) {
                    pos++;
                    have++;
                }

                Key k{};
                for (size_t i = 0; i < have; ++i) k.code[i] = buf[pos - have + i];
                k.size = static_cast<int>(have);
                push_key_queue(k);
            }
        }
    }
#endif
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
    buf.set_prompt(prompt_text);
    buf.prompt_len = static_cast<size_t>(prompt_len);
    buf.pos = 0;
    history.reset();

    // State for tracking whether we're browsing history
    std::string original_text;
    bool was_browsing = false;
    // Get cursor position before printing (prompt's starting row)
    auto cursor_pos = TUI::getCursorPos();
    buf.prompt_row = cursor_pos.row;
    buf.is_display_dirty = true;

    while (true) {
        // ── Render the current line(s) ────────────────────────
        int final_row = buf.row + buf.prompt_row - 1;

        if (buf.is_display_dirty) {
            buf.recompute();
            //buf.clear_prompt();
            //std::cout << buf.display_text();

            buf.clear();
            buf.draw();

            // Print hint in dim color
            if (!buf.hint.empty()) {
                buf.print_hint();
                TUI::setAnsiCode(0); // reset color
            }

            final_row = buf.row + buf.prompt_row - 1;

            // ── Completion menu rendering ────────────────────────
            if (buf.is_completion_active) {
                buf.draw_completion_menu(final_row);
            }
            TUI::setCursorPos(final_row, buf.col);
        } else {
            // Only cursor moved — recompute position and move cursor
            buf.recompute();
            final_row = buf.row + buf.prompt_row - 1;
            TUI::setCursorPos(final_row, buf.col);
        }
        buf.is_display_dirty = false;

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
                    handled = true;
                }
            }

            if (!handled && (k == Key::K_DOWN)) {
                // Current page may have fewer items than max_displayed (last page)
                int items_on_page = std::min(static_cast<int>(max_displayed),
                                            static_cast<int>(buf.candidates.size()) - (int)buf.page_offset);
                if (buf.selected >= items_on_page - 1) {
                    if (buf.page_offset + max_displayed < buf.candidates.size()) {
                        // Advance to next page
                        buf.page_offset += max_displayed;
                        buf.selected = 0;
                    } else {
                        // Already at last page, clamp to actual last item
                        buf.selected = items_on_page - 1;
                    }
                }
                else {
                    buf.selected++;
                }
                buf.show_hint();
                handled = true;
            }
            else if (!handled && (k == Key::K_UP)) {
                if (buf.selected > 0) {
                    buf.selected--;
                }
                else {
                    if (buf.page_offset >= max_displayed) {
                        // Go to previous page, set selected to last item
                        buf.page_offset -= max_displayed;
                        buf.selected = static_cast<int>(max_displayed) - 1;
                    } else {
                        // Already at first page, clamp selected
                        buf.selected = 0;
                    }
                }
                buf.show_hint();
                handled = true;
            }

            // Page navigation
            if (!handled && (k == Key::K_PGDOWN || k == Key::K_RIGHT) && buf.candidates.size() > max_displayed) {
                // Advance by one page; stay put if already at last page
                if (buf.page_offset + max_displayed < buf.candidates.size())
                    buf.page_offset += max_displayed;
                // Clamp selected to the new page's actual item count
                int items_on_page = std::min(static_cast<int>(max_displayed),
                                            static_cast<int>(buf.candidates.size()) - (int)buf.page_offset);
                if (buf.selected >= items_on_page)
                    buf.selected = items_on_page - 1;
                handled = true;
            }
            else if (!handled && (k == Key::K_PGUP || k == Key::K_LEFT) && buf.candidates.size() > max_displayed) {
                // Go back one page; stay at first page if already there
                if (buf.page_offset >= max_displayed)
                    buf.page_offset -= max_displayed;
                else
                    buf.page_offset = 0;
                // Clamp selected to the new page's actual item count
                int items_on_page = std::min(static_cast<int>(max_displayed),
                                            static_cast<int>(buf.candidates.size()) - (int)buf.page_offset);
                if (buf.selected >= items_on_page)
                    buf.selected = items_on_page - 1;
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
                buf.is_display_dirty = true; // menu selection changed, need redraw
                continue;
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
            std::wstring clip = get_clipboard_text();
            if (!clip.empty()) {
                for (auto ws : clip) {
                    Key ks = Key::from_codepoint(ws);
                    buf.insert_char(ks);
                }
            }
            continue;
        }

        if (k == Key::K_ALT_ENTER || k == Key::K_CTRL_ENTER || k == Key::K_SHIFT_ENTER) {
            Key knl{}; knl.code[0] = '\n'; knl.size = 1;
            buf.insert_char(knl);
            int exceed_line = buf.prompt_row + buf.row - TUI::getTerminalHeight();
            if (exceed_line > 1) {
                buf.prompt_row--;
                printf("\n");
            }
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
            if (buf.move_up(term_width)) {
                continue;
            }
            if (!history.is_browsing()) {
                original_text = buf.input();
            }
            if (history.prev()) {
                const std::string* entry = history.get_current();
                if (entry) {
                    buf.set_text(*entry);
                }
            }
            continue;
        }
        if (k == Key::K_DOWN) {
            if (buf.move_down(term_width)) {
                continue;
            }
            if (history.is_browsing()) {
                if (history.next()) {
                    const std::string* entry = history.get_current();
                    if (entry) {
                        buf.set_text(*entry);
                    }
                    else {
                        // Back to original text
                        buf.set_text(original_text);
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
                }
                else {
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
                buf.is_display_dirty = true;
            }
            continue;
        }

        if (k == Key::K_HOME) {
            buf.move_home();
            continue;
        }

        if (k == Key::K_END) {
            buf.move_end();
            continue;
        }

        // ── Normal character input ────────────────────────────
        if (k.ch >= 32 && k.ch < 127) {
            char c = k.code[0];

            // If there's a hint and the typed character matches, consume from hint
            if (!buf.hint.empty() && buf.hint[0] == c) {
                buf.insert_char(Key::from_codepoint(c));
                buf.hint.erase(buf.hint.begin());
            }
            else {
                // No hint or no match — insert normally and clear hint
                if (!buf.hint.empty()) {
                    buf.clear_hint();
                }
                buf.insert_char(Key::from_codepoint(c));
            }

            // Auto-complete: check candidates after inserting a character
            std::string prefix = buf.get_prefix();
            std::vector<std::string> candidates;
            KeyWatcher::build_candidates(prefix, candidates);

            if (!candidates.empty()) {
                buf.candidates = std::move(candidates);
                if (buf.is_completion_active) {
                    buf.page_offset = 0;
                }
                else {
                    buf.selected = 0;
                }
                buf.show_hint();
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
