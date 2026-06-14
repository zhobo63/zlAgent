#include "task_planner.h"
#include <iostream>
#include <sstream>
#include <regex>
#include "json.hpp"
#include "wide_string.h"

namespace agent {

TaskPlanner::TaskPlanner(LLMClient& llm) : llm_(llm) {}

std::string TaskPlanner::planning_system_prompt() {
    return R"(You are a task planner. Break the user's request into clear, ordered sub-steps.
Each step should be specific and actionable - something an agent can execute with tools.

Respond in JSON format:
{
  "overall_goa": "<one-line summary of what we're trying to achieve>",
  "steps": [
    {"id": 1, "description": "<concrete action>"},
    {"id": 2, "description": "<next concrete action>"},
    ...
  ]
}

Rules:
- Steps must be in logical order (dependencies first).
- Each step should be a single focused action.
- Include verification steps where appropriate (e.g., compile after writing code).
- Keep the number of steps reasonable (3-8 for most tasks).
- Do NOT include tool names or implementation details - just describe what to do.)";
}

Plan TaskPlanner::generate_plan(const std::string& task, const std::vector<ChatMessage>& context) {
    ChatMessage system_msg{"system", planning_system_prompt(), ""};
    ChatMessage user_msg{"user", "Task: " + task, ""};

    std::vector<ChatMessage> messages;
    messages.push_back(system_msg);

    // Include recent conversation context so the planner knows what's already been done.
    for (const auto& ctx : context) {
        messages.push_back(ctx);
    }

    messages.push_back(user_msg);

    ChatResponse resp = llm_.chat(messages, {}, 0.2, 2048);

    Plan plan;
    plan.overall_goal = task; // fallback
    return parse_plan(resp.content);
}

Plan TaskPlanner::replan(const std::string& original_task,
                         const std::vector<StepResult>& completed_steps,
                         const std::string& failure_reason) {
    ChatMessage system_msg{"system", planning_system_prompt(), ""};

    // Build a summary of what's been done and what failed.
    std::ostringstream oss;
    oss << "Original task: " << original_task << "\n\n";
    oss << "Completed steps:\n";
    for (const auto& sr : completed_steps) {
        oss << "  Step " << sr.id << ": " << sr.description << "\n";
        if (sr.success) {
            std::string preview = sr.output;
            if (preview.size() > 200) preview.resize(200);
            oss << "    Result: " << preview << "\n";
        } else {
            oss << "    FAILED: " << sr.error_message << "\n";
        }
    }
    oss << "\nThe above step failed. Re-plan the remaining steps, keeping already-completed steps intact.\n";

    ChatMessage user_msg{"user", oss.str(), ""};

    std::vector<ChatMessage> messages;
    messages.push_back(system_msg);
    messages.push_back(user_msg);

    ChatResponse resp = llm_.chat(messages, {}, 0.2, 2048);

    return parse_plan(resp.content);
}

Plan TaskPlanner::parse_plan(const std::string& raw_response) {
    Plan plan;
    plan.overall_goal = "Unknown";

    // Try to extract JSON from the response (may be wrapped in markdown code blocks).
    std::string json_str = raw_response;

    // Strip ```json ... ``` or ``` ... ``` wrappers.
    auto first_brace = json_str.find('{');
    auto last_brace = json_str.rfind('}');
    if (first_brace != std::string::npos && last_brace != std::string::npos) {
        json_str = json_str.substr(first_brace, last_brace - first_brace + 1);
    }

    try {
        auto j = nlohmann::json::parse(json_str);

        if (j.contains("overall_goa")) {
            plan.overall_goal = j["overall_goa"].get<std::string>();
        }

        if (j.contains("steps") && j["steps"].is_array()) {
            for (const auto& step_j : j["steps"]) {
                Step s;
                s.id = step_j.value("id", 0);
                s.description = step_j.value("description", "No description");
                plan.steps.push_back(s);
            }
        }
    } catch (...) {
        // JSON parse failed - fall back to line-by-line extraction.
        std::cout << "[Planner] JSON parse failed, falling back to text parsing." << std::endl;

        // Look for lines like "1. ..." or "- ...".
        std::regex step_regex(R"(^\s*(?:\d+[\.\)]|\-)\s+(.+)$)");
        std::istringstream iss(raw_response);
        std::string line;
        int id = 0;

        while (std::getline(iss, line)) {
            std::smatch match;
            if (std::regex_search(line, match, step_regex)) {
                id++;
                Step s;
                s.id = id;
                s.description = match[1].str();
                plan.steps.push_back(s);
            }
        }

        // If we found no steps at all, treat the whole response as a single step.
        if (plan.steps.empty()) {
            Step s;
            s.id = 1;
            s.description = raw_response.substr(0, std::min(raw_response.size(), static_cast<size_t>(500)));
            plan.steps.push_back(s);
        }
    }

    return plan;
}

} // namespace agent
