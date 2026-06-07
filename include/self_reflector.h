#pragma once

#include <string>
#include "llm_client.h"

namespace agent {

/**
 * Result of a self-reflection check.
 */
struct ReflectionResult {
    bool needs_correction;   // true if the output has issues worth fixing
    std::string feedback;    // what's wrong and how to fix it (empty if no issues)
};

/**
 * SelfReflector reviews an agent's output for quality, correctness, and completeness.
 * It asks the LLM to critique the result against the original task description, then
 * returns structured feedback that can trigger a retry with corrections.
 */
class SelfReflector {
public:
    explicit SelfReflector(LLMClient& llm);

    // Review the agent's output for a given step.
    // Returns whether correction is needed and what to fix.
    ReflectionResult review(const std::string& task_description,
                            const std::string& agent_output);

private:
    LLMClient& llm_;

    static std::string reflection_system_prompt();
    static ReflectionResult parse_reflection(const std::string& raw_response);
};

} // namespace agent
