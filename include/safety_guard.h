#pragma once

#include <string>
#include <vector>

namespace agent {

/**
 * SafetyGuard provides four layers of protection:
 * 1. Dangerous tool confirmation — prompt user before destructive operations.
 * 2. Path whitelist — reject file operations outside allowed directories.
 * 3. Skill content check — detect suspicious shell commands in SKILL.md.
 * 4. Input filter — basic prompt injection detection on user input.
 */
class SafetyGuard {
public:
    // ── 1. Dangerous tool confirmation ─────────────────────

    // Ask the user to confirm a dangerous operation. Returns true if confirmed.
    static bool confirm_dangerous_operation(const std::string& operation);

    // Check if a command string contains destructive patterns (rm -rf, del /f, etc.).
    static bool is_command_dangerous(const std::string& command);

    // ── 2. Path whitelist ──────────────────────────────────

    // Set allowed directories. Empty = no restriction.
    static void set_path_whitelist(const std::vector<std::string>& dirs);

    // Check if a path is within the allowed directories. Returns true if allowed.
    static bool is_path_allowed(const std::string& path);

    // Get current whitelist (for logging).
    static const std::vector<std::string>& get_path_whitelist();

    // ── 3. Skill content check ─────────────────────────────

    // Scan SKILL.md content for suspicious patterns. Returns warnings found.
    static std::vector<std::string> check_skill_content(const std::string& content);

    // ── 4. Input filter ────────────────────────────────────

    // Detect prompt injection attempts in user input. Returns true if suspicious.
    static bool is_prompt_injection(const std::string& input);

private:
    SafetyGuard() = default;

    // Normalize a path (resolve .., convert separators).
    static std::string normalize_path(const std::string& path);

    // Check if path starts with any of the allowed directories.
    static bool is_under_allowed_dir(const std::string& normalized_path);
};

} // namespace agent
