#include "pch.h"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "json.hpp"
#include "tui.h"
#include "user_reply.h"
#include "file_utils.h"
#include "key_watcher.h"
#include "safety_guard.h"

namespace agent {
using json = nlohmann::json;

// ── Helpers ───────────────────────────────────────────────

static std::string to_lower(const std::string& s) {
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return lower;
}

// Truncate a string for display, adding "..." if it was cut.
static std::string truncate(const std::string& s, size_t max_len) {
    if (s.size() <= max_len) return s;
    return s.substr(0, max_len) + "...";
}

// ── parse_reply_mode / reply_mode_to_string ───────────────

UserReplyMode parse_reply_mode(const std::string& value) {
    auto lower = to_lower(value);
    if (lower == "off")       return UserReplyMode::Off;
    if (lower == "exec")      return UserReplyMode::Exec;
    if (lower == "edit")      return UserReplyMode::Edit;
    if (lower == "always")    return UserReplyMode::Always;
    return UserReplyMode::Off; // default fallback
}

const char* reply_mode_to_string(UserReplyMode mode) {
    switch (mode) {
        case UserReplyMode::Off:     return "off";
        case UserReplyMode::Exec:    return "exec";
        case UserReplyMode::Edit:    return "edit";
        case UserReplyMode::Always:   return "always";
    }
    return "off";
}

// ── prompt_user_reply ─────────────────────────────────────

UserReplyResult prompt_user_reply(
    const std::string& tool_name,
    const std::string& json_args,
    const std::string& error_message) {

    UserReplyResult result;
    result.action = ReplyAction::Yes; // default

    bool is_error = !error_message.empty();

    // Display the intervention prompt.
    TOUT::append(TUI::AnsiColor_Yellow, u8"\n\u23F8  [User Reply]");
    if (is_error) {
        TOUT::append(TUI::AnsiColor_Bright_Red, " Tool failed: " + tool_name);
    } else {
        TOUT::append(" Tool: " + tool_name);
    }
    TOUT::append("\n    Args: " + json_args + "\n");

    if (is_error) {
        // Show only the first line of the error for brevity.
        auto nl = error_message.find('\n');
        std::string err_preview = (nl != std::string::npos) ? error_message.substr(0, nl) : error_message;
        TOUT::message(TUI::AnsiColor_Bright_Red, "    Error: " + truncate(err_preview, 120));
    }

    // Action descriptions differ slightly depending on whether this is an error or a pre-check.
    TOUT::append("\n    What would you like to do?\n"
        "    y/yes   - Retry with same arguments\n"
        "    n/no    - Skip this tool call\n");

    TOUT::append("\n    Reply: ");

    bool confirmed = SafetyGuard::ask_user_confirm(
        is_error ? ("Retry tool call? (y=retry, n=skip)") : ("Proceed with tool call? (y=yes, n=no)"), 60);

    if (confirmed) {
        result.action = ReplyAction::Yes;
    } else {
        result.action = ReplyAction::No;
    }

    return result;
}

} // namespace agent
