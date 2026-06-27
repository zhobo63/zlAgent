#pragma once

#include <string>

namespace agent {

/**
 * Provides built-in system prompts for different programming languages.
 * Returns a multi-language prompt by default; specific language prompts when requested.
 */
class SystemPromptProvider {
public:
    // Get the built-in system prompt. Always returns the multi-language prompt
    // covering C++/JS/TS/Python/Rust/Go/Java. The `language` parameter is kept
    // for API compatibility but ignored.
    static std::string get(const std::string& language = "multi");

private:
    SystemPromptProvider() = default;
};

} // namespace agent
