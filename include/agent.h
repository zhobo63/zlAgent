#pragma once

#include <string>
#include "llm_client.h"
#include "tool.h"
#include "memory.h"

namespace agent {

/**
 * The core Agent that runs the reasoning loop:
 *   1. Send conversation + tools to LLM
 *   2. If tool calls returned, execute them and feed results back
 *   3. Repeat until no more tool calls (max iterations safety limit)
 *   4. Return final assistant message
 */
class Agent {
public:
    explicit Agent(const std::string& llm_url = "http://127.0.0.1:1234");

    // Register a tool the agent can use
    void add_tool(ToolPtr tool);

    // Set the system prompt (role, constraints, etc.)
    void set_system_prompt(const std::string& prompt);

    // Main entry: send user message and get response (non-streaming)
    std::string run(const std::string& user_input);

    // Streaming version: on_token is called for each token chunk as it arrives.
    // Returns the final response content after streaming completes.
    std::string run_stream(
        const std::string& user_input,
        TokenCallback on_token);

private:
    LLMClient llm_;
    ToolRegistry registry_;
    Memory memory_;

    int max_iterations_ = 10;  // safety limit for tool-call loops

    // Internal loop: call LLM, execute tools, repeat (non-streaming)
    ChatResponse reasoning_loop();

    // Internal streaming loop: same logic but uses chat_stream with token callback
    ChatResponse reasoning_loop_stream(TokenCallback on_token);
};

} // namespace agent
