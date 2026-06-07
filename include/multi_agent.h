#pragma once

#include <string>
#include <vector>
#include <memory>
#include "llm_client.h"
#include "tool.h"
#include "memory.h"

namespace agent {

/**
 * Role of a sub-agent in the multi-agent system.
 */
enum class AgentRole {
    Coder,      // Writes and modifies code
    Reviewer,   // Reviews code for quality/correctness
    Tester      // Runs builds/tests and validates results
};

std::string agent_role_to_string(AgentRole role);

/**
 * A specialized sub-agent with a focused system prompt.
 */
class SubAgent {
public:
    SubAgent(const std::string& llm_url, AgentRole role, ToolRegistry* shared_registry);

    // Execute a task and return the result string.
    std::string execute(const std::string& task);

    AgentRole role() const { return role_; }

private:
    LLMClient llm_;
    Memory memory_;
    ToolRegistry* registry_ = nullptr;
    AgentRole role_;

    // Return the system prompt for this agent's role.
    std::string system_prompt();

    // Run a mini reasoning loop (max 5 iterations per sub-agent).
    ChatResponse run_loop(const std::string& task);
};

/**
 * MultiAgent coordinates specialized sub-agents to handle different step types.
 * The TaskPlanner assigns steps, and MultiAgent routes each step to the right agent.
 */
class MultiAgent {
public:
    explicit MultiAgent(const std::string& llm_url, ToolRegistry* shared_registry);

    // Execute a single task using the appropriate sub-agent(s).
    // For coding tasks: Coder → Reviewer → Tester pipeline.
    // For simple tasks: direct execution by the most suitable agent.
    std::string execute_task(const std::string& task_description);

private:
    std::shared_ptr<SubAgent> coder_;
    std::shared_ptr<SubAgent> reviewer_;
    std::shared_ptr<SubAgent> tester_;

    // Decide which agent(s) to use for a given step.
    AgentRole route_step(const std::string& step_description);
};

} // namespace agent
