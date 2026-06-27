#include "pch.h"

#include "safety_guard.h"
#include "logger.h"
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
    std::cout << "   Type 'y' to confirm, anything else to cancel: ";

    std::string response;
    if (!std::getline(std::cin, response)) return false;

    // Trim whitespace.
    auto trim = [](std::string& s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](char c){ return !std::isspace(c); }));
        s.erase(std::find_if(s.rbegin(), s.rend(), [](char c){ return !std::isspace(c); }).base(), s.end());
    };
    trim(response);

    std::string lower = response;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return (lower == "y" || lower == "yes");
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
