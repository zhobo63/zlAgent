#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include "llm_client.h"

namespace agent {

/**
 * Cumulative token usage tracker.
 */
struct TokenUsage {
    size_t prompt_tokens = 0;       // tokens in the request prompt
    size_t completion_tokens = 0;   // tokens returned by the LLM
    size_t total_tokens = 0;        // running total across all calls

    void add(size_t prompt, size_t completion) {
        prompt_tokens += prompt;
        completion_tokens += completion;
        total_tokens += prompt + completion;
    }
};

/**
 * Lightweight heuristic token estimator.
 *
 * Not a real BPE tokenizer — uses per-character weight tables to approximate
 * the token count that GPT-style models would produce.  Good enough for
 * budgeting, Memory compression triggers, and CLI /stats display.
 */
class TokenCounter {
public:
    // ── Estimation API ─────────────────────────────────────

    /// Estimate tokens in a plain text string.
    static size_t estimate(const std::string& text);

    /// Estimate tokens for a single ChatMessage (includes ~4 token role overhead).
    static size_t estimate_message(const ChatMessage& msg);

    /// Estimate total tokens for an entire conversation.
    static size_t estimate_conversation(const std::vector<ChatMessage>& messages);

    // ── API usage extraction ───────────────────────────────

    /// Extract real token counts from the "usage" field of an OpenAI-compatible
    /// JSON response body.  Returns {prompt, completion} or {0, 0} on failure.
    static std::pair<size_t, size_t> from_api_usage(const std::string& json_body);

private:
    TokenCounter() = default;

    // Decode a single UTF-8 codepoint starting at *pos and advance pos.
    // Returns the byte length (1–4) or 0 on malformed input.
    static int decode_utf8_codepoint(const char* data, size_t len, size_t& pos);

    // Return the heuristic weight for a decoded Unicode codepoint.
    static double weight_for_codepoint(uint32_t cp);
};

} // namespace agent
