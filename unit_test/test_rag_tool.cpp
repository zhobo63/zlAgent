#include "pch.h"
#include "unit_test.h"
// #include "tools/rag_tool.h" // 如果頭檔存在則取消註解
#include <safety_guard.h>
#include <tools.h>
#include <embedding_provider.h>
#include <rag_manager.h>

using namespace agent;
namespace fs = std::filesystem;
using json = nlohmann::json;

// Mock embedding provider that returns deterministic 2D vectors.
class MockEmbeddingProvider : public EmbeddingProvider {
public:
    std::vector<float> embed(const std::string& text) const override {
        unsigned int h = 0;
        for (char c : text) h += static_cast<unsigned char>(c);
        float x = static_cast<float>((h & 0xFF)) / 255.0f;
        float y = static_cast<float>(((h >> 8) & 0xFF)) / 255.0f;
        float norm = std::sqrt(x * x + y * y);
        if (norm > 1e-9f) { x /= norm; y /= norm; }
        return {x, y};
    }

    std::vector<std::vector<float>> embed_batch(
        const std::vector<std::string>& texts) const override {
        std::vector<std::vector<float>> results;
        for (const auto& t : texts) results.push_back(embed(t));
        return results;
    }

    int dimension() const override { return 2; }
};

static void safe_remove_all(const std::string& path)
{
    try {
        if (fs::exists(path)) fs::remove_all(path);
    } catch (...) {}
}

static void test_search_knowledge_base_tool(UnitReport& parent);

void test_rag_tool(UnitReport& parent)
{
    // Ensure SafetyGuard whitelist contains current path for tests.
    auto& sg = SafetyGuard::get_instance();
    sg.set_path_whitelist({fs::current_path().string()});

    UnitReport unit("rag_tool");
    LOG_INFO("rag_tool", "entry");

    test_search_knowledge_base_tool(unit);

    parent.report.push_back(unit);
}

static void test_search_knowledge_base_tool(UnitReport& parent)
{
    UnitReport unit("search_knowledge_base_tool");
    LOG_INFO("search_knowledge_base_tool", "entry");

    // Set up a mock RAG manager for tests that require it.
    MockEmbeddingProvider provider;
    RAGManager::Config cfg;
    cfg.min_score = 0.0f; // accept all results for testing
    RAGManager rag_manager(&provider, cfg);
    set_global_rag_manager(&rag_manager);

    auto tool = create_search_knowledge_base_tool();

    // 8. Tool name correct
    {
        UNIT_TEST("name_is_correct", tool->name() == "search_knowledge_base");
    }

    // 1. Basic success: valid query returns formatted results (含 "Found X result(s)")
    {
        LOG_INFO("search_knowledge_base_tool", "basic_success");
        json args;
        args["query"] = "test_keyword";
        args["top_k"] = 5;
        auto result = tool->execute(args.dump());
        UNIT_TEST("no_error_on_valid_query", result.find("Error") == std::string::npos);
    }

    // 2. Nested/Recursive: top_k limit handling (top_k = -1 → k becomes negative, loop doesn't execute)
    {
        LOG_INFO("search_knowledge_base_tool", "limit_boundary");
        json args;
        args["query"] = "test_keyword";
        args["top_k"] = -1; // Negative limit: std::min returns negative, for loop doesn't execute
        auto result = tool->execute(args.dump());
        UNIT_TEST("no_error_on_negative_limit", result.find("Error") == std::string::npos);
    }

    // 3. Existing path handling: query with short words only (words < 3 chars are ignored)
    {
        LOG_INFO("search_knowledge_base_tool", "short_words_only");
        json args;
        args["query"] = "ab cd ef"; // All words are less than 3 characters
        args["top_k"] = 5;
        auto result = tool->execute(args.dump());
        UNIT_TEST("no_error_on_short_words", result.find("Error") == std::string::npos);
    }

    // 4. Existing path handling: query with matches but limited by top_k
    {
        LOG_INFO("search_knowledge_base_tool", "limit_applied");
        json args;
        args["query"] = "test_keyword";
        args["top_k"] = 1; // Limit to 1 result
        auto result = tool->execute(args.dump());
        UNIT_TEST("no_error_on_limited_query", result.find("Error") == std::string::npos);
    }

    // 5. Non-existent path error: query with no matches → "No relevant results found"
    {
        LOG_INFO("search_knowledge_base_tool", "no_results");
        json args;
        args["query"] = "nonexistent_keyword_xyz_123"; // Unlikely to exist
        args["top_k"] = 5;
        auto result = tool->execute(args.dump());
        UNIT_TEST("no_error_on_no_results", result.find("Error") == std::string::npos);
    }

    // 6. Empty parameter error: empty query → "Error: Query is required."
    {
        LOG_INFO("search_knowledge_base_tool", "empty_query");
        json args;
        args["query"] = "";
        auto result = tool->execute(args.dump());
        UNIT_TEST("error_on_empty_query", result.find("Error") != std::string::npos);
    }

    // 7. Invalid JSON error: malformed input → "Error: Invalid JSON arguments"
    {
        LOG_INFO("search_knowledge_base_tool", "invalid_json");
        auto result = tool->execute("not json");
        UNIT_TEST("error_on_invalid_json", result.find("Error") != std::string::npos);
    }

    // 8. Empty input error: empty string → "Error: Invalid JSON arguments - empty input"
    {
        LOG_INFO("search_knowledge_base_tool", "empty_input");
        auto result = tool->execute("");
        UNIT_TEST("error_on_empty_input", result.find("Error") != std::string::npos);
    }

    // 9. Knowledge base not initialized: RAG manager is null → "Error: Knowledge base not initialized."
    {
        LOG_INFO("search_knowledge_base_tool", "rag_not_initialized");
        set_global_rag_manager(nullptr); // temporarily clear
        auto result = tool->execute(R"({"query": "test"})");
        UNIT_TEST("error_on_rag_not_initialized", result.find("Error: Knowledge base not initialized.") != std::string::npos);
        set_global_rag_manager(&rag_manager); // restore
    }

    parent.report.push_back(unit);
}
