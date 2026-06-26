#include "pch.h"

#include "agent.h"
#include "local_tools.h"
#include "task_planner.h"
#include "self_reflector.h"
#include "multi_agent.h"
#include "logger.h"

namespace agent {

using json = nlohmann::json;

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

    LOG_INFO("Lazy", "\nDiscovering local tools...");
    auto local_tools = create_local_tools();
    for (auto& tool : local_tools) {
        registry_.register_tool(std::move(tool));
    }
    LOG_INFO("Lazy", "Local tools discovered and registered.");
}

// ── LLM-based planning decision ────────────────────────────

bool Agent::needs_planning(ChatResponse &resp) {
    // Fast path: trivially short inputs never need planning.
    try {
        std::string answer = resp.content;
        // Trim whitespace and convert to upper case for comparison.
        size_t start = answer.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) return false;
        answer = answer.substr(start);
        std::transform(answer.begin(), answer.end(), answer.begin(), ::toupper);
        return answer.find("YES") != std::string::npos;
    } catch (...) {
        // If the LLM call fails, fall back to a conservative default: no planning.
        return false;
    }
}

// Streaming version: tokens are printed as they arrive via on_token callback
std::string Agent::run_stream(const std::string& user_input, TokenCallback on_token, ChatResponse* usage_out) {
    // Lazy discover local tools on first chat.
    discover_local_tools();

    // Add user message to memory
    ChatMessage user_msg{"user", user_input, ""};
    memory_.add(user_msg);

    // Run streaming reasoning loop
    ChatResponse response = reasoning_loop_stream(user_input, on_token);

    // Pass usage info back if requested
    if (usage_out) {
        usage_out->prompt_tokens     = response.prompt_tokens;
        usage_out->completion_tokens = response.completion_tokens;
        usage_out->max_tokens        = response.max_tokens;
    }

    // Add assistant response to memory
    memory_.add(ChatMessage{"assistant", response.content, ""});

    return response.content;
}

// ── User Reply handler ─────────────────────────────────────────────
// After the caller has handled Abort and Custom, this processes the remaining
// actions. result is empty for pre-execution; contains tool output for post-execution.
enum class ReplyDirective {
    Continue,   // proceed with (possibly modified) args
    Skip,       // skip this tool call entirely
    ReExecute,  // re-execute with modified args (post-execution only)
};

static ReplyDirective handle_user_reply(
    ChatResponse::ToolCall& tc,
    const std::string& result,
    const UserReplyResult& reply) {

    if (reply.action == ReplyAction::No) {
        LOG_WARN("Tool", "Skipped: " + tc.name);
        return ReplyDirective::Skip;
    }

    if (result.empty()) {
        // Pre-execution: fall through to execute with args.
        return ReplyDirective::Continue;
    }
    // Post-execution: re-execute with args.
    return ReplyDirective::ReExecute;
}


// Streaming reasoning loop: same logic but uses chat_stream with token callback.
ChatResponse Agent::reasoning_loop_stream(const std::string& user_input, TokenCallback on_token) {
    int iteration = 0;
    size_t total_prompt = 0, total_completion = 0;  // accumulate across iterations

    while (iteration < max_iterations_) {
        iteration++;

        // Compress context if history is too large
        if (memory_.summarize(llm_)) {
            LOG_INFO("Memory", "\nContext compressed via summarization.\n");
        }

        const auto& messages = memory_.get_messages();
        auto tool_defs = registry_.get_definitions();

        // Call LLM with streaming
        ChatResponse resp = llm_.chat_stream(messages, on_token, tool_defs);

        // Accumulate token usage across iterations
        total_prompt += resp.prompt_tokens;
        total_completion += resp.completion_tokens;

        // If task planning is enabled and the input looks like a complex task, use the advanced pipeline.
        if (task_planning_ && needs_planning(resp)) {
            std::cout << run_planned(user_input, resp, on_token);
            return resp;
        }

        // If no tool calls, return the response
        LOG_DEBUG("Agent", "LLM response: has_tool_calls=" + std::to_string(resp.has_tool_calls) +
                 ", tool_calls.size()=" + std::to_string(resp.tool_calls.size()));
        if (!resp.has_tool_calls || resp.tool_calls.empty()) {
            resp.prompt_tokens = total_prompt;
            resp.completion_tokens = total_completion;
            return resp;
        }

        // Add assistant message with tool_calls to memory (required by OpenAI API)
        ChatMessage assistant_msg{"assistant", resp.content};
        for (const auto& tc : resp.tool_calls) {
            ToolCallInfo tci{};
            tci.id = tc.id;
            tci.name = tc.name;
            tci.arguments = tc.arguments;
            assistant_msg.tool_calls.push_back(std::move(tci));
        }
        memory_.add(assistant_msg);

        // Execute each tool call and add results to memory (non-streaming for tools)
        for (auto& tc : resp.tool_calls) {
            // Guard: skip if LLM returned empty arguments (streaming truncation)
            if (tc.arguments.empty()) {
                LOG_WARN("Tool", "\nSkipping '" + tc.name + u8"' — empty arguments from LLM, feeding error back");
                ChatMessage tool_msg{"tool", "[Error] Tool call was truncated: missing arguments for '" + tc.name + "'. Please retry with complete arguments.", tc.name};
                if (!tc.id.empty()) {
                    tool_msg.content = "[call_id: " + tc.id + "] " + tool_msg.content;
                }
                memory_.add(tool_msg);
                continue;
            }

            // ── User Reply: pre-execution check (Exec/Edit/Always mode) ──
            if (user_reply_mode_ == UserReplyMode::Exec && tc.name == "execute_command") {
                auto reply = prompt_user_reply(tc.name, tc.arguments);
                if (reply.action == ReplyAction::No) {
                    LOG_WARN("Tool", "Skipped: " + tc.name);
                    continue;
                }
            } else if (user_reply_mode_ == UserReplyMode::Edit && (tc.name == "edit_file" || tc.name == "write_file")) {
                auto reply = prompt_user_reply(tc.name, tc.arguments);
                if (reply.action == ReplyAction::No) {
                    LOG_WARN("Tool", "Skipped: " + tc.name);
                    continue;
                }
            } else if (user_reply_mode_ == UserReplyMode::Always) {
                auto reply = prompt_user_reply(tc.name, tc.arguments);
                if (reply.action == ReplyAction::No) {
                    LOG_WARN("Tool", "Skipped: " + tc.name);
                    continue;
                }
            }

            // Validate arguments are well-formed JSON before executing
            try {
                auto arg = json::parse(tc.arguments);
            } catch (const json::parse_error& e) {
                LOG_WARN("Tool", "Truncated/invalid JSON arguments for '" + tc.name + "': " + std::string(e.what()));
                ChatMessage tool_msg{"tool",
                    "[Error] Tool call arguments are malformed or truncated: " + std::string(e.what()) +
                    ". The response may have hit the token limit. For large files, use edit_file for targeted changes instead of write_file.",
                    tc.name};
                if (!tc.id.empty()) {
                    tool_msg.content = "[call_id: " + tc.id + "] " + tool_msg.content;
                }
                memory_.add(tool_msg);
                continue;
            }

            LOG_INFO(u8"🛠️Tool", "\nExecuting: " + tc.name + " with args: " + tc.arguments);

            std::string result = registry_.execute(tc.name, tc.arguments);

            // Add tool response to memory
            ChatMessage tool_msg{"tool", result, tc.name};
            if (!tc.id.empty()) {
                tool_msg.content = "[call_id: " + tc.id + "] " + tool_msg.content;
            }
            memory_.add(tool_msg);

            LOG_INFO(u8"🛠️Tool", "Result: " + result + "\n");
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

std::string Agent::run_planned(const std::string& user_input, ChatResponse& resp, TokenCallback on_token) {
    TaskPlanner planner(llm_);
    SelfReflector reflector(llm_);
    MultiAgent multi_agent(llm_.get_base_url(), &registry_);

    // Step 1: Generate a plan
    LOG_INFO("Planner", "\nGenerating task plan...");
    const auto& context = memory_.get_messages();
    Plan plan = planner.generate_plan(resp.content, context);

    if (plan.steps.empty()) {
        // Planning failed - fall back to normal execution.
        LOG_WARN("Planner", "No steps generated, falling back to direct execution.");
        return resp.content;
    }

    // Display the plan
    LOG_INFO("Planner", "\nPlan for: " + plan.overall_goal);
    for (const auto& step : plan.steps) {
        LOG_INFO("Planner", "  Step " + std::to_string(step.id) + ": " + step.description);
    }

    // Step 2: Execute each step with reflection and optional multi-agent routing.
    // Use a while loop so that replanning (which replaces plan.steps) doesn't
    // invalidate the iterator of a range-based for loop.
    std::vector<StepResult> completed_steps;
    std::ostringstream final_result;

    size_t step_index = 0;
    while (step_index < plan.steps.size()) {
        auto& step = plan.steps[step_index];
        step.status = "in_progress";
        LOG_INFO("Planner", "\nExecuting Step " + std::to_string(step.id) + ": " + step.description);

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

            ChatResponse resp = reasoning_loop_stream(step.description, step_callback);
            step_output = resp.content;
            memory_.add(ChatMessage{"assistant", step_output, ""});
        }

        // Self-Reflection: review the output and retry if needed.
        bool step_success = true;
        std::string error_msg;

        if (self_reflection_) {
            auto reflection = reflector.review(step.description, step_output);

            if (reflection.needs_correction) {
                LOG_WARN("Reflection", "\nIssues found:");
                LOG_WARN("Reflection", "  " + reflection.feedback.substr(0, 300));

                // Retry with feedback.
                for (int retry = 0; retry < max_reflection_retries_; ++retry) {
                    LOG_INFO("Reflection", "\nRetry " + std::to_string(retry + 1) + "/" + std::to_string(max_reflection_retries_));

                    std::string correction_task = step.description +
                        "\n\nPrevious attempt had issues. Fix the following:\n" + reflection.feedback;

                    if (multi_agent_) {
                        step_output = multi_agent.execute_task(correction_task);
                    } else {
                        ChatMessage fix_msg{"user", correction_task, ""};
                        memory_.add(fix_msg);
                        ChatResponse fix_resp = reasoning_loop_stream(user_input, on_token);
                        step_output = fix_resp.content;
                        memory_.add(ChatMessage{"assistant", step_output, ""});
                    }

                    // Re-review after correction.
                    auto re_reflection = reflector.review(step.description, step_output);
                    if (!re_reflection.needs_correction) {
                        LOG_INFO("Reflection", "Correction accepted.");
                        break;
                    }

                    reflection = re_reflection; // continue with latest feedback.
                }

                // If still failing after all retries, mark as failed but keep the output.
                if (reflection.needs_correction) {
                    step_success = false;
                    error_msg = "Still has issues after " + std::to_string(max_reflection_retries_) +
                                " retries: " + reflection.feedback.substr(0, 200);
                    LOG_ERROR("Reflection", "Step still has issues after max retries.");
                }
            } else {
                LOG_INFO("Reflection", "Step passed quality check.");
            }
        }

        step.status = step_success ? "completed" : "failed";
        step.result = step_output.substr(0, 500); // store a summary.

        completed_steps.push_back({step.id, step.description, step_output, step_success, error_msg});

        ++step_index;

        if (!step_success) {
            // Attempt to re-plan from this point.
            LOG_WARN("Planner", "\nStep " + std::to_string(step.id) + " failed, attempting replan...");
            Plan new_plan = planner.replan(plan.overall_goal, completed_steps, error_msg);

            if (!new_plan.steps.empty()) {
                LOG_INFO("Planner", "Replanned steps:");
                for (const auto& ns : new_plan.steps) {
                    LOG_INFO("Planner", "  Step " + std::to_string(ns.id) + ": " + ns.description);
                }

                // Replace remaining steps with the replan and restart from index 0.
                plan.steps = std::move(new_plan.steps);
                step_index = 0;
            } else {
                LOG_ERROR("Planner", "Replan failed, continuing with next step.");
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