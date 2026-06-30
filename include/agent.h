#pragma once

#include <string>
#include "llm_client.h"
#include "tool.h"
#include "memory.h"
#include "user_reply.h"

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
    Agent(const std::string& llm_url = "http://127.0.0.1:1234",
          const std::string& model = "local");

    // Register a tool the agent can use
    void add_tool(ToolPtr tool);

    // Get all registered tool names (for dependency validation)
    std::vector<std::string> get_tool_names() const;

    // Get all registered tools (for listing / inspection)
    std::vector<ToolPtr> get_tools() const;

    // Set the system prompt (role, constraints, etc.)
    void set_system_prompt(const std::string& prompt);

    // Streaming version: on_token is called for each token chunk as it arrives.
    // Returns the final response content after streaming completes.
    // If usage_out is provided, it will be filled with cumulative token counts.
    std::string run_stream(
        const std::string& user_input,
        TokenCallback on_token,
        ChatResponse* usage_out = nullptr);

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

    // Maximum iterations for the reasoning loop (default: 10)
    void set_max_iterations(int n) { max_iterations_ = n; }

    // Maximum reflection retries per step (default: 2)
    void set_max_reflection_retries(int n) { max_reflection_retries_ = n; }

    // Accessors for internal components (used by long-term memory integration).
    Memory& get_memory() { return memory_; }
    const Memory& get_memory() const { return memory_; }
    LLMClient& get_llm() { return llm_; }
    const LLMClient& get_llm() const { return llm_; }

    // Switch the LLM model at runtime.
    void set_llm_model(const std::string& model) { llm_.set_model(model); }

    // Enable lazy local tool discovery (default: true).
    // When enabled, local tools are discovered on first chat instead of at startup.
    void set_lazy_local_tools(bool enabled) { lazy_local_tools_ = enabled; }

    // Set whether local tool discovery is allowed (from config).
    void set_local_tools_enabled(bool enabled) { local_tools_enabled_ = enabled; }

    // User Reply: allow user intervention during reasoning loop.
    void set_user_reply_mode(UserReplyMode mode) { user_reply_mode_ = mode; }
    UserReplyMode get_user_reply_mode() const { return user_reply_mode_; }

	void reset_iteration_count() { current_iteration_ = 0; }
	void reset_tokens_used() { tokens_used_ = 0; }
	int get_current_iteration() const { return current_iteration_; }
	int get_max_iterations() const { return max_iterations_; }
	int get_tokens_used() const { return tokens_used_; }
    int get_max_token() const {return max_tokens_; }
private:
    LLMClient llm_;
    ToolRegistry registry_;
    Memory memory_;

	int current_iteration_ = 0;     // tracks reasoning loop iterations
    int max_iterations_ = 10;       // safety limit for tool-call loops
	int tokens_used_ = 0;          // tracks tokens used in reasoning loop
	int max_tokens_ = 8192;         // default max tokens for LLM calls

    // Advanced feature flags
    bool task_planning_   = true;
    bool self_reflection_ = true;
    bool multi_agent_     = false;

    // Lazy local tool discovery — discover on first chat instead of at startup.
    bool local_tools_enabled_    = true;
    bool lazy_local_tools_       = true;
    bool local_tools_discovered_ = false;
    int  max_reflection_retries_ = 2;

    // User Reply mode — controls when the Agent pauses for user input.
    UserReplyMode user_reply_mode_ = UserReplyMode::Off;

    // Discover and register local tools if lazy discovery is enabled.
    void discover_local_tools();

    // Internal streaming loop: same logic but uses chat_stream with token callback
    ChatResponse reasoning_loop_stream(const std::string& user_input, TokenCallback on_token);

    // Decide whether the input warrants task planning.
    // Uses a lightweight LLM call to ask if the task is complex enough to need planning.
    // Trivially short inputs (< 30 chars) bypass the LLM and return false directly.
    bool needs_planning(ChatResponse &resp);

    // ── Advanced pipeline ───────────────────────────────────

    // Full pipeline: plan → execute steps (with reflection + multi-agent) → assemble result.
    std::string run_planned(const std::string& user_input, ChatResponse& resp, TokenCallback on_token);
};

// Global Agent accessor (set by main.cpp, used by completion system).
Agent* get_global_agent();
void set_global_agent(Agent* ag);

} // namespace agent
