#pragma once

#include <string>
#include <vector>

namespace agent {

/**
 * Result of a path safety check.
 *
 * - Allowed:           inside working directory or whitelist — auto-allow.
 * - NeedsConfirmation: outside both, and strict_mode is OFF — ask the user.
 * - Denied:            outside both, and strict_mode is ON — reject outright.
 */
enum class PathCheckResult {
    Allowed,
    NeedsConfirmation,
    Denied
};

/**
 * SafetyGuard provides four layers of protection:
 * 1. Dangerous tool confirmation — prompt user before destructive operations.
 * 2. Path whitelist + working directory — reject file operations outside allowed directories.
 * 3. Skill content check — detect suspicious shell commands in SKILL.md.
 * 4. Input filter — basic prompt injection detection on user input.
 */
class SafetyGuard {
public:
    // ── 1. Dangerous tool confirmation ─────────────────────

    // Ask the user to confirm a dangerous operation. Returns true if confirmed.
    bool confirm_dangerous_operation(const std::string& operation);

    // Check if a command string contains destructive patterns (rm -rf, del /f, etc.).
    static bool is_command_dangerous(const std::string& command);

    // ── 2. Path whitelist + working directory ──────────────

    // Set allowed directories. Empty = no restriction.
    void set_path_whitelist(const std::vector<std::string>& dirs);

    // Check if a path is within the allowed directories. Returns true if allowed.
    bool is_path_allowed(const std::string& path) const;

    // Get current whitelist (for logging).
    const std::vector<std::string>& get_path_whitelist() const;

    // Reset whitelist to empty — useful for tests.
    void reset_path_whitelist();

    // Set the working directory. Paths under this directory are always allowed.
    void set_working_directory(const std::string& dir);

    // Get current working directory.
    const std::string& get_working_directory() const;

    // Set strict mode.
    //   false (default) = ConfirmMode — ask user when path is outside both wd and whitelist.
    //   true            = RejectMode  — deny outright when path is outside both wd and whitelist.
    void set_strict_mode(bool strict);

    // Get current strict mode.
    bool get_strict_mode() const;

    // Integrated path check that considers working directory, whitelist, and strict mode.
    // In ConfirmMode (strict=false), this method will prompt the user if needed.
    PathCheckResult is_path_ok(const std::string& path);

    // ── 3. Skill content check ─────────────────────────────

    // Scan SKILL.md content for suspicious patterns. Returns warnings found.
    static std::vector<std::string> check_skill_content(const std::string& content);

    // ── 4. Input filter ────────────────────────────────────

    // Detect prompt injection attempts in user input. Returns true if suspicious.
    static bool is_prompt_injection(const std::string& input);

    // Singleton accessor — provides global access to the SafetyGuard instance.
    static SafetyGuard& get_instance();

    // Normalize a path (resolve .., convert separators).
    static std::string normalize_path(const std::string& path);

    // Check if path starts with any of the allowed directories.
    bool is_under_allowed_dir(const std::string& normalized_path) const;

    // Path whitelist — instance member to avoid global state.
    std::vector<std::string> path_whitelist_;

private:
    // Working directory — paths under this are always allowed.
    std::string working_directory_;

    // Strict mode: true = reject outright, false = ask user for out-of-scope paths.
    bool strict_mode_ = false;
};

} // namespace agent
