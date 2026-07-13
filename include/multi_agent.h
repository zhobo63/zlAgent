#pragma once

#include <string>
#include <vector>
#include <memory>
#include "llm_client.h"

namespace agent {

/**
 * A specialized sub-agent with a focused system prompt.
 */

class SubAgent {
public:
    SubAgent(const std::string& name);

    const std::string& get_name() const { return name_; }
    const std::string& description() const {return description_; }

    // Execute a task and return the result string.
    std::string execute(const std::string& task);

    // Run a mini reasoning loop (max 5 iterations per sub-agent).
    virtual ChatResponse run_loop(const std::string& task) { return ChatResponse{}; }
protected:
    std::string name_;
    std::string description_;
};

/**
 * A specialized sub-agent with LLM.
 */

class Agent;

class SubAgentLLM: public SubAgent {
public:
    SubAgentLLM(const std::string& name);

    void set_workdir(const std::string workdir);

    ChatResponse run_loop(const std::string& task) override;
private:
    mutable std::unique_ptr<Agent> agent_;
};

/**
 * A specialized sub-agent with network
 */

class SubAgentNet : public SubAgent {
public:
    SubAgentNet(const std::string& name);

private:
    // Cached HTTP clients for connection reuse (keep-alive).
    // Lazily initialized on first use.
    mutable std::unique_ptr<httplib::Client> client_;
};

/**
 * A specialized sub-agent with CLI
 */

class SubAgentCLI : public SubAgent {
public:
    SubAgentCLI(const std::string& name);

private:

};

/**
 * MultiAgent coordinates specialized sub-agents to handle different step types.
 * The TaskPlanner assigns steps, and MultiAgent routes each step to the right agent.
 */
class MultiAgent {
public:
    MultiAgent();

    bool is_enable() const;

    // Register a sub-agent and automatically wrap it as a Tool in the Agent's ToolRegistry.
    void register_agent(std::shared_ptr<SubAgent> agent);

private:
    // Cached HTTP clients for connection reuse (keep-alive).
    // Lazily initialized on first use.
    mutable std::unique_ptr<httplib::Server> server_;

    std::vector<std::shared_ptr<SubAgent>> agents_;
};

} // namespace agent
