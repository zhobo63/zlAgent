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

TUI::OStream& TUI::OStream::operator<<(const char* text)
{
    return this->operator<<(std::string(text));
}

TUI::OStream& TUI::OStream::operator<<(const char ch)
{
    return this->operator<<(std::to_string(ch));
}

TUI::OStream& TUI::OStream::operator<<(int value)
{
    return this->operator<<(std::to_string(value));
}

TUI::OStream& TUI::OStream::operator<<(size_t value)
{
    return this->operator<<(std::to_string(value));
}

void TUI::OStream::flush()
{
    std::cout.flush();
}
