#include "pch.h"

#include "memory.h"

namespace agent {

Memory::Memory(int max_messages) : max_messages_(max_messages) {}

void Memory::add(ChatMessage msg) {
    cached_tokens_ += TokenCounter::estimate_message(msg);
    history_.push_back(std::move(msg));
}

const std::vector<ChatMessage>& Memory::get_messages() const {
    return history_;
}

void Memory::clear() {
    history_.clear();
    cached_tokens_ = 0;
}

void Memory::set_system_prompt(const std::string& prompt) {
    // Remove existing system prompt if any
    auto it = std::find_if(history_.begin(), history_.end(),
        [](const ChatMessage& m) { return m.role == "system"; });
    if (it != history_.end()) {
        // Subtract old message tokens, then add new ones
        cached_tokens_ -= TokenCounter::estimate_message(*it);
        *it = ChatMessage{"system", prompt, ""};
        cached_tokens_ += TokenCounter::estimate_message(*it);
    } else {
        // Insert at the beginning
        history_.insert(history_.begin(), ChatMessage{"system", prompt, ""});
        cached_tokens_ += TokenCounter::estimate_message(ChatMessage{"system", prompt, ""});
    }
}

// ---------------------------------------------------------------------------
// Summarize: compress older messages into a single summary via the LLM.
// Strategy: keep system prompt + most recent half; summarize the rest.
// ---------------------------------------------------------------------------

bool Memory::summarize(LLMClient& llm) {
    if (static_cast<int>(history_.size()) <= max_messages_) {
        return false;  // no need to compress yet
    }

    // Find system prompt index (always keep it at the front)
    size_t sys_idx = 0;
    for (size_t i = 0; i < history_.size(); i++) {
        if (history_[i].role == "system") {
            sys_idx = i;
            break;
        }
    }

    // Determine how many messages to keep at the tail (recent half)
    size_t total = history_.size();
    size_t recent_count = std::max(size_t(10), total / 2);  // keep at least 10 recent msgs
    size_t summary_start = sys_idx + 1;                     // after system prompt
    size_t summary_end   = total - recent_count;            // before the recent tail

    if (summary_end <= summary_start) {
        return false;  // nothing to summarize
    }

    // Build a list of messages to summarize and subtract their token count
    std::vector<ChatMessage> msgs_to_summarize;
    for (size_t i = summary_start; i < summary_end; i++) {
        cached_tokens_ -= TokenCounter::estimate_message(history_[i]);
        msgs_to_summarize.push_back(history_[i]);
    }

    // Ask the LLM to produce a concise summary
    ChatMessage sys_msg{"system",
        "You are a summarizer. Summarize the following conversation into 3-5 bullet points. "
        "Preserve key facts, decisions, code changes, and important context. "
        "Be concise but complete."};

    // Per-message token budget for the summary request — enough context without
    // wasting tokens on very long messages (e.g. large code blocks).
    constexpr size_t SUMMARY_MSG_TOKEN_BUDGET = 500;

    std::vector<ChatMessage> prompt;
    prompt.push_back(sys_msg);
    for (const auto& m : msgs_to_summarize) {
        // Truncate long messages based on estimated tokens, not raw bytes,
        // so code blocks and important context aren't cut off arbitrarily.
        std::string content = m.content;
        size_t msg_tokens = TokenCounter::estimate(content);
        if (msg_tokens > SUMMARY_MSG_TOKEN_BUDGET) {
            // Truncate proportionally: keep roughly the same ratio of bytes
            // as the budget-to-actual token ratio. This avoids repeated calls
            // to estimate() and gives a reasonable truncation point.
            size_t trunc_bytes = static_cast<size_t>(
                content.size() * static_cast<double>(SUMMARY_MSG_TOKEN_BUDGET) / msg_tokens);
            if (trunc_bytes < 128) trunc_bytes = 128;  // minimum context
            content = content.substr(0, trunc_bytes) + "... [truncated]";
        }
        prompt.push_back(ChatMessage{m.role, content, m.name});
    }

    auto resp = llm.chat(prompt);

    if (resp.content.empty()) {
        return false;  // summarization failed
    }

    // Replace the summarized range with a single summary message
    ChatMessage summary_msg{"assistant", "[Summary of earlier conversation]\n" + resp.content};
    cached_tokens_ += TokenCounter::estimate_message(summary_msg);

    history_.erase(history_.begin() + static_cast<long>(summary_start),
                   history_.begin() + static_cast<long>(summary_end));
    history_.insert(history_.begin() + static_cast<long>(summary_start), summary_msg);

    return true;
}

} // namespace agent
