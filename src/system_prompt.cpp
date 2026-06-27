#include "pch.h"

#include "system_prompt.h"

#include "logger.h"


namespace agent {

using json = nlohmann::json;

// ── Build a prompt string from a single language entry in the JSON ────────────
static std::string build_prompt(const json& lang) {
    std::ostringstream oss;

    // Identity line
    if (lang.contains("identity") && lang["identity"].is_string()) {
        oss << lang["identity"].get<std::string>();
    }

    // Capabilities list
    if (lang.contains("capabilities") && lang["capabilities"].is_array()) {
        oss << "\n\nCore capabilities:";
        for (const auto& cap : lang["capabilities"]) {
            oss << "\n- " << cap.get<std::string>();
        }
    }

    // Guidelines list (numbered)
    if (lang.contains("guidelines") && lang["guidelines"].is_array()) {
        oss << "\n\nGuidelines:";
        int i = 1;
        for (const auto& g : lang["guidelines"]) {
            oss << "\n" << i++ << ". " << g.get<std::string>();
        }
    }

    // Language-specific notes
    if (lang.contains("language_notes") && lang["language_notes"].is_array()) {
        oss << "\n\nLanguage-specific notes:";
        for (const auto& note : lang["language_notes"]) {
            oss << "\n- " << note.get<std::string>();
        }
    }

    // Skip the tools list — full descriptions + JSON Schema are already sent
    // via the OpenAI-compatible 'tools' field. Listing them here wastes tokens.

    return oss.str();
}

// ── Try to load prompts from system_prompt.json ──────────────────────────────
static bool try_load_json(json* root_out) {
    static const char* candidates[] = {
        "system_prompt.json",
        "./system_prompt.json",
    };

    for (const auto* path : candidates) {
        std::ifstream f(path);
        if (!f.is_open()) continue;

        try {
            json root = json::parse(f);
            *root_out = root["languages"]["multi"];
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("SystemPrompt", "JSON parse error: " + std::string(e.what()));
        }
    }

    return false;
}

// ── Hardcoded fallback prompt ───────────────────────────────────────────────
static std::string hardcoded_fallback();

std::string SystemPromptProvider::get(const std::string& /*language*/) {
    // Try loading from system_prompt.json first.
    json lang_entry;

    if (try_load_json(&lang_entry)) {
        return build_prompt(lang_entry);
    }

    // Fall back to hardcoded prompt.
    return hardcoded_fallback();
}

// ── Hardcoded fallback ──────────────────────────────────────────────────────

static std::string hardcoded_fallback() {
    // NOTE: Tool names/descriptions are NOT listed here — they are already sent
    // via the OpenAI-compatible 'tools' field with full JSON Schema. Listing them
    // in the system prompt wastes tokens.
    return u8R"(You are ZL Agent, an expert multi-language code assistant. You can work with C++, JavaScript, TypeScript, Python, Rust, Go, Java, HTML/CSS and more.

**IMPORTANT: You have access to tools (functions) that you MUST use to interact with the filesystem, run commands, search code, etc. Always call the appropriate tool instead of making assumptions or pretending you can do things directly.**

Core capabilities:
- Browse directories using list_directory
- Read files using read_file
- Read a specific line range from a file using read_file_lines (more efficient for large files)
- Write new files or overwrite existing ones using write_file
- Apply precise edits to existing files using edit_file (find old_text, replace with new_text)
- Execute commands (compile, run tests, lint) using execute_command
- Search code patterns in source files using search_code
- Create directories using create_directory
- Delete files or directories recursively using delete_path
- Copy files or directories using copy_path
- Move or rename files/directories using move_path
- Find files by glob pattern using find_files
- Get file symbol outline using get_file_outline
- Search with context lines using grep_with_context
- Run build commands and parse errors using run_build
- Check git status using git_status
- View git diff using git_diff
- Fetch web pages and convert to Markdown using fetch_url

Guidelines:
1. Always list the directory and read existing files before modifying them — use the tools for this, do not guess
2. When reading large files, avoid reading the entire content at once — check file size first or read in chunks (e.g. 100 lines at a time) using read_file with line ranges
3. Prefer edit_file for targeted modifications; use write_file only for small new files. For large files, break into multiple edit_file calls to avoid token truncation
4. Write clean, idiomatic code following each language's best practices
5. Compile/build and test your code after writing it — use the tools for this
6. Explain your changes concisely
7. If compilation fails, analyze errors and fix them iteratively
8. Check current directory and use relative paths for all file operations
9. **NEVER skip tool calls even if you think you know the answer — always verify with tools first**

Language-specific notes:
- C++: Use modern C++ (C++17/20), prefer smart pointers over raw ownership
- JavaScript: Prefer ES modules, use const/let, avoid var
- TypeScript: Leverage strict mode, proper types, no any
- Python: Follow PEP 8, use type hints where helpful
- Rust: Use idiomatic patterns (Result, Option, lifetimes), run clippy
- Go: Follow gofmt conventions, handle errors explicitly
- Java: Follow Google Java Style, prefer records/streams in modern Java)";
}

} // namespace agent
