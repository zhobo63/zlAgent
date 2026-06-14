#include "multi_agent.h"
#include "wide_string.h"
#include <iostream>
#include <algorithm>

namespace agent {

std::string agent_role_to_string(AgentRole role) {
    switch (role) {
        case AgentRole::Coder:    return "Coder";
        case AgentRole::Reviewer: return "Reviewer";
        case AgentRole::Tester:   return "Tester";
    }
    return "Unknown";
}

// �w�w SubAgent �w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w

SubAgent::SubAgent(const std::string& llm_url, AgentRole role, ToolRegistry* shared_registry)
    : llm_(llm_url), registry_(shared_registry), role_(role) {
    memory_.set_system_prompt(system_prompt());
}

std::string SubAgent::system_prompt() {
    switch (role_) {
        case AgentRole::Coder:
            return R"(You are a Coder agent. Your job is to write, modify, and organize code.
- Use read_file before editing any file.
- Prefer edit_file for targeted changes; use write_file only for new files or full rewrites.
- Write clean, modern C++ (C++17/20) code with clear comments.
- After writing code that should compile, suggest running the build tool.)";

        case AgentRole::Reviewer:
            return R"(You are a Reviewer agent. Your job is to review code for quality and correctness.
- Check for bugs, memory safety issues, style violations, and performance anti-patterns.
- Use read_file and grep_with_context to inspect the code.
- Report findings in a structured format: severity (high/medium/low), location, description, suggestion.
- Be constructive - always suggest how to fix issues.)";

        case AgentRole::Tester:
            return R"(You are a Tester agent. Your job is to build and test code.
- Use run_build or execute_command to compile the project.
- If compilation fails, analyze errors and report them clearly with file:line references.
- Run tests if they exist; report pass/fail status.
- Do NOT modify source files - only verify.)";
    }
    return "You are a general-purpose agent.";
}

ChatResponse SubAgent::run_loop(const std::string& task) {
    ChatMessage user_msg{"user", task, ""};
    memory_.add(user_msg);

    int iteration = 0;
    const int max_iter = 5; // sub-agents have a tighter limit

    while (iteration < max_iter) {
        iteration++;

        auto messages = memory_.get_messages();
        std::vector<ToolDefinition> tool_defs;
        if (registry_) {
            tool_defs = registry_->get_definitions();
        }

        ChatResponse resp = llm_.chat(messages, tool_defs);

        if (!resp.has_tool_calls || resp.tool_calls.empty()) {
            return resp;
        }

        for (const auto& tc : resp.tool_calls) {
            std::cout << "  [" << agent_role_to_string(role_) << "] Tool: " << tc.name
                       << " args: " << tc.arguments.substr(0, 120) << std::endl;

            std::string result = "";
            if (registry_) {
                result = registry_->execute(tc.name, tc.arguments);
            } else {
                result = "[No tool registry available]";
            }

            ChatMessage tool_msg{"tool", result, tc.name};
            if (!tc.id.empty()) {
                tool_msg.content = "[call_id: " + tc.id + "] " + tool_msg.content;
            }
            memory_.add(tool_msg);
        }
    }

    return ChatResponse{"[Sub-agent max iterations reached.]"};
}

std::string SubAgent::execute(const std::string& task) {
    auto resp = run_loop(task);
    return resp.content;
}

// �w�w MultiAgent �w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w�w

MultiAgent::MultiAgent(const std::string& llm_url, ToolRegistry* shared_registry)
    : coder_(std::make_shared<SubAgent>(llm_url, AgentRole::Coder, shared_registry)),
      reviewer_(std::make_shared<SubAgent>(llm_url, AgentRole::Reviewer, shared_registry)),
      tester_(std::make_shared<SubAgent>(llm_url, AgentRole::Tester, shared_registry)) {}

AgentRole MultiAgent::route_step(const std::string& step_description) {
    // Simple keyword-based routing. Could be replaced by LLM-based routing later.
    std::string lower = step_description;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower.find("review") != std::string::npos ||
        lower.find("inspect") != std::string::npos ||
        lower.find("audit") != std::string::npos) {
        return AgentRole::Reviewer;
    }

    if (lower.find("build") != std::string::npos ||
        lower.find("compile") != std::string::npos ||
        lower.find("test") != std::string::npos ||
        lower.find("verify") != std::string::npos) {
        return AgentRole::Tester;
    }

    // Default: Coder handles everything else.
    return AgentRole::Coder;
}

std::string MultiAgent::execute_task(const std::string& task_description) {
    auto role = route_step(task_description);

    std::cout << "\n[MultiAgent] Routing step to " << agent_role_to_string(role) << " agent." << std::endl;

    // For coding tasks, run a Coder �� Reviewer pipeline.
    if (role == AgentRole::Coder) {
        auto coder_result = coder_->execute(task_description);

        // If the coder produced something substantial, have the reviewer check it.
        if (coder_result.size() > 50) {
            std::cout << "[MultiAgent] Running code review..." << std::endl;
            std::string review_task = "Review the following work for correctness and quality:\n" + coder_result;
            auto review_result = reviewer_->execute(review_task);

            // If the reviewer found issues, feed them back to the coder.
            if (!review_result.empty() && review_result.find("issue") != std::string::npos) {
                std::cout << "[MultiAgent] Reviewer found issues, sending feedback to Coder..." << std::endl;
                std::string fix_task = "Fix the following issues in your previous work:\n" + review_result;
                coder_result = coder_->execute(fix_task);
            }
        }

        return coder_result;
    }

    // For reviewer/tester tasks, just run directly.
    if (role == AgentRole::Reviewer) {
        return reviewer_->execute(task_description);
    }

    if (role == AgentRole::Tester) {
        return tester_->execute(task_description);
    }

    return "[MultiAgent] Unknown agent role.";
}

} // namespace agent
