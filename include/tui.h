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

#include "zltui.h"
#include "file_utils.h"

class TOUT {
public:

    // ── Utility functions ─────────────────────────

    using fn_message = std::function<void(const TUI::Color color, const std::string& msg)>;
    using fn_append = std::function<void(const std::string& msg)>;
    using fn_token = std::function<void(bool reasoning, const std::string& msg)>;
    using fn_confirm = std::function<bool(const std::string& msg)>;
    using fn_interrupted = std::function<void()>;
    using fn_select = std::function<void(int sel)>;
    static fn_message on_message;
    static fn_append on_append;
    static fn_token on_token;
    static fn_confirm on_confirm;
    static fn_interrupted on_interrupted;

    static std::vector<std::string> s_keywords;

    static void log(int lv, const char* component, const std::string& msg);
    static void message(const TUI::Color color, const std::string& msg);

    static TUI::RichText::Style current_style;
    static void set_style(const TUI::RichText::Style& style);
    static void set_style(const TUI::Color& fgcolor);
    static void append(const std::string& msg);
    static void append(const TUI::Color& fgcolor, const std::string& msg);
    static void token(bool reasoning, const std::string& msg);
    static void check(const std::string& text, bool checked);
    static void diff(const std::string& path, const agent::Diff& diff);
    static void markdown(const std::string& msg);
    static void tool_result(const std::string& name, const std::string& result);

    static void add_keywords(const std::vector<std::string>& keywords);
    static bool confirm(const std::string& msg);
    static void set_interrupted(fn_interrupted func);

    struct Model {
        std::string id;
        std::string info;
    };
    static void select_model(const std::vector<Model>& models, int current, fn_select cb);

    using fn_select_model = std::function<void(const std::vector<Model>& models, int current, fn_select cb)>;
    static fn_select_model on_select_model;

    // ── Unified output entry point ────────────────

    /// Enable or disable all cout / cerr calls. Default: true.
    static void set_output_enabled(bool enabled);

    enum OutputMode_ {
        OutputMode_cout,
        OutputMode_TUI,
    };
    static OutputMode_ output_mode;

    static void set_output_mode(OutputMode_ mode);

    struct OStream 
    {
        OStream& operator<<(const std::string& text);
        OStream& operator<<(const char* text);
        OStream& operator<<(const char ch);
        OStream& operator<<(int value);
        OStream& operator<<(size_t value);

        void flush();
    };

    static OStream cout;
    static OStream cerr;
private:
    static std::mutex& s_mutex();
    static bool       s_enabled;
};

#endif // TUI_H
