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

    // Update or add a single key in the given section. Creates the file if it doesn't exist.
    static bool update_key(const std::string& ini_path,
                           const std::string& section,
                           const std::string& key,
                           const std::string& value);

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
        std::string model     = "local";
        double    temperature = 0.2;
        int       max_tokens  = 16384;
    } llm;

    // ── Memory ─────────────────────────────────────
    struct Memory {
        int max_messages = 50;
        bool long_term_enabled = false;
        std::string store_dir = ".zlagent_memory";
        int max_sessions = 100;
        bool inject_facts_to_prompt = true;
        bool auto_extract_facts = true;
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
        // User reply mode: "off", "on_error", or "always"
        std::string user_reply_mode = "off";
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

    // ── Logging ──────────────────────────────────────────
    struct Logging {
        std::string level = "info";  // debug, info, warn, error, none
    } logging;

    // ── Safety ───────────────────────────────────────
    struct Safety {
        bool dangerous_tool_confirmation = true;  // prompt before destructive ops
        std::vector<std::string> path_whitelist;   // empty = no restriction
        std::string working_directory;              // current working directory (always allowed)
        bool skill_content_check = true;           // scan SKILL.md for suspicious patterns
        bool input_filter = true;                  // detect prompt injection in user input
    } safety;

    // ── RAG (Retrieval Augmented Generation) ─────────
    struct RAG {
        bool enabled = false;
        std::string embedding_backend = "tfidf";   // "lm_studio" or "tfidf"
        std::string embedding_model = "text-embedding-3-small";
        std::string store_path = "knowledge_base.json";
        int top_k = 5;
        float min_score = 0.3f;
        std::vector<std::string> knowledge_dirs;   // directories to ingest at startup
    } rag;

    // ── Terminal Command Direct Execution ─────────────
    struct TerminalCommands {
        bool enabled = true;                        // enable direct terminal command detection
        std::vector<std::string> direct_commands;   // high-confidence: execute without asking
        std::vector<std::string> confirm_commands;  // low-confidence: ask before executing
        bool ask_unknown = false;                   // prompt for commands not in either list
    } terminal_commands;

    // Load from an INI file path. Returns false on error (keeps defaults).
    static Config load(const std::string& ini_path);

    // Save the config to an INI file path.
    static bool save(const Config& cfg, const std::string& ini_path);

    // Helper: parse a boolean string ("true"/"false", "1"/"0", "yes"/"no").
    static bool parse_bool(const std::string& s, bool default_val = false);
};

} // namespace agent
