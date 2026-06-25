#include "pch.h"

#include "self_reflector.h"


namespace agent {

SelfReflector::SelfReflector(LLMClient& llm) : llm_(llm) {}

std::string SelfReflector::reflection_system_prompt() {
    return u8R"(You are a quality reviewer. Critique the following output against the task description.

Respond in JSON format:
{
  "needs_correction": true/false,
  "feedback": "<specific issues and how to fix them, or empty string if no issues>"
}

Check for:
- Correctness: Does the output actually accomplish what was asked?
- Completeness: Are all parts of the task addressed?
- Quality: Is the code clean, well-structured, and free of obvious bugs?
- Consistency: Are there contradictions or half-done changes?

Be strict but fair. Only flag real issues - don't nitpick style unless it causes confusion.)";
}

ReflectionResult SelfReflector::review(const std::string& task_description,
                                        const std::string& agent_output) {
    ChatMessage system_msg{"system", reflection_system_prompt(), ""};

    std::ostringstream oss;
    oss << "Task: " << task_description << "\n\n";
    oss << "Agent output:\n---\n";
    oss << agent_output << "\n---\n";

    ChatMessage user_msg{"user", oss.str(), ""};

    std::vector<ChatMessage> messages;
    messages.push_back(system_msg);
    messages.push_back(user_msg);

    ChatResponse resp = llm_.chat(messages, {}, 0.2, 1024);

    return parse_reflection(resp.content);
}

ReflectionResult SelfReflector::parse_reflection(const std::string& raw_response) {
    ReflectionResult result;
    result.needs_correction = false;
    result.feedback = "";

    // Try JSON first.
    auto first_brace = raw_response.find('{');
    auto last_brace = raw_response.rfind('}');
    if (first_brace != std::string::npos && last_brace != std::string::npos) {
        std::string json_str = raw_response.substr(first_brace, last_brace - first_brace + 1);

        try {
            auto j = nlohmann::json::parse(json_str);

            if (j.contains("needs_correction")) {
                result.needs_correction = j["needs_correction"].get<bool>();
            }
            if (j.contains("feedback")) {
                result.feedback = j["feedback"].get<std::string>();
            }
        } catch (...) {
            // Fall back: if the response contains any non-empty text, treat it as feedback.
            if (!raw_response.empty()) {
                result.needs_correction = true;
                result.feedback = raw_response.substr(0, std::min(raw_response.size(), static_cast<size_t>(500)));
            }
        }
    } else {
        // No JSON - treat the whole response as feedback.
        if (!raw_response.empty()) {
            result.needs_correction = true;
            result.feedback = raw_response.substr(0, std::min(raw_response.size(), static_cast<size_t>(500)));
        }
    }

    return result;
}

} // namespace agent
