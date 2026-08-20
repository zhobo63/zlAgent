#include "pch.h"
#include "tui.h"

// ── Static member definitions ───────────────────────

bool TOUT::s_enabled = true;

std::mutex& TOUT::s_mutex() {
    static std::mutex mtx;
    return mtx;
}

// ── Unified output ──────────────────────────────────

void TOUT::set_output_enabled(bool enabled) {
    s_enabled = enabled;
}

TOUT::OStream TOUT::cout;
TOUT::OStream TOUT::cerr;

TOUT::OStream& TOUT::OStream::operator<<(const std::string& text)
{
    if (!s_enabled) return *this;
    std::cout << text;
    agent::send_event("out", text);
    return *this;
}

TOUT::OStream& TOUT::OStream::operator<<(const char* text)
{
    return this->operator<<(std::string(text));
}

TOUT::OStream& TOUT::OStream::operator<<(const char ch)
{
    return this->operator<<(std::to_string(ch));
}

TOUT::OStream& TOUT::OStream::operator<<(int value)
{
    return this->operator<<(std::to_string(value));
}

TOUT::OStream& TOUT::OStream::operator<<(size_t value)
{
    return this->operator<<(std::to_string(value));
}

void TOUT::OStream::flush()
{
    std::cout.flush();
}
