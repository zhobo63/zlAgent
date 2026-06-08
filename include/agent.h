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
 *
 * Advanced features (optional):
 * - Task Planning: break complex tasks into sub-steps before execution
 * - Self-Reflection: review output quality and auto-correct errors
 * - Multi-Agent: delegate steps to specialized sub-agents (Coder/Reviewer/Tester)
 */
class Agent {
public:
    explicit Agent(const std::string& llm_url = "http://127.0.0.1:1234");

    // Register a tool the agent can use
    void add_tool(ToolPtr tool);

    // Get all registered tool names (for dependency validation)
    std::vector<std::string> get_tool_names() const;

    // Set the system prompt (role, constraints, etc.)
    void set_system_prompt(const std::string& prompt);

    // Main entry: send user message and get response (non-streaming)
    std::string run(const std::string& user_input);

    // Streaming version: on_token is called for each token chunk as it arrives.
    // Returns the final response content after streaming completes.
    std::string run_stream(
        const std::string& user_input,
        TokenCallback on_token);

    // ── Advanced feature toggles ────────────────────────────

    // Task Planning: break complex tasks into sub-steps (default: true)
    void set_task_planning(bool enabled) { task_planning_ = enabled; }
    bool task_planning_enabled() const { return task_planning_; }

    // Self-Reflection: review output quality and auto-correct (default: true)
    void set_self_reflection(bool enabled) { self_reflection_ = enabled; }
    bool self_reflection_enabled() const { return self_reflection_; }

    // Multi-Agent: use specialized sub-agents for step execution (default: false)
    void set_multi_agent(bool enabled) { multi_agent_ = enabled; }
    bool multi_agent_enabled() const { return multi_agent_; }

    // Maximum reflection retries per step (default: 2)
    void set_max_reflection_retries(int n) { max_reflection_retries_ = n; }

    // Accessors for internal components (used by long-term memory integration).
    Memory& get_memory() { return memory_; }
    const Memory& get_memory() const { return memory_; }
    LLMClient& get_llm() { return llm_; }
    const LLMClient& get_llm() const { return llm_; }

private:
    LLMClient llm_;
    ToolRegistry registry_;
    Memory memory_;

    int max_iterations_ = 10;  // safety limit for tool-call loops

    // Advanced feature flags
    bool task_planning_   = true;
    bool self_reflection_ = true;
    bool multi_agent_     = false;
    int  max_reflection_retries_ = 2;

    // Internal loop: call LLM, execute tools, repeat (non-streaming)
    ChatResponse reasoning_loop();

    // Internal streaming loop: same logic but uses chat_stream with token callback
    ChatResponse reasoning_loop_stream(TokenCallback on_token);

    // ── Advanced pipeline ───────────────────────────────────

    // Full pipeline: plan → execute steps (with reflection + multi-agent) → assemble result.
    std::string run_planned(const std::string& user_input, TokenCallback on_token);
};

} // namespace agent
