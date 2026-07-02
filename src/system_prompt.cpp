#include "pch.h"

#include "system_prompt.h"

#include "logger.h"


namespace agent {

// ── Read an entire file into a string ────────────────────────────────────────
static std::string read_file_content(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};

    std::ostringstream oss;
    oss << f.rdbuf();
    return oss.str();
}

// ── Try to load system_prompt.md ─────────────────────────────────────────────
static bool try_load_md(std::string* out) {
    static const char* candidates[] = {
        "system_prompt.md",
        "./system_prompt.md",
    };

    for (const auto* path : candidates) {
        std::string content = read_file_content(path);
        if (!content.empty()) {
            *out = std::move(content);
            return true;
        }
    }

    return false;
}

// ── Try to load AGENTS.md ────────────────────────────────────────────────────
static bool try_load_agents_md(std::string* out) {
    static const char* candidates[] = {
        "AGENTS.md",
        "./AGENTS.md",
    };

    for (const auto* path : candidates) {
        std::string content = read_file_content(path);
        if (!content.empty()) {
            *out = std::move(content);
            return true;
        }
    }

    return false;
}

// ── Hardcoded fallback prompt ───────────────────────────────────────────────
static std::string hardcoded_fallback();

std::string SystemPromptProvider::get(const std::string& /*language*/) {
    // 1. Try loading system_prompt.md first.
    std::string md_content;

    if (try_load_md(&md_content)) {
        LOG_INFO("SystemPrompt", "Loaded from system_prompt.md");

        // Also try to load AGENTS.md and append it.
        std::string agents_content;
        if (try_load_agents_md(&agents_content)) {
            LOG_INFO("SystemPrompt", "Loaded from AGENTS.md, merging...");
            md_content += "\n\n## Additional guidelines (from AGENTS.md)\n\n";
            md_content += agents_content;
        } else {
            LOG_INFO("SystemPrompt", "AGENTS.md not found, using system_prompt.md only");
        }

        return md_content;
    }

    // Fall back to hardcoded prompt.
    LOG_WARN("SystemPrompt", "system_prompt.md not found, using hardcoded fallback");
    return hardcoded_fallback();
}

// ── Hardcoded fallback ──────────────────────────────────────────────────────

static std::string hardcoded_fallback() {
    return u8R"(You are ZL Agent, an expert multi-language code assistant with access to filesystem tools.

**IMPORTANT: Always call the appropriate tool instead of making assumptions or pretending you can do things directly.**

Guidelines:
1. Always list the directory and read existing files before modifying them — use the tools for this, do not guess
2. Write clean, idiomatic code following each language's best practices
3. Compile/build and test your code after writing it; if compilation fails, analyze errors and fix iteratively
4. Explain your changes concisely
5. C++ source file use UTF8 BOM, all string not ascii use `u8`

Language-specific notes:
- C++: Use modern C++ (C++17/20), prefer smart pointers over raw ownership
- JavaScript: Prefer ES modules, use const/let, avoid var
- TypeScript: Leverage strict mode, proper types, no any
- Python: Follow PEP 8, use type hints where helpful
- Rust: Use idiomatic patterns (Result, Option, lifetimes), run clippy
- Go: Follow gofmt conventions
- Java: Follow Google Java Style, prefer records/streams in modern Java)";
}

} // namespace agent
