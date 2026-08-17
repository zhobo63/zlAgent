#include "pch.h"
#include "tui.h"

// ── Static member definitions ───────────────────────

bool TUI::s_enabled = true;

std::mutex& TUI::s_mutex() {
    static std::mutex mtx;
    return mtx;
}

// ── Unified output ──────────────────────────────────

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
