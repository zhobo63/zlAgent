#ifndef TUI_H
#define TUI_H

#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <string>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/ioctl.h>
#endif

// ── ANSI color helpers ───────────────────────────
enum class AnsiColor {
    Black       = 0,
    Red         = 1,
    Green       = 2,
    Yellow      = 3,
    Blue        = 4,
    Magenta     = 5,
    Cyan        = 6,
    White       = 7,
    BrightBlack = 8, // gray (bright black)
};

// \x1b	Hexadecimal
// \033	Octal

inline std::string ansi_color(AnsiColor fg, bool bold = false) {
    return "\033[" + std::to_string(bold ? 1 : 0) + ";" + std::to_string(30 + static_cast<int>(fg)) + "m";
}

class TUI {
public:
    // ── Style ─────────────────────────────────────
    static constexpr const char* ANSI_RESET   = "\033[0m";  // reset all
    static constexpr const char* ANSI_BOLD    = "\033[1m";  // bold
    static constexpr const char* ANSI_DIM     = "\033[2m";  // dim
    static constexpr const char* ANSI_ITALIC  = "\033[3m";  // italic
    static constexpr const char* ANSI_UNDER   = "\033[4m";  // underline
    static constexpr const char* ANSI_STRIKE  = "\033[9m";  // strikethrough

    // ── Foreground colors ─────────────────────────
    static constexpr const char* ANSI_FG_BLACK   = "\033[30m";
    static constexpr const char* ANSI_FG_RED     = "\033[31m";
    static constexpr const char* ANSI_FG_GREEN   = "\033[32m";
    static constexpr const char* ANSI_FG_YELLOW  = "\033[33m";
    static constexpr const char* ANSI_FG_BLUE    = "\033[34m";
    static constexpr const char* ANSI_FG_MAGENTA = "\033[35m";
    static constexpr const char* ANSI_FG_CYAN    = "\033[36m";
    static constexpr const char* ANSI_FG_WHITE   = "\033[37m";

    // ── Bright foreground colors ──────────────────
    static constexpr const char* ANSI_BRIGHT_BLACK  = "\033[90m";  // gray
    static constexpr const char* ANSI_BRIGHT_RED    = "\033[91m";
    static constexpr const char* ANSI_BRIGHT_GREEN  = "\033[92m";
    static constexpr const char* ANSI_BRIGHT_YELLOW = "\033[93m";
    static constexpr const char* ANSI_BRIGHT_BLUE   = "\033[94m";
    static constexpr const char* ANSI_BRIGHT_MAGENTA= "\033[95m";
    static constexpr const char* ANSI_BRIGHT_CYAN   = "\033[96m";
    static constexpr const char* ANSI_BRIGHT_WHITE  = "\033[97m";

    // ── Background colors ─────────────────────────
    static constexpr const char* ANSI_BG_BLACK   = "\033[40m";
    static constexpr const char* ANSI_BG_RED     = "\033[41m";
    static constexpr const char* ANSI_BG_GREEN   = "\033[42m";
    static constexpr const char* ANSI_BG_YELLOW  = "\033[43m";
    static constexpr const char* ANSI_BG_BLUE    = "\033[44m";
    static constexpr const char* ANSI_BG_MAGENTA = "\033[45m";
    static constexpr const char* ANSI_BG_CYAN    = "\033[46m";
    static constexpr const char* ANSI_BG_WHITE   = "\033[47m";

    // ── Bright background colors ──────────────────    
    static constexpr const char* ANSI_BRIGHT_BG_BLACK  = "\033[100m";
    static constexpr const char* ANSI_BRIGHT_BG_RED    = "\033[101m";
    static constexpr const char* ANSI_BRIGHT_BG_GREEN  = "\033[102m";
    static constexpr const char* ANSI_BRIGHT_BG_YELLOW = "\033[103m";
    static constexpr const char* ANSI_BRIGHT_BG_BLUE   = "\033[104m";
    static constexpr const char* ANSI_BRIGHT_BG_MAGENTA= "\033[105m";
    static constexpr const char* ANSI_BRIGHT_BG_CYAN   = "\033[106m";
    static constexpr const char* ANSI_BRIGHT_BG_WHITE  = "\033[107m";

    // ── Cursor control ────────────────────────────
    static constexpr const char* ANSI_CURSOR_HOME     = "\033[H";       // move to top-left
    static constexpr const char* ANSI_CLEAR_LINE      = "\033[K";       // clear from cursor to end of line
    static constexpr const char* ANSI_CLEAR_SCREEN    = "\033[2J";      // clear entire screen
    static constexpr const char* ANSI_SCROLL_UP       = "\033[1A";      // move cursor up one line
    static constexpr const char* ANSI_SCROLL_DOWN     = "\033[1B";      // move cursor down one line
    static constexpr const char* ANSI_CLEAR_TO_END    = "\x1B[0J";	    // clears from cursor until end of screen
    static constexpr const char* ANSI_CLEAR_TO_BEGIN  = "\x1B[1J";      // clears from cursor to beginning of screen

    // ── Utility functions ─────────────────────────

    /// Clear current line from cursor to end, then move cursor home
    static inline void cls() {
        cout << ANSI_CURSOR_HOME << ANSI_CLEAR_LINE;
    }

    /// Flush stdout buffer
    static inline void flush() {
        cout.flush();
    }

    /// Clear entire screen and move cursor to top-left
    static inline void clearScreen() {
        cout << ANSI_CLEAR_SCREEN << ANSI_CURSOR_HOME;
    }

    /// Clear current line from cursor to end
    static inline void clearLine() {
        cout << ANSI_CLEAR_LINE;
    }

    /// Move cursor home and flush
    static inline void home() {
        cout << ANSI_CURSOR_HOME;
    }

    /// Output reset code (clear all styles)
    static inline void reset() {
        cout << ANSI_RESET;
    }

    /// Save cursor position
    static inline void saveCursor() {
        cout << "\033[s";
    }

    /// Restore cursor to saved position
    static inline void restoreCursor() {
        cout << "\033[u";
    }

    /// Move cursor up `n` lines (default 1)
    static inline void scrollUp(int n = 1) {
        for (int i = 0; i < n; ++i) cout << ANSI_SCROLL_UP;
    }

    /// Move cursor down `n` lines (default 1)
    static inline void scrollDown(int n = 1) {
        for (int i = 0; i < n; ++i) cout << ANSI_SCROLL_DOWN;
    }

    /// Set cursor to specific row/col (1-based)
    static inline void setCursorPos(int row, int col) {
    	cout << "\033[" << row << ";" << col << "H";
    }

    /// Get terminal width in columns. Falls back to 80.
    static inline int getTerminalWidth() {
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

    /// Get terminal height in rows. Falls back to 24.
    static inline int getTerminalHeight() {
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

    /// Cursor position (1-based row/col).
    struct CursorPos {
    	int row = 1;
    	int col = 1;
    };

    /// Get current cursor position. Falls back to {1, 1} on failure.
    static inline CursorPos getCursorPos() {
    #ifdef _WIN32
    	CONSOLE_SCREEN_BUFFER_INFO csbi;
    	if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
    		return {1, 1};
    	return {static_cast<int>(csbi.dwCursorPosition.Y + 1), static_cast<int>(csbi.dwCursorPosition.X + 1)};
    #else
    	// Send DSR request and read response (non-blocking, 50ms timeout)
    	printf("\033[6n");
    	fflush(stdout);
    	struct timeval tv = {0, 50000};
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
    #endif
    }

    /// Emit a raw ANSI SGR code (e.g. 2=dim, 37=white, 0=reset).
    static inline void setAnsiCode(int code) {
    	printf("\x1b[%dm", code);
    }

    /// Wrap text with a foreground color and optional bold
    static inline std::string color(const std::string& text, AnsiColor fg, bool bold = false) {
        return ansi_color(fg, bold) + text + ANSI_RESET;
    }

    /// Bold text (no color change)
    static inline std::string bold(const std::string& text) {
        return ANSI_BOLD + text + ANSI_RESET;
    }

    /// Dim text (for thinking/secondary content)
    static inline std::string dim(const std::string& text) {
        return ANSI_DIM + text + ANSI_RESET;
    }

    static inline std::string cursor_pos(int row, int col) {
        return "\x1b[" + std::to_string(row) + ";" + std::to_string(col) + "H";
    }

    static inline std::string cursor_up(int row) {
        return "\x1b[" + std::to_string(row) + "A";
    }

    static inline std::string cursor_down(int row) {
        return "\x1b[" + std::to_string(row) + "B";
    }

    static inline std::string cursor_right(int col) {
        return "\x1b[" + std::to_string(col) + "C";
    }

    static inline std::string cursor_left(int col) {
        return "\x1b[" + std::to_string(col) + "D";
    }

    /// Underline text
    static inline std::string underline(const std::string& text) {
        return ANSI_UNDER + text + ANSI_RESET;
    }
    static inline std::string check(const std::string& text, bool checked) {
        if (checked) {
            return color(u8"✅ " + text, AnsiColor::Green);
        }
        else {
            return color(u8"❌ " + text, AnsiColor::BrightBlack);
        }
    }

    /// Print a colored line with newline
    static inline void printColor(const std::string& text, AnsiColor fg, bool bold = false) {
        cout << ansi_color(fg, bold) << text << ANSI_RESET << "\n";
    }

    /// Print a dimmed line (for thinking/secondary output)
    static inline void printDim(const std::string& text) {
        if (!s_enabled) return;
        cout << ANSI_DIM << text << ANSI_RESET << "\n";
    }

    // ── Unified output entry point ────────────────

    /// Enable or disable all TUI::out() / TUI::err() calls. Default: true.
    static void set_output_enabled(bool enabled);

    struct OStream 
    {
        OStream& operator<<(const std::string& text);
        OStream& operator<<(int value);

        void flush();
    };

    static OStream cout;
    static OStream cerr;
private:
    static std::mutex& s_mutex();
    static bool       s_enabled;
};

//TUI::OStream& operator<< (TUI::OStream&, const std::string &text);

#endif // TUI_H
