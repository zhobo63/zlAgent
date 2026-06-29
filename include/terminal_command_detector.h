#pragma once

#include <string>
#include <set>
#include <vector>

namespace agent {

/**
 * Confidence level for terminal command detection.
 */
enum class CommandConfidence {
    NotACommand,        // Definitely not a shell command
    High,               // High confidence — execute directly
    Low                 // Low confidence — ask user before executing
};

/**
 * Detects whether user input is a terminal/shell command that should be
 * executed directly without going through the LLM.
 *
 * Uses heuristic rules:
 * - Known command whitelist (high/low confidence)
 * - Shell operator detection (pipes, redirects)
 * - Natural language exclusion patterns
 */
class TerminalCommandDetector {
public:
    // Create from config. Takes ownership of the detector via raw pointer pattern
    // consistent with other global singletons in this codebase.
    static TerminalCommandDetector* create(const std::vector<std::string>& direct_commands,
                                           const std::vector<std::string>& confirm_commands);

    /**
     * Detect whether input is a terminal command and return confidence level.
     */
    CommandConfidence detect(const std::string& input) const;

    /**
     * Return a human-readable explanation of the detection result for logging/display.
     */
    std::string explain(const std::string& input) const;

    // ── Standalone execution (no LLM involved) ────────────────

    /**
     * Execute a shell command directly and print the output to stdout.
     * Returns true if the command succeeded (exit code 0).
     * If response is non-null, the command output is also written there.
     */
    static bool execute_directly(const std::string& command, std::string& response);

private:
    TerminalCommandDetector() = default;

    std::set<std::string> direct_commands_;   // high-confidence commands
    std::set<std::string> confirm_commands_;  // low-confidence commands

    // Extract the first token (command name) from input.
    static std::string get_first_token(const std::string& input);

    // Check if input contains shell operators like |, >, >>, <, ;, &&, ||
    bool has_shell_operators(const std::string& input) const;

    // Check if input looks like natural language rather than a command.
    //bool looks_like_natural_language(const std::string& input) const;

    // Convert string to lowercase for case-insensitive comparison.
    static std::string to_lower(const std::string& s);
};

} // namespace agent
