#include "pch.h"

#include "safety_guard.h"
#include "logger.h"
#include "key_watcher.h"
#include "agent.h"
#include <cstring>
#include <regex>

namespace agent {

SafetyGuard& SafetyGuard::get_instance() {
    static SafetyGuard instance;
    return instance;
}

// Case-insensitive substring search — no copy of the haystack.
static bool contains_ci(const std::string& haystack, const char* needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0 || haystack.size() < nlen) return false;
    for (size_t i = 0; i <= haystack.size() - nlen; ++i) {
        bool match = true;
        for (size_t j = 0; j < nlen; ++j) {
            if (::tolower(static_cast<unsigned char>(haystack[i + j]))
                != ::tolower(static_cast<unsigned char>(needle[j]))) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

// ============================================================================
// 1. Dangerous tool confirmation
// ============================================================================

bool SafetyGuard::is_command_dangerous(const std::string& command) {
    // Patterns that indicate destructive operations.
    static const char* dangerous_patterns[] = {
        "rm -rf", "rm -r ", "rm -f ",
        "del /f", "del /s", "rd /s", "rmdir /s",
        "format ", "mkfs",
        "> /dev/", ">> /dev/",
        ":(){:|:&};:",   // fork bomb
    };

    for (const char* pat : dangerous_patterns) {
        if (contains_ci(command, pat)) return true;
    }
    return false;
}

bool SafetyGuard::confirm_dangerous_operation(const std::string& operation) {
    LOG_WARN("Safety", "Dangerous operation detected: " + operation);
    TOUT::cout << u8"⚠  Dangerous operation: " << operation << "\n";
    return ask_user_confirm("Confirm this dangerous operation?", 60);
}

bool SafetyGuard::ask_user_confirm(const std::string& message, int timeout_seconds) {
    // Check if we are running inside a SubAgentNet client.
    Agent* g_agent = get_global_agent();
    if (g_agent) {
        std::shared_ptr<SubAgentNet> sub_agent = g_agent->get_sub_agent();
        if (sub_agent && sub_agent->is_connected())
            return sub_agent->ask_confirm(message, timeout_seconds);
    }

    // Local confirmation via KeyWatcher.
    TOUT::cout << "   " << message << "\n";
    TOUT::cout << "   Type 'y' to confirm, anything else to cancel: ";

    auto k = KeyWatcher::read_key();
    char ch = 0;
    if (k.size > 0) ch = static_cast<char>(k.code[0]);

    std::string lower(1, ::tolower(static_cast<unsigned char>(ch)));
    return (lower == "y");
}

// ============================================================================
// 2. Path whitelist
// ============================================================================

void SafetyGuard::set_path_whitelist(const std::vector<std::string>& dirs) {
    path_whitelist_.clear();
    for (const auto& d : dirs) {
        path_whitelist_.push_back(normalize_path(d));
    }
}

const std::vector<std::string>& SafetyGuard::get_path_whitelist() const {
    return path_whitelist_;
}

void SafetyGuard::reset_path_whitelist() {
    path_whitelist_.clear();
}

void SafetyGuard::set_working_directory(const std::string& dir) {
    working_directory_ = normalize_path(dir);
}

const std::string& SafetyGuard::get_working_directory() const {
    return working_directory_;
}

void SafetyGuard::set_strict_mode(bool strict) {
    strict_mode_ = strict;
}

bool SafetyGuard::get_strict_mode() const {
    return strict_mode_;
}

PathCheckResult SafetyGuard::is_path_ok(const std::string& path) {
    // Empty whitelist AND no working directory → allow everything (legacy behavior).
    if (path_whitelist_.empty() && working_directory_.empty())
        return PathCheckResult::Allowed;

    std::string norm = normalize_path(path);

    // Resolve relative paths against the working directory so that
    // "multi-agent.md" becomes "F:/hg/zlagent/multi-agent.md" before comparison.
    if (!working_directory_.empty() && !norm.empty() && norm[0] != '/'
#ifdef _WIN32
        && !(norm.size() >= 2 && norm[1] == ':')
#endif
    ) {
        // Relative path — prepend working directory.
        std::string resolved = working_directory_ + "/" + norm;
        norm = normalize_path(resolved);
    }

    // Inside working directory → auto-allow.
    if (!working_directory_.empty()
        && norm.compare(0, working_directory_.size(), working_directory_) == 0)
    {
        return PathCheckResult::Allowed;
    }

    // Inside whitelist → auto-allow.
    if (is_under_allowed_dir(norm))
        return PathCheckResult::Allowed;

    // Outside both — decision depends on strict mode.
    if (strict_mode_) {
        LOG_WARN("Safety", "Path outside working directory and whitelist: " + path);
        return PathCheckResult::Denied;
    }

    // ConfirmMode: ask the user.
    bool confirmed = SafetyGuard::ask_user_confirm(
        "[Safety] Path outside working directory/whitelist: " + path, 60);

    if (confirmed) {
        // Remember this path so we don't ask again.
        // Add the parent directory to the whitelist rather than the file itself,
        // so that sibling files under the same directory are also allowed.
        std::string parent = norm;
        auto last_slash = parent.find_last_of('/');
        if (last_slash != std::string::npos && last_slash > 0)
            parent = parent.substr(0, last_slash);

        // Avoid duplicates — only add if not already covered.
        bool already_allowed = false;
        for (const auto& wd : path_whitelist_) {
            if (parent.compare(0, wd.size(), wd) == 0) {
                already_allowed = true;
                break;
            }
        }
        if (!already_allowed)
            path_whitelist_.push_back(parent);

        return PathCheckResult::Allowed;
    }

    return PathCheckResult::Denied;
}

bool SafetyGuard::is_path_allowed(const std::string& path) const {
    // Empty whitelist = no restriction.
    if (path_whitelist_.empty()) return true;

    std::string norm = normalize_path(path);
    return is_under_allowed_dir(norm);
}

std::string SafetyGuard::normalize_path(const std::string& path) {
    // Convert backslashes to forward slashes for uniform comparison.
    std::string result = path;
    std::replace(result.begin(), result.end(), '\\', '/');

    // Remove trailing slash (but keep root "/").
    while (result.size() > 1 && result.back() == '/') {
        result.pop_back();
    }

    return result;
}

bool SafetyGuard::is_under_allowed_dir(const std::string& normalized_path) const {
    for (const auto& allowed : path_whitelist_) {
        // Check if the path starts with the allowed directory.
        if (normalized_path.compare(0, allowed.size(), allowed) == 0) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// 3. Skill content check
// ============================================================================

std::vector<std::string> SafetyGuard::check_skill_content(const std::string& content) {
    std::vector<std::string> warnings;

    // Suspicious patterns in SKILL.md instructions.
    struct PatternCheck {
        const char* pattern;
        const char* warning;
    };

    static const PatternCheck checks[] = {
        {"rm -rf /",         "!  Detected destructive command: 'rm -rf /'"},
        {"rm -rf /*",        "!  Detected destructive command: 'rm -rf /*'"},
        {"curl.*\\|.*bash",  "!  Detected pipe-to-shell pattern (potential malware delivery)"},
        {"wget.*\\|.*sh",    "!  Detected wget-pipe-to-shell pattern"},
        {"eval(",            "!  Detected 'eval()' - arbitrary code execution risk"},
        {":(){:|:&};:",      "!  Detected fork bomb pattern"},
        {"chmod 777",        "!  Detected 'chmod 777' - overly permissive permissions"},
        {"mkfs",             "!  Detected filesystem formatting command"},
        {"> /dev/",          "!  Detected write to device file"},
    };

    for (const auto& check : checks) {
        // Case-insensitive substring match — no copy of the haystack.
        if (contains_ci(content, check.pattern)) {
            warnings.push_back(check.warning);
        }
    }

    return warnings;
}

// ============================================================================
// 4. Input filter - prompt injection detection
// ============================================================================

bool SafetyGuard::is_prompt_injection(const std::string& input) {
    // Common prompt injection keywords/phrases.
    static const char* injection_patterns[] = {
        "[system]",
        "ignore previous instructions",
        "ignore all previous",
        "disregard above",
        "forget your instructions",
        "you are now",
        "override system",
        "bypass safety",
        "jailbreak",
        "dan mode",
        "developer mode",
        "act as if you were",
    };

    for (const char* pat : injection_patterns) {
        if (contains_ci(input, pat)) return true;
    }
    return false;
}

} // namespace agent
