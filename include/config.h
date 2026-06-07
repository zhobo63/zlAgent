#pragma once

#include <string>
#include <map>
#include <vector>

namespace agent {

/**
 * Lightweight INI parser (no external dependencies).
 * Supports: [section] key = value, comments (# or ;), blank lines.
 */
class IniParser {
public:
    // Parse an INI file into a map of section -> key-value pairs.
    static std::map<std::string, std::map<std::string, std::string>> parse(const std::string& path);

private:
    IniParser() = default;
};

/**
 * Global configuration loaded from zlagent.ini (or defaults).
 */
struct Config {
    // ── LLM ────────────────────────────────────────────────
    struct LLM {
        std::string url       = "http://127.0.0.1:1234";
        double    temperature = 0.2;
        int       max_tokens  = 4096;
    } llm;

    // ── Memory ─────────────────────────────────────────────
    struct Memory {
        int max_messages = 50;
    } memory;

    // ── Agent ──────────────────────────────────────────────
    struct Agent_ {
        int max_iterations = 10;
        // "multi" (default) or a specific language: cpp, js, ts, python, rust, go, java
        std::string language = "multi";
        // Auto-detect language from source file extensions in current directory.
        bool auto_detect_language = true;
        // Optional: path to an external system prompt file (.md / .txt). Overrides built-in.
        std::string prompt_file = "";
    } agent_;

    // ── Features (advanced toggles) ────────────────────────
    struct Features {
        bool task_planning       = true;
        bool self_reflection     = true;
        bool multi_agent         = false;
        int  max_reflection_retries = 2;
    } features;

    // ── Plugins ────────────────────────────────────────────
    struct Plugins {
        std::string directory = "plugins";
    } plugins;

    // ── Local Tools ────────────────────────────────────────
    struct LocalTools {
        bool enabled = true;
    } local_tools;

    // Load from an INI file path. Returns false on error (keeps defaults).
    static Config load(const std::string& ini_path);

private:
    // Helper: parse a boolean string ("true"/"false", "1"/"0", "yes"/"no").
    static bool parse_bool(const std::string& s, bool default_val = false);
};

} // namespace agent
