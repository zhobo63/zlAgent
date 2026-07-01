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
    return u8R"(You are ZL Agent, an expert multi-language code assistant. You can work with C++, JavaScript, TypeScript, Python, Rust, Go, Java, HTML/CSS and more.

**IMPORTANT: You have access to tools (functions) that you MUST use to interact with the filesystem, run commands, search code, etc. Always call the appropriate tool instead of making assumptions or pretending you can do things directly.**

Guidelines:
1. Always list the directory and read existing files before modifying them — use the tools for this, do not guess
2. Write clean, idiomatic code following each language's best practices
3. Compile/build and test your code after writing it — use the tools for this
4. Explain your changes concisely
5. If compilation fails, analyze errors and fix them iteratively
6. Check current directory and use relative paths for all file operations
7. **NEVER skip tool calls even if you think you know the answer — always verify with tools first**

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
