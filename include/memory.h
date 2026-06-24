#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include "llm_client.h"
#include "token_counter.h"

namespace agent {

/**
 * Manages conversation history with LLM-based summarization.
 *
 * When history exceeds max_messages, older messages are compressed into a
 * summary via the LLM to preserve context without wasting tokens.
 */
class Memory {
public:
    static constexpr int  DEFAULT_MAX_MESSAGES = 50;     // trigger summarization at this count
    static constexpr int  MIN_RECENT_KEEP      = 10;     // always keep at least N recent messages
    static constexpr size_t MAX_SUMMARY_INPUT   = 2048;  // truncate individual messages for summary
    static constexpr size_t MAX_TOKENS_BEFORE_SUMMARIZE = 65536; // trigger summarization when total tokens exceed this

    explicit Memory(int max_messages = DEFAULT_MAX_MESSAGES);

    /// Add a message by value (supports move semantics).
    void add(ChatMessage msg);

    /// Get all stored messages as a const reference (avoids copying).
    const std::vector<ChatMessage>& get_messages() const;

    /// Clear history.
    void clear();

    /// Set or update the system prompt in-place, adjusting token count accordingly.
    void set_system_prompt(const std::string& prompt);

    /// Compress older messages into a summary using the LLM.
    /// When history exceeds max_messages, this summarizes the oldest half and
    /// replaces it with a single summary message. Returns true if compression happened.
    bool summarize(LLMClient& llm);

    /// Cached token count for the current conversation (incrementally maintained).
    size_t get_cached_token_count() const { return cached_tokens_; }

private:
    std::vector<ChatMessage> history_;
    int max_messages_ = DEFAULT_MAX_MESSAGES;
    size_t cached_tokens_ = 0;  // running estimate of total tokens in history_
};

} // namespace agent
