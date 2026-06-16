#include "pch.h"

#include "agent.h"
#include "wide_string.h"
#include "local_tools.h"
#include "task_planner.h"
#include "self_reflector.h"
#include "multi_agent.h"

namespace agent {

Agent::Agent(const std::string& llm_url, const std::string& model)
    : llm_(llm_url, model) {}

void Agent::add_tool(ToolPtr tool) {
    registry_.register_tool(std::move(tool));
}

std::vector<std::string> Agent::get_tool_names() const {
    std::vector<std::string> names;
    for (const auto& tool : registry_.get_tools()) {
        names.push_back(tool->name());
    }
    return names;
}

void Agent::set_system_prompt(const std::string& prompt) {
    memory_.set_system_prompt(prompt);
}

// ── Lazy local tool discovery ──────────────────────────────

void Agent::discover_local_tools() {
    if (!local_tools_enabled_ || !lazy_local_tools_ || local_tools_discovered_) return;
    local_tools_discovered_ = true;

    std::cout << "\n[Lazy] Discovering local tools..." << std::endl;
    auto local_tools = create_local_tools();
    for (auto& tool : local_tools) {
        registry_.register_tool(std::move(tool));
    }
    std::cout << "[Lazy] Local tools discovered and registered." << std::endl;
}

std::string Agent::run(const std::string& user_input) {
    // Lazy discover local tools on first chat.
    discover_local_tools();

    // If task planning is enabled and the input looks like a complex task, use the advanced pipeline.
    if (task_planning_ && needs_planning(user_input)) {
        return run_planned(user_input, nullptr);
    }

    // Add user message to memory
    ChatMessage user_msg{"user", user_input, ""};
    memory_.add(user_msg);

    // Run reasoning loop
    ChatResponse response = reasoning_loop();

    // Add assistant response to memory
    memory_.add(ChatMessage{"assistant", response.content, ""});

    return response.content;
}

// ── Planning heuristic ─────────────────────────────────────

bool Agent::needs_planning(const std::string& input) {
    // Convert to lowercase for case-insensitive checks.
    std::string lower = input;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // 1. Length threshold — short inputs are unlikely to need planning.
    if (input.size() < 30) return false;

    // 2. Multi-step keywords (English + Chinese)
    static const char* multi_step_keywords[] = {
        "first", "then", "next", "after that", "finally",
        "step", "steps", "phase", "phases",
        "also", "and then", "followed by",
        "1.", "2.", "3.",
        // Chinese multi-step indicators
        u8"首先", u8"然後", u8"接著", u8"階段", u8"一", u8"步驟",
        u8"二步", u8"接下來", u8"第三步", u8"第四步",
    };
    for (const auto* kw : multi_step_keywords) {
        if (lower.find(kw) != std::string::npos)
            return true;
    }

    // 3. Action-rich input — contains multiple action verbs suggesting a pipeline.
    static const char* action_verbs[] = {
        "create", "write", "read", "search", "build",
        "test", "deploy", "refactor", "analyze",
        "generate", "implement", "fix", "review",
    };
    int verb_count = 0;
    for (const auto* v : action_verbs) {
        if (lower.find(v) != std::string::npos)
            ++verb_count;
    }
    if (verb_count >= 2)
        return true;

    // 4. Long input with file/code references — likely a complex task.
    static const char* code_refs[] = {
        "file", "code", "function",
        u8"檔案", u8"程式碼", u8"函數", u8"類別",
    };
    if (input.size() > 100) {
        for (const auto* ref : code_refs) {
            if (lower.find(ref) != std::string::npos)
                return true;
        }
    }

    // Default: simple query — no planning needed.
    return false;
}

// Streaming version: tokens are printed as they arrive via on_token callback
std::string Agent::run_stream(const std::string& user_input, TokenCallback on_token, ChatResponse* usage_out) {
    // Lazy discover local tools on first chat.
    discover_local_tools();

    // If task planning is enabled and the input looks like a complex task, use the advanced pipeline.
    if (task_planning_ && needs_planning(user_input)) {
        return run_planned(user_input, on_token);
    }

    // Add user message to memory
    ChatMessage user_msg{"user", user_input, ""};
    memory_.add(user_msg);

    // Run streaming reasoning loop
    ChatResponse response = reasoning_loop_stream(on_token);

    // Pass usage info back if requested
    if (usage_out) {
        usage_out->prompt_tokens     = response.prompt_tokens;
        usage_out->completion_tokens = response.completion_tokens;
    }

    // Add assistant response to memory
    memory_.add(ChatMessage{"assistant", response.content, ""});

    return response.content;
}

ChatResponse Agent::reasoning_loop() {
    int iteration = 0;

    while (iteration < max_iterations_) {
        iteration++;

        // Compress context if history is too large
        if (memory_.summarize(llm_)) {
            std::cout << "[Memory] Context compressed via summarization." << std::endl;
        }

        // Get current conversation history + tools
        auto messages = memory_.get_messages();
        auto tool_defs = registry_.get_definitions();

        // Call LLM
        ChatResponse resp = llm_.chat(messages, tool_defs);

        // If no tool calls, return the response
        if (!resp.has_tool_calls || resp.tool_calls.empty()) {
            return resp;
        }

        // Execute each tool call and add results to memory
        for (const auto& tc : resp.tool_calls) {
            std::cout << "[Tool] Executing: " << tc.name
                      << " with args: " << tc.arguments << std::endl;

            std::string result = registry_.execute(tc.name, tc.arguments);

            // Add tool response to memory
            ChatMessage tool_msg{"tool", result, tc.name};
            if (!tc.id.empty()) {
                tool_msg.content = "[call_id: " + tc.id + "] " + tool_msg.content;
            }
            memory_.add(tool_msg);

            std::cout << "[Tool] Result: " << result.substr(0, 200)
                      << (result.size() > 200 ? "..." : "") << std::endl;
        }

        // Loop again - LLM will see tool results and decide next action
    }

    // Safety fallback after max iterations
    return ChatResponse{"[Max iterations reached. Stopping.]"};
}

// Streaming reasoning loop: same logic but uses chat_stream with token callback.
ChatResponse Agent::reasoning_loop_stream(TokenCallback on_token) {
    int iteration = 0;
    size_t total_prompt = 0, total_completion = 0;  // accumulate across iterations

    while (iteration < max_iterations_) {
        iteration++;

        // Compress context if history is too large
        if (memory_.summarize(llm_)) {
            std::cout << "\n[Memory] Context compressed via summarization.\n" << std::endl;
        }

        auto messages = memory_.get_messages();
        auto tool_defs = registry_.get_definitions();

        // Call LLM with streaming
        ChatResponse resp = llm_.chat_stream(messages, on_token, tool_defs);

        // Accumulate token usage across iterations
        total_prompt += resp.prompt_tokens;
        total_completion += resp.completion_tokens;

        // If no tool calls, return the response
        if (!resp.has_tool_calls || resp.tool_calls.empty()) {
            resp.prompt_tokens = total_prompt;
            resp.completion_tokens = total_completion;
            return resp;
        }

        // Execute each tool call and add results to memory (non-streaming for tools)
        for (const auto& tc : resp.tool_calls) {
            std::cout << "\n[Tool] Executing: " << tc.name
                      << " with args: " << tc.arguments << std::endl;

            std::string result = registry_.execute(tc.name, tc.arguments);

            // Add tool response to memory
            ChatMessage tool_msg{"tool", result, tc.name};
            if (!tc.id.empty()) {
                tool_msg.content = "[call_id: " + tc.id + "] " + tool_msg.content;
            }
            memory_.add(tool_msg);

            std::string preview = result.substr(0, 200);
            if (result.size() > 200) preview += "...";
            std::cout << "[Tool] Result: " << preview << "\n" << std::endl;
        }

        // Loop again - LLM will see tool results and decide next action
    }

    // Safety fallback after max iterations
    ChatResponse fallback{"[Max iterations reached. Stopping.]"};
    fallback.prompt_tokens = total_prompt;
    fallback.completion_tokens = total_completion;
    return fallback;
}

// ── Advanced Pipeline: Plan → Execute (with Reflection + Multi-Agent) ──

std::string Agent::run_planned(const std::string& user_input, TokenCallback on_token) {
    TaskPlanner planner(llm_);
    SelfReflector reflector(llm_);
    MultiAgent multi_agent(llm_.get_base_url(), &registry_);

    // Step 1: Generate a plan
    std::cout << "\n[Planner] Generating task plan..." << std::endl;
    auto context = memory_.get_messages();
    Plan plan = planner.generate_plan(user_input, context);

    if (plan.steps.empty()) {
        // Planning failed - fall back to normal execution.
        std::cout << "[Planner] No steps generated, falling back to direct execution." << std::endl;
        ChatMessage user_msg{"user", user_input, ""};
        memory_.add(user_msg);
        ChatResponse response = reasoning_loop_stream(on_token);
        memory_.add(ChatMessage{"assistant", response.content, ""});
        return response.content;
    }

    // Display the plan
    std::cout << "\n[Planner] Plan for: " << plan.overall_goal << std::endl;
    for (const auto& step : plan.steps) {
        std::cout << "  Step " << step.id << ": " << step.description << std::endl;
    }

    // Step 2: Execute each step with reflection and optional multi-agent routing.
    std::vector<StepResult> completed_steps;
    std::ostringstream final_result;

    for (auto& step : plan.steps) {
        step.status = "in_progress";
        std::cout << "\n[Planner] Executing Step " << step.id << ": " << step.description << std::endl;

        // Execute the step using multi-agent or direct agent.
        std::string step_output;
        if (multi_agent_) {
            step_output = multi_agent.execute_task(step.description);
        } else {
            // Use the main agent's reasoning loop for this step.
            ChatMessage user_msg{"user", step.description, ""};
            memory_.add(user_msg);

            std::string step_result;
            auto step_callback = [&step_result, &on_token](const std::string& token, bool is_reasoning) -> bool {
                step_result += token;
                if (on_token) return on_token(token, is_reasoning);
                return true;
            };

            ChatResponse resp = reasoning_loop_stream(step_callback);
            step_output = resp.content;
            memory_.add(ChatMessage{"assistant", step_output, ""});
        }

        // Self-Reflection: review the output and retry if needed.
        bool step_success = true;
        std::string error_msg;

        if (self_reflection_) {
            auto reflection = reflector.review(step.description, step_output);

            if (reflection.needs_correction) {
                std::cout << "\n[Reflection] Issues found:" << std::endl;
                std::cout << "  " << reflection.feedback.substr(0, 300) << std::endl;

                // Retry with feedback.
                for (int retry = 0; retry < max_reflection_retries_; ++retry) {
                    std::cout << "\n[Reflection] Retry " << (retry + 1) << "/" << max_reflection_retries_ << std::endl;

                    std::string correction_task = step.description +
                        "\n\nPrevious attempt had issues. Fix the following:\n" + reflection.feedback;

                    if (multi_agent_) {
                        step_output = multi_agent.execute_task(correction_task);
                    } else {
                        ChatMessage fix_msg{"user", correction_task, ""};
                        memory_.add(fix_msg);
                        ChatResponse fix_resp = reasoning_loop_stream(on_token);
                        step_output = fix_resp.content;
                        memory_.add(ChatMessage{"assistant", step_output, ""});
                    }

                    // Re-review after correction.
                    auto re_reflection = reflector.review(step.description, step_output);
                    if (!re_reflection.needs_correction) {
                        std::cout << "[Reflection] Correction accepted." << std::endl;
                        break;
                    }

                    reflection = re_reflection; // continue with latest feedback.
                }

                // If still failing after all retries, mark as failed but keep the output.
                if (reflection.needs_correction) {
                    step_success = false;
                    error_msg = "Still has issues after " + std::to_string(max_reflection_retries_) +
                                " retries: " + reflection.feedback.substr(0, 200);
                    std::cout << "[Reflection] Step still has issues after max retries." << std::endl;
                }
            } else {
                std::cout << "[Reflection] Step passed quality check." << std::endl;
            }
        }

        step.status = step_success ? "completed" : "failed";
        step.result = step_output.substr(0, 500); // store a summary.

        completed_steps.push_back({step.id, step.description, step_output, step_success, error_msg});

        if (!step_success) {
            // Attempt to re-plan from this point.
            std::cout << "\n[Planner] Step " << step.id << " failed, attempting replan..." << std::endl;
            Plan new_plan = planner.replan(plan.overall_goal, completed_steps, error_msg);

            if (!new_plan.steps.empty()) {
                std::cout << "[Planner] Replanned steps:" << std::endl;
                for (const auto& ns : new_plan.steps) {
                    std::cout << "  Step " << ns.id << ": " << ns.description << std::endl;
                }

                // Replace remaining steps with the replan.
                plan.steps = new_plan.steps;
            } else {
                std::cout << "[Planner] Replan failed, continuing with next step." << std::endl;
            }
        }
    }

    // Step 3: Assemble final result.
    final_result << "## Task Completed\n\n";
    final_result << "**Goal:** " << plan.overall_goal << "\n\n";
    final_result << "**Steps executed:**\n\n";

    for (const auto& sr : completed_steps) {
        final_result << "### Step " << sr.id << ": " << sr.description << "\n\n";
        if (sr.success) {
            std::string preview = sr.output;
            if (preview.size() > 1000) preview.resize(1000);
            final_result << preview << "\n\n";
        } else {
            final_result << "**[FAILED]** " << sr.error_message << "\n\n";
        }
    }

    // Add the assembled result to memory.
    ChatMessage user_msg{"user", user_input, ""};
    memory_.add(user_msg);
    memory_.add(ChatMessage{"assistant", final_result.str(), ""});

    return final_result.str();
}

} // namespace agent
