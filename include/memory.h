#pragma once

#include <string>
#include <vector>
#include "llm_client.h"

namespace agent {

/**
 * Manages conversation history with a sliding window and optional summarization.
 * Keeps the most recent N messages; when exceeded, older messages are compressed
 * into a summary via the LLM to preserve context without wasting tokens.
 */
class Memory {
public:
    explicit Memory(int max_messages = 50);

    // Add a message to history
    void add(const ChatMessage& msg);

    // Get all stored messages (trimmed to window)
    std::vector<ChatMessage> get_messages() const;

    // Clear history
    void clear();

    // Set system prompt
    void set_system_prompt(const std::string& prompt);

    // Compress older messages into a summary using the LLM.
    // When history exceeds max_messages, this summarizes the oldest half and
    // replaces it with a single summary message. Returns true if compression happened.
    bool summarize(LLMClient& llm);

private:
    std::vector<ChatMessage> history_;
    int max_messages_;
};

} // namespace agent
