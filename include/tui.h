#ifndef TUI_H
#define TUI_H

#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <string>
#include <sstream>

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

    // ── Utility functions ─────────────────────────

    /// Clear current line from cursor to end, then move cursor home
    static inline void cls() {
        std::cout << ANSI_CURSOR_HOME << ANSI_CLEAR_LINE;
    }

    /// Flush stdout buffer
    static inline void flush() {
        std::cout << std::flush;
    }

    /// Clear entire screen and move cursor to top-left
    static inline void clearScreen() {
        std::cout << ANSI_CLEAR_SCREEN << ANSI_CURSOR_HOME;
    }

    /// Clear current line from cursor to end
    static inline void clearLine() {
        std::cout << ANSI_CLEAR_LINE;
    }

    /// Move cursor home and flush
    static inline void home() {
        std::cout << ANSI_CURSOR_HOME;
    }

    /// Output reset code (clear all styles)
    static inline void reset() {
        std::cout << ANSI_RESET;
    }

    /// Save cursor position
    static inline void saveCursor() {
        std::cout << "\033[s";
    }

    /// Restore cursor to saved position
    static inline void restoreCursor() {
        std::cout << "\033[u";
    }

    /// Move cursor up `n` lines (default 1)
    static inline void scrollUp(int n = 1) {
        for (int i = 0; i < n; ++i) std::cout << ANSI_SCROLL_UP;
    }

    /// Move cursor down `n` lines (default 1)
    static inline void scrollDown(int n = 1) {
        for (int i = 0; i < n; ++i) std::cout << ANSI_SCROLL_DOWN;
    }

    /// Set cursor to specific row/col (1-based)
    static inline void setCursorPos(int row, int col) {
        std::cout << "\033[" << row << ";" << col << "H";
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
        std::cout << ansi_color(fg, bold) << text << ANSI_RESET << std::endl;
    }

    /// Print a dimmed line (for thinking/secondary output)
    static inline void printDim(const std::string& text) {
        if (!s_enabled) return;
        std::cout << ANSI_DIM << text << ANSI_RESET << std::endl;
    }

    // ── Unified output entry point ────────────────

    /// printf-style output to stdout, auto-flush, thread-safe.
    static void out(const char* fmt, ...);

    /// Output a string directly to stdout, auto-flush, thread-safe.
    static void out(const std::string& text);

    /// printf-style output to stderr, auto-flush, thread-safe.
    static void err(const char* fmt, ...);

    /// Enable or disable all TUI::out() / TUI::err() calls. Default: true.
    static void set_output_enabled(bool enabled);

private:
    static std::mutex& s_mutex();
    static bool       s_enabled;
};

#endif // TUI_H
