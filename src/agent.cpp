#include "agent.h"
#include <iostream>

namespace agent {

Agent::Agent(const std::string& llm_url) : llm_(llm_url) {}

void Agent::add_tool(ToolPtr tool) {
    registry_.register_tool(std::move(tool));
}

void Agent::set_system_prompt(const std::string& prompt) {
    memory_.set_system_prompt(prompt);
}

std::string Agent::run(const std::string& user_input) {
    // Add user message to memory
    ChatMessage user_msg{"user", user_input, ""};
    memory_.add(user_msg);

    // Run reasoning loop
    ChatResponse response = reasoning_loop();

    // Add assistant response to memory
    memory_.add(ChatMessage{"assistant", response.content, ""});

    return response.content;
}

// Streaming version: tokens are printed as they arrive via on_token callback
std::string Agent::run_stream(const std::string& user_input, TokenCallback on_token) {
    // Add user message to memory
    ChatMessage user_msg{"user", user_input, ""};
    memory_.add(user_msg);

    // Run streaming reasoning loop
    ChatResponse response = reasoning_loop_stream(on_token);

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

        // If no tool calls, return the response
        if (!resp.has_tool_calls || resp.tool_calls.empty()) {
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

            std::cout << "[Tool] Result: " << result.substr(0, 200)
                      << (result.size() > 200 ? "..." : "") << "\n" << std::endl;
        }

        // Loop again - LLM will see tool results and decide next action
    }

    // Safety fallback after max iterations
    return ChatResponse{"[Max iterations reached. Stopping.]"};
}

} // namespace agent
