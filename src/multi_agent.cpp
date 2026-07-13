#include "pch.h"

#include "multi_agent.h"
#include "logger.h"
#include "agent.h"

namespace agent {

// ── SubAgent ───────────────────────────────────────

SubAgent::SubAgent(const std::string& name) : name_(name) {
}

std::string SubAgent::execute(const std::string& task) {
    auto resp = run_loop(task);
    return resp.content;
}

// ── SubAgentLLM ───────────────────────────────────────

SubAgentLLM::SubAgentLLM(const std::string& name): SubAgent(name)
{
    
}

void SubAgentLLM::set_workdir(const std::string workdir)
{

}

ChatResponse SubAgentLLM::run_loop(const std::string& task) {
    ChatMessage user_msg{"user", task, ""};
    ChatResponse response;

    bool in_reasoning = false;
    if (agent_) {
        agent_->run_stream(task, [&](const std::string& token, bool is_reasoning_flag) {
            return true;
            }, &response);
    }

    LOG_INFO("SubAgentLLM", response.content);
    return response;
}


// ── MultiAgent ─────────────────────────────────────

MultiAgent::MultiAgent()
{
}

bool MultiAgent::is_enable() const
{
    return server_ && server_->is_running();
}

void MultiAgent::register_agent(std::shared_ptr<SubAgent> agent)
{
    agents_.push_back(agent);
}

} // namespace agent
