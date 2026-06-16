#pragma once

#include <string>
#include <vector>
#include "llm_client.h"
#include "token_counter.h"

namespace agent {

/**
 * Manages conversation history with LLM-based summarization.
 * When history exceeds max_messages, older messages are compressed into a
 * summary via the LLM to preserve context without wasting tokens.
 */
class Memory {
public:
    explicit Memory(int max_messages = 50);

    // Add a message to history (token count is updated incrementally)
    void add(const ChatMessage& msg);

    // Get all stored messages
    std::vector<ChatMessage> get_messages() const;

    // Clear history
    void clear();

    // Set system prompt
    void set_system_prompt(const std::string& prompt);

    // Compress older messages into a summary using the LLM.
    // When history exceeds max_messages, this summarizes the oldest half and
    // replaces it with a single summary message. Returns true if compression happened.
    bool summarize(LLMClient& llm);

    /// Cached token count for the current conversation (incrementally maintained).
    size_t get_cached_token_count() const { return cached_tokens_; }

private:
    std::vector<ChatMessage> history_;
    int max_messages_;
    size_t cached_tokens_ = 0;  // running estimate of total tokens in history_
};

} // namespace agent
