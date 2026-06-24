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
static bool try_load_json(const std::string& language, json* root_out) {
    // Search locations: current working directory first, then next to the source file.
    static const char* candidates[] = {
        "system_prompt.json",
        "./system_prompt.json",
    };

    for (const auto* path : candidates) {
        std::ifstream f(path);
        if (!f.is_open()) continue;

        try {
            json root = json::parse(f);
            if (root.contains("languages") && root["languages"].contains(language)) {
                *root_out = root["languages"][language];
                return true;
            }
        } catch (const std::exception& e) {
            LOG_ERROR("SystemPrompt", "JSON parse error: " + std::string(e.what()));
        }
    }

    return false;
}

// ── Hardcoded fallback prompts (identical to the original implementation) ────
static std::string hardcoded_prompt(const std::string& language);

std::string SystemPromptProvider::get(const std::string& language) {
    // Try loading from system_prompt.json first.
    json lang_entry;

    if (try_load_json(language, &lang_entry)) {
        return build_prompt(lang_entry);
    }

    // Fall back to hardcoded prompts.
    return hardcoded_prompt(language);
}

// ── Hardcoded fallbacks ─────────────────────────────────────────────────────

static std::string hardcoded_prompt(const std::string& language) {
    // NOTE: Tool names/descriptions are NOT listed here — they are already sent
    // via the OpenAI-compatible 'tools' field with full JSON Schema. Listing them
    // in the system prompt wastes tokens.
    return u8R"(You are ZL Agent, an expert multi-language code assistant. You can work with C++, JavaScript, TypeScript, Python, Rust, Go, Java, HTML/CSS and more.

Guidelines:
1. Always list the directory and read existing files before modifying them
2. Prefer edit_file for targeted modifications; use write_file only for small new files. For large files, break into multiple edit_file calls to avoid token truncation
3. Write clean, idiomatic code following each language's best practices
4. Compile/build and test your code after writing it
5. Explain your changes concisely
6. If compilation fails, analyze errors and fix them iteratively
7. Check current directory and use relative paths for all file operations

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
