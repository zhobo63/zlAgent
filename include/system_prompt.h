#pragma once

#include <string>

namespace agent {

/**
 * Provides built-in system prompts for different programming languages.
 * Returns a multi-language prompt by default; specific language prompts when requested.
 */
class SystemPromptProvider {
public:
    // Get the built-in system prompt for the given language identifier.
    // "multi" (default) → universal C++/JS/TS/Python/Rust/Go/Java prompt.
    // "cpp", "js", "ts", "python", "rust", "go", "java" → language-specific.
    static std::string get(const std::string& language = "multi");

private:
    SystemPromptProvider() = default;
};

} // namespace agent
