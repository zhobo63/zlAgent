#include "pch.h"
#include "tui.h"

// ── Static member definitions ───────────────────────

bool TUI::s_enabled = true;

std::mutex& TUI::s_mutex() {
    static std::mutex mtx;
    return mtx;
}

// ── Unified output ──────────────────────────────────

void TUI::out(const char* fmt, ...) {
    if (!s_enabled) return;

    va_list args;
    va_start(args, fmt);
    std::lock_guard<std::mutex> lock(s_mutex());

    // First pass: determine buffer size.
    va_list args_copy;
    va_copy(args_copy, args);
    int len = std::vsnprintf(nullptr, 0, fmt, args_copy);
    va_end(args_copy);

    if (len < 0) {
        va_end(args);
        return;
    }

    // Second pass: format into buffer.
    std::string buf(static_cast<size_t>(len), '\0');
    std::vsnprintf(buf.data(), static_cast<size_t>(len) + 1, fmt, args);
    va_end(args);

    std::cout << buf;

    agent::send_event("out", buf);
}

void TUI::set_output_enabled(bool enabled) {
    s_enabled = enabled;
}

TUI::OStream TUI::cout;
TUI::OStream TUI::cerr;

TUI::OStream& TUI::OStream::operator<<(const std::string& text)
{
    if (!s_enabled) return *this;
    std::cout << text;
    agent::send_event("out", text);
    return *this;
}

TUI::OStream& TUI::OStream::operator<<(int value)
{
    if (!s_enabled) return *this;
    std::cout << value;
    agent::send_event("out", std::to_string(value));
    return *this;
}

void TUI::OStream::flush()
{
    std::cout.flush();
}

//TUI::OStream& operator<< (TUI::OStream& cout, const std::string& text)
//{
//    return cout << text;
//}
