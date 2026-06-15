#include "pch.h"

#include "tool.h"
#include "json.hpp"
#include "rag_manager.h"

namespace agent {
using json = nlohmann::json;

// Global RAG manager pointer (set by main.cpp).
static RAGManager* g_rag_manager = nullptr;

void set_global_rag_manager(RAGManager* mgr) {
    g_rag_manager = mgr;
}

RAGManager* get_global_rag_manager() {
    return g_rag_manager;
}

// -----------------------------------------------------------------------
// SearchKnowledgeBaseTool - LLM can call this to search the knowledge base
// -----------------------------------------------------------------------
class SearchKnowledgeBaseTool : public Tool {
public:
    std::string name() const override { return "search_knowledge_base"; }
    std::string description() const override {
        return "Search the knowledge base for relevant information using semantic similarity. "
               "Use this when you need to recall facts, documentation, or context that may not be in your immediate memory. "
               "Returns ranked results with relevance scores.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["query"]["type"] = "string";
        schema["properties"]["query"]["description"] = "The search query in natural language";
        schema["properties"]["top_k"]["type"] = "integer";
        schema["properties"]["top_k"]["description"] = "Maximum number of results to return (default: 5)";
        schema["required"] = {"query"};
        return schema.dump();
    }

    std::string execute(const std::string& json_args) override {
        try {
            auto args = json::parse(json_args);
            std::string query = args.value("query", "");
            int top_k = args.value("top_k", -1);

            if (query.empty()) return "Error: Query is required.";
            if (!g_rag_manager) return "Error: Knowledge base not initialized.";

            auto results = g_rag_manager->search(query, top_k);
            if (results.empty()) {
                return "No relevant results found for query: \"" + query + "\"";
            }

            std::ostringstream oss;
            oss << "Found " << results.size() << " result(s) for query: \"" << query << "\"\n\n";
            for (size_t i = 0; i < results.size(); ++i) {
                const auto& r = results[i];
                oss << "--- Result " << (i + 1) << " (score: " << std::fixed
                    << static_cast<double>(r.score) << ") ---\n";
                oss << "Source: " << r.source << "\n";
                // Truncate content for readability.
                std::string preview = r.content;
                if (preview.size() > 800) {
                    preview.resize(800);
                    preview += "...";
                }
                oss << "Content:\n" << preview << "\n\n";
            }
            return oss.str();

        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }
};

ToolPtr create_search_knowledge_base_tool() {
    return std::make_shared<SearchKnowledgeBaseTool>();
}

} // namespace agent
