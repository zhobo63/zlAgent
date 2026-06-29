#include "pch.h"

#include "terminal_command_detector.h"
#include "logger.h"

namespace agent {

// ── Default command lists (used when config provides none) ───────────────

static const char* DEFAULT_DIRECT[] = {
    // Navigation / listing
    "ls", "dir", "cd", "pwd", "pushd", "popd",
    // File content
    "cat", "echo", "head", "tail", "wc", "less", "more",
    // System info
    "date", "whoami", "hostname", "uname", "uptime", "df", "du", "free",
    // Search / find
    "find", "grep", "locate", "which", "where",
    // Version control (read-only)
    "git status", "git log", "git diff", "git branch", "git tag", "git show",
    // Build tools (read-only or build only)
    "npm run", "cargo build", "make", "cmake", "mvn compile",
    // Language runners (non-destructive)
    "python", "node", "java", "go run", "ruby", "php", "perl",
};

static const char* DEFAULT_CONFIRM[] = {
    // Destructive file ops
    "rm", "del", "rmdir", "erase",
    // Process management
    "kill", "pkill", "xargs kill",
    // Container / service management
    "docker", "systemctl", "service",
    // Privilege escalation
    "sudo", "su",
    // File permission changes
    "chmod", "chown", "chgrp",
    // Network download (potential side effects)
    "wget", "curl",
    // Package managers (install = side effect)
    "apt install", "pip install", "npm install", "cargo install",
};

// ── Natural language exclusion prefixes ────────────────────────────────

//static const char* NATURAL_LANGUAGE_PREFIXES[] = {
//    "please", "can you", "could you", "would you", "help me",
//    "i want to", "i need to", "how do i", "how to", "what is",
//    "explain", "describe", "tell me", "show me how", "run for me",
//    "execute for me", "do this", "do that", "can we", "should i",
//};

// ── Implementation ─────────────────────────────────────────────────────

TerminalCommandDetector* TerminalCommandDetector::create(
    const std::vector<std::string>& direct_commands,
    const std::vector<std::string>& confirm_commands) {

    auto* detector = new TerminalCommandDetector();

    // Use provided lists or fall back to defaults.
    if (!direct_commands.empty()) {
        for (const auto& cmd : direct_commands) {
            detector->direct_commands_.insert(to_lower(cmd));
        }
    } else {
        for (const char* cmd : DEFAULT_DIRECT) {
            detector->direct_commands_.insert(cmd);
        }
    }

    if (!confirm_commands.empty()) {
        for (const auto& cmd : confirm_commands) {
            detector->confirm_commands_.insert(to_lower(cmd));
        }
    } else {
        for (const char* cmd : DEFAULT_CONFIRM) {
            detector->confirm_commands_.insert(cmd);
        }
    }

    LOG_INFO("TerminalCommandDetector", "Initialized with " +
             std::to_string(detector->direct_commands_.size()) + " direct commands, " +
             std::to_string(detector->confirm_commands_.size()) + " confirm commands");

    return detector;
}

std::string TerminalCommandDetector::get_first_token(const std::string& input) {
    // Skip leading whitespace.
    size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start])))
        ++start;

    if (start >= input.size()) return "";

    // Read until next whitespace or shell operator.
    size_t end = start;
    while (end < input.size() && !std::isspace(static_cast<unsigned char>(input[end])) &&
           input[end] != '|' && input[end] != '>' && input[end] != '<' &&
           input[end] != ';' && input[end] != '&') {
        ++end;
    }

    return input.substr(start, end - start);
}

bool TerminalCommandDetector::has_shell_operators(const std::string& input) const {
    // Check for shell metacharacters that indicate command composition.
    static const char operators[] = "|>&;<";
    for (char c : input) {
        if (std::strchr(operators, c)) return true;
    }
    // Also check for && and ||.
    if (input.find("&&") != std::string::npos) return true;
    if (input.find("||") != std::string::npos) return true;
    return false;
}

//bool TerminalCommandDetector::looks_like_natural_language(const std::string& input) const {
//    std::string lower = to_lower(input);
//
//    // 1. Check for natural language prefixes.
//    for (const char* prefix : NATURAL_LANGUAGE_PREFIXES) {
//        if (lower.find(prefix) == 0) return true;
//    }
//
//    // 2. Contains question mark or exclamation — likely a question/statement.
//    if (input.find('?') != std::string::npos || input.find('!') != std::string::npos)
//        return true;
//
//    // 3. Very long input with many spaces (>10 words) is probably descriptive text.
//    int word_count = 0, space_count = 0;
//    for (char c : input) {
//        if (std::isspace(static_cast<unsigned char>(c))) ++space_count;
//    }
//    if (space_count > 15) return true;
//
//    // 4. Contains common English articles at word boundaries that are unusual in commands.
//    static const char* articles[] = {" the ", " an ", " a "};
//    for (const char* article : articles) {
//        if (lower.find(article) != std::string::npos) return true;
//    }
//
//    // 5. Starts with a letter that is not typical of shell commands (long first word >12 chars).
//    std::string first = get_first_token(input);
//    if (first.size() > 12 && !has_shell_operators(input)) {
//        // Long first token without shell operators — likely natural language.
//        return true;
//    }
//
//    return false;
//}

CommandConfidence TerminalCommandDetector::detect(const std::string& input) const {
    if (input.empty()) return CommandConfidence::NotACommand;

    // Natural language exclusion — if it looks like a sentence, skip.
    //if (looks_like_natural_language(input)) {
    //    LOG_DEBUG("TerminalCommandDetector", "Rejected as natural language: " + input);
    //    return CommandConfidence::NotACommand;
    //}

    std::string first_token = to_lower(get_first_token(input));
    if (first_token.empty()) return CommandConfidence::NotACommand;

    // Check direct commands whitelist.
    if (direct_commands_.count(first_token)) {
        LOG_DEBUG("TerminalCommandDetector", "High confidence match: " + input);
        return CommandConfidence::High;
    }

    // Check confirm commands whitelist.
    if (confirm_commands_.count(first_token)) {
        LOG_DEBUG("TerminalCommandDetector", "Low confidence match: " + input);
        return CommandConfidence::Low;
    }

    // Unknown command — check for shell-like patterns to decide.
    if (has_shell_operators(input)) {
        // Shell operators suggest it's a command, but we're not sure about the base command.
        LOG_DEBUG("TerminalCommandDetector", "Shell operators detected: " + input);
        return CommandConfidence::Low;
    }

    // Short first token (<=8 chars) with arguments — might be a command.
    //if (first_token.size() <= 8 && !looks_like_natural_language(input)) {
    //    LOG_DEBUG("TerminalCommandDetector", "Possible unknown command: " + input);
    //    return CommandConfidence::Low;
    //}

    LOG_DEBUG("TerminalCommandDetector", "Not a terminal command: " + input);
    return CommandConfidence::NotACommand;
}

std::string TerminalCommandDetector::explain(const std::string& input) const {
    //if (looks_like_natural_language(input))
    //    return "Looks like natural language";

    std::string first_token = to_lower(get_first_token(input));

    if (direct_commands_.count(first_token))
        return "Known safe command: " + first_token;

    if (confirm_commands_.count(first_token))
        return "Potentially dangerous command: " + first_token;

    if (has_shell_operators(input))
        return "Contains shell operators";

    if (first_token.size() <= 8)
        return "Unknown short command: " + first_token;

    return "Does not match terminal command patterns";
}

std::string TerminalCommandDetector::to_lower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

bool TerminalCommandDetector::execute_directly(const std::string& command, std::string& response) {
    LOG_INFO("Terminal", "Executing directly: " + command);

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        auto err_msg = "Failed to execute command:" + command;
        LOG_ERROR("Terminal", err_msg);
        response = err_msg;
        return false;
    }

    char buffer[4096] = { 0 };
    while (fgets(buffer, sizeof(buffer), pipe)) {
        std::cout << buffer;
        response += buffer;
    }

    int status = pclose(pipe);
    if (status != 0) {
        auto warn_msg = "Command failed with exit code " + std::to_string(status);
        LOG_WARN("Terminal", warn_msg);
        response += "\n" + warn_msg;
        return false;
    }

    return true;
}

} // namespace agent
