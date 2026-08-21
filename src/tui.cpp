#include "pch.h"
#include "tui.h"
#include "key_watcher.h"

#include <algorithm>
#include <cctype>
#include <iomanip>

using namespace agent;

namespace {

std::string format_diff_line_number(int number, std::size_t width)
{
    if (number < 0) {
        return std::string(width, ' ');
    }

    std::ostringstream formatted;
    formatted << std::setw(static_cast<int>(width)) << number;
    return formatted.str();
}

std::string render_markdown_inline(const std::string& text)
{
    std::string rendered;
    rendered.reserve(text.size());

    for (std::size_t i = 0; i < text.size();) {
        if (text.compare(i, 2, "**") == 0 || text.compare(i, 2, "__") == 0) {
            rendered += TUI::ANSI_BOLD;
            i += 2;
        }
        else if (text[i] == '*' || text[i] == '_') {
            rendered += TUI::ANSI_ITALIC;
            ++i;
        }
        else if (text[i] == '`') {
            rendered += TUI::ANSI_FG_YELLOW;
            ++i;
        }
        else {
            rendered += text[i];
            ++i;
        }
    }

    return rendered;
}

} // namespace

// ── Static member definitions ───────────────────────

bool TOUT::s_enabled = true;
TUI::RichText::Style TOUT::current_style;
TOUT::OutputMode_ TOUT::output_mode = TOUT::OutputMode_cout;
TOUT::OStream TOUT::cout;
TOUT::OStream TOUT::cerr;

TOUT::fn_message TOUT::on_message;
TOUT::fn_append TOUT::on_append;
TOUT::fn_token TOUT::on_token;

std::mutex& TOUT::s_mutex() {
    static std::mutex mtx;
    return mtx;
}

// ── Unified output ──────────────────────────────────

static const char* level_tag(int lvl) {
    switch (lvl) {
    case 0: return "[D]";
    case 1: return "[I]";
    case 2: return "[W]";
    case 3: return "[E]";
    default: return "[?]";
    }
}

static const char* level_color(int lvl) {
#if defined(_WIN32) && !defined(__MINGW32__)
    // Windows MSVC supports ANSI escape codes in the new console (Win10+).
    // If you need legacy support, replace with SetConsoleTextAttribute.
#endif
    switch (lvl) {
    case 0: return "\033[90m";  // grey
    case 1:  return "\033[36m";  // cyan
    case 2:  return "\033[33m";  // yellow
    case 3: return "\033[31m";  // red
    default:              return "";
    }
}

static TUI::Color level_tui_color(int lvl)
{
    switch (lvl) {
    case 0: return TUI::AnsiColor_Bright_Black;
    case 1: return TUI::AnsiColor_Cyan;
    case 2: return TUI::AnsiColor_Yellow;
    case 3: return TUI::AnsiColor_Red;
    default: return TUI::AnsiColor_White;
    }
}


void TOUT::log(int lv, const char* component, const std::string& msg)
{
    switch (output_mode) {
    case OutputMode_cout:
        cout << level_color(lv) << level_tag(lv)
            << component << " " << msg << TUI::ANSI_RESET << "\n";
        break;
    case OutputMode_TUI:
        if (on_message) {
            on_message(level_tui_color(lv),
                       level_tag(lv) + std::string(component) + " " + msg);
        }
        break;
    }
}

void TOUT::message(const TUI::Color color, const std::string& msg)
{
    switch (output_mode) {
    case OutputMode_cout:
        cout << color.toAnsi(true) << msg << TUI::ANSI_RESET << "\n";
        break;
    case OutputMode_TUI:
        if (on_message) {
            on_message(color, msg);
        }
        break;
    }
}

void TOUT::set_style(const TUI::RichText::Style& style)
{
    current_style = style;
}
void TOUT::set_style(const TUI::Color& fgcolor)
{
    current_style.fg_color = fgcolor;
}
void TOUT::append(const std::string& msg)
{
    switch (output_mode) {
    case OutputMode_cout:
        cout << current_style.fg_color.toAnsi(true);
        cout << msg;
        break;
    case OutputMode_TUI:
        if (on_append) {
            on_append(msg);
        }
        break;
    }
}

void TOUT::append(const TUI::Color& fgcolor, const std::string& msg)
{
    set_style(fgcolor);
    append(msg);
}

void TOUT::token(bool reasoning, const std::string& msg)
{
    switch (output_mode) {
    case OutputMode_cout:
        append(msg);
        break;
    case OutputMode_TUI:
        if (on_token) {
            on_token(reasoning, msg);
        }
        break;
    }
}

void TOUT::check(const std::string& text, bool checked) {
    if (checked) {
        append(TUI::AnsiColor_Green, u8"✅ " + text);
    }
    else {
        append(TUI::AnsiColor_Bright_Black, u8"❌ " + text);
    }
}

void TOUT::diff(const std::string& path, const agent::Diff& diff)
{
    switch (output_mode) {
    case OutputMode_cout: {
        cout << "\n" << TUI::ANSI_FG_WHITE << path << TUI::ANSI_RESET << "\n";
        const std::size_t line_width = std::max<std::size_t>(
            1, std::to_string(diff.max_line).size());

        for (const auto& l : diff.lines) {
            const std::string line_number =
                format_diff_line_number(l.number, line_width);

            switch (l.op) {
            case agent::Diff::Context:
                cout << TUI::ANSI_BRIGHT_BLACK << line_number << TUI::ANSI_FG_WHITE << "  " << l.line << "\n";
                break;
            case agent::Diff::Remove:
                cout << TUI::ANSI_BRIGHT_BLACK << line_number << TUI::ANSI_FG_RED << " -" << l.line << "\n";
                break;
            case agent::Diff::Add:
                cout << TUI::ANSI_BRIGHT_BLACK << line_number << TUI::ANSI_FG_GREEN << " +" << l.line << "\n";
                break;
            case agent::Diff::Separator:
                cout << TUI::ANSI_FG_WHITE << u8"\u2500\u2500\u2500\n";
                break;
            }        
        }
        break;
    }
    case OutputMode_TUI:

        break;
    }
}

void TOUT::markdown(const std::string& msg)
{
    switch (output_mode) {
    case OutputMode_cout: {
        std::istringstream stream(msg);
        std::string line;
        bool in_code_block = false;

        while (std::getline(stream, line)) {
            const auto first = line.find_first_not_of(" \t");
            const std::string leading = first == std::string::npos
                ? line
                : line.substr(0, first);
            const std::string content = first == std::string::npos
                ? std::string()
                : line.substr(first);

            if (content.rfind("```", 0) == 0) {
                in_code_block = !in_code_block;
                cout << TUI::ANSI_DIM << line << TUI::ANSI_RESET << "\n";
                continue;
            }

            if (in_code_block) {
                cout << TUI::ANSI_DIM << line << TUI::ANSI_RESET << "\n";
                continue;
            }

            if (content.rfind("#", 0) == 0) {
                std::size_t hashes = 0;
                while (hashes < content.size() && content[hashes] == '#') {
                    ++hashes;
                }
                if (hashes < content.size() && content[hashes] == ' ') {
                    cout << leading << TUI::ANSI_BOLD << TUI::ANSI_FG_CYAN
                         << render_markdown_inline(content) << TUI::ANSI_RESET << "\n";
                    continue;
                }
            }

            if (content.rfind("> ", 0) == 0) {
                cout << leading << TUI::ANSI_FG_CYAN
                     << render_markdown_inline(content) << TUI::ANSI_RESET << "\n";
                continue;
            }

            const bool unordered_list = content.size() >= 2 &&
                (content[0] == '-' || content[0] == '*' || content[0] == '+') &&
                content[1] == ' ';
            const auto dot = content.find(". ");
            const bool ordered_list = dot != std::string::npos && dot > 0 &&
                std::all_of(content.begin(), content.begin() + static_cast<std::ptrdiff_t>(dot),
                            [](unsigned char c) { return std::isdigit(c) != 0; });

            if (unordered_list || ordered_list) {
                const std::size_t marker_length = unordered_list ? 2 : dot + 2;
                cout << leading << TUI::ANSI_FG_YELLOW
                     << content.substr(0, marker_length) << TUI::ANSI_RESET
                     << render_markdown_inline(content.substr(marker_length)) << "\n";
                continue;
            }

            if (content == "---" || content == "***" || content == "___") {
                cout << TUI::ANSI_DIM << line << TUI::ANSI_RESET << "\n";
                continue;
            }

            cout << leading << render_markdown_inline(content)
                 << TUI::ANSI_RESET << "\n";
        }
        break;
    }
    case OutputMode_TUI:
        break;
    }
}

void TOUT::tool_result(const std::string& name, const std::string& result)
{
    switch (output_mode) {
    case OutputMode_cout: 
        cout << TUI::ANSI_FG_CYAN << name << TUI::ANSI_RESET << "\n";
        cout << TUI::ANSI_DIM << result << TUI::ANSI_RESET << "\n";
        break;
    case OutputMode_TUI:
        break;
    }
}

bool TOUT::confirm()
{
    switch (output_mode) {
    case OutputMode_cout:
    {
        auto k = KeyWatcher::read_key();
        char ch = 0;
        if (k.size > 0) ch = static_cast<char>(k.code[0]);
        std::string lower(1, ::tolower(static_cast<unsigned char>(ch)));
        return (lower == "y") ? true : false;
    }
        break;
    case OutputMode_TUI:
        break;
    }
    return false;
}

void TOUT::set_output_enabled(bool enabled) {
    s_enabled = enabled;
}


void TOUT::set_output_mode(OutputMode_ mode)
{
    output_mode = mode;
}

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
