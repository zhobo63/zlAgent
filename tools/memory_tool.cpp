#include "pch.h"

#include "tool.h"
#include "json.hpp"
#include "long_term_memory.h"

namespace agent {
using json = nlohmann::json;

// -----------------------------------------------------------------------
// SearchMemoriesTool - semantic search over past session summaries via RAG
// -----------------------------------------------------------------------
class SearchMemoriesTool : public Tool {
    LongTermMemory* ltm_;  // non-owning; lifetime managed by Agent

    struct ScoredSession {
        float score;
        SessionSummary session;
    };
public:
    explicit SearchMemoriesTool(LongTermMemory* memory) : ltm_(memory) {}

    std::string name() const override { return "search_memories"; }
    std::string description() const override {
        return "Search past conversation sessions for relevant context. "
               "Use this when you need to recall what was discussed, decided, or done in previous sessions.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["query"]["type"] = "string";
        schema["properties"]["query"]["description"] = "What you're looking for (natural language)";
        schema["properties"]["top_k"]["type"] = "integer";
        schema["properties"]["top_k"]["description"] = "Maximum number of results to return (default: 3)";
        schema["required"] = {"query"};
        return schema.dump();
    }

    std::string execute(const std::string& json_args) override {
        try {
            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                return "Error: Invalid JSON arguments - not json";
            }
        std::string query = args.value("query", "");
        int top_k = args.value("top_k", 3);

            if (query.empty()) return "Error: Query is required.";
            if (!ltm_) return "Error: Long-term memory not initialized.";

            auto sessions = ltm_->get_recent_sessions(top_k * 2);
            if (sessions.empty()) {
                return "No past sessions found in long-term memory.";
            }

            // Simple keyword matching - for semantic search, use RAG's search_knowledge_base instead.
            std::string query_lower = query;
            std::transform(query_lower.begin(), query_lower.end(), query_lower.begin(), ::tolower);

            struct ScoredSession {
                float score;
                SessionSummary session;
            };
            std::vector<ScoredSession> scored;

            for (const auto& s : sessions) {
                // Score based on keyword overlap.
                std::string topic_lower = s.topic;
                std::transform(topic_lower.begin(), topic_lower.end(), topic_lower.begin(), ::tolower);
                std::string summary_lower = s.summary;
                std::transform(summary_lower.begin(), summary_lower.end(), summary_lower.begin(), ::tolower);

                int matches = 0;
                // Split query into words and count matches.
                std::istringstream iss(query_lower);
                std::string word;
                while (iss >> word) {
                    if (word.size() < 3) continue;
                    if (topic_lower.find(word) != std::string::npos) matches++;
                    if (summary_lower.find(word) != std::string::npos) matches++;
                }

                float score = static_cast<float>(matches);
                if (score > 0.5f) {
                    ScoredSession se;
                    se.score = score;
                    se.session = s;
                    scored.push_back(se);
                }
            }

            // Sort by score descending.
            std::sort(scored.begin(), scored.end(),
                     [](const ScoredSession& a, const ScoredSession& b) { return a.score > b.score; });

            int k = std::min(top_k, static_cast<int>(scored.size()));
            if (k == 0) {
                return "No relevant past sessions found for query: \"" + query + "\"";
            }

            std::ostringstream oss;
            oss << "Found " << k << " relevant session(s):\n\n";
            for (int i = 0; i < k; ++i) {
                const auto& s = scored[i].session;
                std::string date = s.timestamp.substr(0, 10);
                oss << "--- Session " << (i + 1) << " ---\n";
                oss << "Date: " << date << "\n";
                oss << "Topic: " << s.topic << "\n";
                std::string preview = s.summary;
                if (preview.size() > 600) {
                    preview.resize(600);
                    preview += "...";
                }
                oss << "Summary:\n" << preview << "\n\n";
            }

            return oss.str();

        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }
};

// -----------------------------------------------------------------------
// RecallFactsTool - retrieve structured facts by key prefix
// -----------------------------------------------------------------------
class RecallFactsTool : public Tool {
    LongTermMemory* ltm_;  // non-owning; lifetime managed by Agent
public:
    explicit RecallFactsTool(LongTermMemory* memory) : ltm_(memory) {}

    std::string name() const override { return "recall_facts"; }
    std::string description() const override {
        return "Retrieve stored facts from long-term memory. "
               "Use a key prefix to filter (e.g., 'project.' for project-related facts). "
               "Leave empty to get all facts.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["prefix"]["type"] = "string";
        schema["properties"]["prefix"]["description"] = "Key prefix to filter (e.g., 'project.', empty for all)";
        return schema.dump();
    }

    std::string execute(const std::string& json_args) override {
        try {
            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                return "Error: Invalid JSON arguments - not json";
            }
            std::string prefix = args.value("prefix", "");

            if (!ltm_) return "Error: Long-term memory not initialized.";

            auto facts = ltm_->get_facts(prefix);
            if (facts.empty()) {
                return prefix.empty()
                    ? "No facts stored in long-term memory."
                    : "No facts found with prefix: \"" + prefix + "\"";
            }

            std::ostringstream oss;
            oss << "Found " << facts.size() << " fact(s):\n\n";
            for (const auto& f : facts) {
                oss << "- **" << f.key << "** = " << f.value;
                if (!f.source_session.empty()) {
                    oss << " [from " << f.source_session.substr(0, 10) << "]";
                }
                oss << "\n";
            }

            return oss.str();

        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }
};

ToolPtr create_search_memories_tool(LongTermMemory* ltm) {
    return std::make_shared<SearchMemoriesTool>(ltm);
}

ToolPtr create_recall_facts_tool(LongTermMemory* ltm) {
    return std::make_shared<RecallFactsTool>(ltm);
}

} // namespace agent
