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

// ── Detect the current OS name at runtime ────────────────────────────────────
static std::string get_os_name() {
#if defined(_WIN32)
    // On Windows, use environment variables to distinguish:
    //   MSYSTEM  → MinGW/MSYS (e.g. "MINGW64", "MINGW32")
    //   CYGWIN   → Cygwin
    //   neither  → native Windows (MSVC / Visual Studio)
    {
        const char* msys = getenv("MSYSTEM");
        if (msys && *msys) return "MinGW";
    }
    {
        const char* cygwin = getenv("CYGWIN");
        if (cygwin && *cygwin) return "Cygwin";
    }
    return "Windows";
#elif defined(__linux__)
    return "Linux";
#elif defined(__APPLE__)
    return "macOS";
#else
    return "Unknown";
#endif
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

std::string SystemPromptProvider::get() {
    // 1. Try loading system_prompt.md first.
    std::string md_content;

    if (try_load_md(&md_content)) {
        LOG_INFO("SystemPrompt", "Loaded from system_prompt.md");
    } else {
        // Fall back to hardcoded prompt.
        LOG_WARN("SystemPrompt", "system_prompt.md not found, using hardcoded fallback");
        md_content = hardcoded_fallback();
    }

    // Also try to load AGENTS.md and append it.
    std::string agents_content;
    if (try_load_agents_md(&agents_content)) {
        LOG_INFO("SystemPrompt", "Loaded from AGENTS.md, merging...");
        md_content += agents_content;
    } else {
        LOG_INFO("SystemPrompt", "AGENTS.md not found, using system_prompt.md only");
    }

    // Append OS context as a dynamic section.
    std::string os_name = get_os_name();
    if (!os_name.empty() && os_name != "Unknown") {
        md_content += "- Operating System: " + os_name + "\n";
        LOG_INFO("SystemPromptProvider", "Operating System: " + os_name);
    }

    return md_content;
}

// ── Hardcoded fallback ──────────────────────────────────────────────────────

static std::string hardcoded_fallback() {
    return u8R"(You are ZL Agent, an expert multi-language code assistant with access to filesystem tools.

**IMPORTANT: Always call the appropriate tool instead of making assumptions or pretending you can do things directly.**

Guidelines:
1. Always list the directory and read existing files before modifying them — use the tools for this, do not guess
2. Write clean, idiomatic code following each language's best practices
3. Explain your changes concisely

Language-specific notes:
- C++: Use modern C++ (C++17/20), prefer smart pointers over raw ownership, strings contain UTF-8 use `u8` prefix
- JavaScript: Prefer ES modules, use const/let, avoid var
- TypeScript: Leverage strict mode, proper types, no any
- Python: Follow PEP 8, use type hints where helpful
- Rust: Use idiomatic patterns (Result, Option, lifetimes), run clippy
- Go: Follow gofmt conventions
- Java: Follow Google Java Style, prefer records/streams in modern Java)";
}

} // namespace agent
