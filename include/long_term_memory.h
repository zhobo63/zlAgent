#pragma once

#include <string>
#include <vector>
#include <map>
#include "llm_client.h"

namespace agent {

class Memory;
class RAGManager;

/**
 * A summary of a past conversation session.
 */
struct SessionSummary {
    std::string timestamp;          // ISO-8601: "2025-01-15T14:30:00"
    std::string topic;              // session topic (LLM extracted)
    std::string summary;            // concise summary of what happened
    int message_count = 0;          // number of messages in the session
};

/**
 * A structured fact stored for long-term recall.
 */
struct FactEntry {
    std::string key;                // e.g., "project.build_system"
    std::string value;              // e.g., "CMake with vcpkg"
    std::string source_session;     // timestamp of the session that created this fact
    std::string timestamp;          // creation time
};

/**
 * Long-term memory: persists session summaries and structured facts across runs.
 * Integrates with RAG so past sessions can be semantically searched.
 */
class LongTermMemory {
public:
    struct Config {
        std::string store_dir = ".zlagent/memory";
        int max_sessions = 100;              // max session summaries to keep
        bool inject_facts_to_prompt = true;  // append facts to system prompt at startup
        bool auto_extract_facts = true;      // ask LLM to extract facts when saving a session
    };

    explicit LongTermMemory(const Config& cfg = {});

    // ── Session Management ────────────────────────

    // Save current working memory as a session: generate summary + extract facts via LLM.
    void save_session(Memory& working_memory, LLMClient& llm);

    // Get the N most recent session summaries (newest first).
    std::vector<SessionSummary> get_recent_sessions(int n = 10) const;

    // ── Semantic Facts ────────────────────────────

    // Add a structured fact. Overwrites if key already exists.
    void add_fact(const std::string& key, const std::string& value);

    // Retrieve facts whose key starts with the given prefix. Empty prefix = all facts.
    std::vector<FactEntry> get_facts(const std::string& prefix = "") const;

    // Remove a fact by exact key match.
    void remove_fact(const std::string& key);

    // ── Persistence ───────────────────────────────

    // Load from disk (called at startup). Returns true if data was found.
    bool load();

    // Save to disk (called before exit or after changes).
    void save() const;

    // Build a human-readable context string for system prompt injection.
    std::string build_context_string(int recent_n = 5) const;

    // ── RAG Integration ───────────────────────────

    // Inject all session summaries into the RAG knowledge base so they can be
    // semantically searched via search_knowledge_base tool.
    void integrate_with_rag(RAGManager* rag_manager);

private:
    Config cfg_;
    std::vector<SessionSummary> sessions_;   // newest first
    std::map<std::string, FactEntry> facts_;  // key -> fact

    // Generate a concise session summary via LLM.
    static std::string generate_summary(const std::vector<ChatMessage>& messages,
                                        LLMClient& llm);

    // Extract structured key-value facts from the conversation via LLM.
    static std::vector<std::pair<std::string, std::string>> extract_facts(
        const std::vector<ChatMessage>& messages, LLMClient& llm);

    // Current timestamp as ISO-8601 string (local time).
    static std::string current_timestamp();
};

// Global long-term memory accessor (set by main.cpp, used by memory tools).
LongTermMemory* get_global_long_term_memory();
void set_global_long_term_memory(LongTermMemory* ltm);

} // namespace agent
