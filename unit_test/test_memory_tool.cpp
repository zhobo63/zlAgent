#include "pch.h"
#include "unit_test.h"
// #include "tools/memory_tool.h" // 如果頭檔存在則取消註解
#include <safety_guard.h>
#include <long_term_memory.h>
#include <tools.h>

using namespace agent;
namespace fs = std::filesystem;
using json = nlohmann::json;

static void safe_remove_all(const std::string& path)
{
    try {
        if (fs::exists(path)) fs::remove_all(path);
    } catch (...) {}
}

void test_search_memories_tool(UnitReport& parent, LongTermMemory* ltm);
void test_recall_facts_tool(UnitReport& parent, LongTermMemory* ltm);

// Temporary LongTermMemory for testing — scoped to the lifetime of this pointer.
static std::unique_ptr<agent::LongTermMemory> g_test_ltm;

void test_memory_tool(UnitReport& parent)
{
    // Ensure SafetyGuard whitelist contains current path for tests.
    auto& sg = SafetyGuard::get_instance();
    sg.set_path_whitelist({fs::current_path().string()});

    // Initialize a temporary LongTermMemory so the memory tools don't error out.
    agent::LongTermMemory::Config cfg;
    cfg.store_dir = ".zlagent/test_memory";
    g_test_ltm = std::make_unique<agent::LongTermMemory>(cfg);

    UnitReport unit("memory_tool");
    LOG_INFO("memory_tool", "entry");

    test_search_memories_tool(unit, g_test_ltm.get());
    test_recall_facts_tool(unit, g_test_ltm.get());

    // Cleanup.
    g_test_ltm.reset();

    parent.report.push_back(unit);
}

void test_search_memories_tool(UnitReport& parent, LongTermMemory* ltm)
{
    UnitReport unit("search_memories_tool");
    LOG_INFO("search_memories_tool", "entry");

    auto tool = create_search_memories_tool(ltm);

    // 8. Tool name correct
    {
        UNIT_TEST("name_is_correct", tool->name() == "search_memories");
    }

    // 1. Basic success: valid query returns formatted results (含 "Found X relevant session(s)")
    {
        LOG_INFO("search_memories_tool", "basic_success");
        json args;
        args["query"] = "test_keyword";
        args["top_k"] = 5;
        auto result = tool->execute(args.dump());
        UNIT_TEST("no_error_on_valid_query", result.find("Error") == std::string::npos);
    }

    // 2. Nested/Recursive: top_k limit handling (top_k = -1 → k becomes negative, loop doesn't execute)
    {
        LOG_INFO("search_memories_tool", "limit_boundary");
        json args;
        args["query"] = "test_keyword";
        args["top_k"] = -1; // Negative limit: std::min returns negative, for loop doesn't execute
        auto result = tool->execute(args.dump());
        UNIT_TEST("no_error_on_negative_limit", result.find("Error") == std::string::npos);
    }

    // 3. Existing path handling: query with short words only (words < 3 chars are ignored)
    {
        LOG_INFO("search_memories_tool", "short_words_only");
        json args;
        args["query"] = "ab cd ef"; // All words are less than 3 characters
        args["top_k"] = 5;
        auto result = tool->execute(args.dump());
        UNIT_TEST("no_error_on_short_words", result.find("Error") == std::string::npos);
    }

    // 3. Existing path handling: query with matches but limited by top_k
    {
        LOG_INFO("search_memories_tool", "limit_applied");
        json args;
        args["query"] = "test_keyword";
        args["top_k"] = 1; // Limit to 1 result
        auto result = tool->execute(args.dump());
        UNIT_TEST("no_error_on_limited_query", result.find("Error") == std::string::npos);
    }

    // 4. Non-existent path error: query with no matches → "No relevant past sessions found"
    {
        LOG_INFO("search_memories_tool", "no_results");
        json args;
        args["query"] = "nonexistent_keyword_xyz_123"; // Unlikely to exist
        args["top_k"] = 5;
        auto result = tool->execute(args.dump());
        UNIT_TEST("no_error_on_no_results", result.find("Error") == std::string::npos);
    }

    // 5. Empty parameter error: empty query → "Error: Query is required."
    {
        LOG_INFO("search_memories_tool", "empty_query");
        json args;
        args["query"] = "";
        auto result = tool->execute(args.dump());
        UNIT_TEST("error_on_empty_query", result.find("Error") != std::string::npos);
    }

    // 6. Invalid JSON error: malformed input → "Error: Invalid JSON arguments"
    {
        LOG_INFO("search_memories_tool", "invalid_json");
        auto result = tool->execute("not json");
        UNIT_TEST("error_on_invalid_json", result.find("Error") != std::string::npos);
    }

    // 7. Empty input error: empty string → "Error: Invalid JSON arguments - empty input"
    {
        LOG_INFO("search_memories_tool", "empty_input");
        auto result = tool->execute("");
        UNIT_TEST("error_on_empty_input", result.find("Error") != std::string::npos);
    }

    parent.report.push_back(unit);
}

void test_recall_facts_tool(UnitReport& parent, LongTermMemory* ltm)
{
    UnitReport unit("recall_facts_tool");
    LOG_INFO("recall_facts_tool", "entry");

    auto tool = create_recall_facts_tool(ltm);

    // 8. Tool name correct
    {
        UNIT_TEST("name_is_correct", tool->name() == "recall_facts");
    }

    // 1. Basic success: valid prefix returns formatted results (含 "Found X fact(s)")
    {
        LOG_INFO("recall_facts_tool", "basic_success");
        json args;
        args["prefix"] = "project."; // Assuming a common prefix that might exist
        auto result = tool->execute(args.dump());
        UNIT_TEST("no_error_on_valid_prefix", result.find("Error") == std::string::npos);
    }

    // 2. Nested/Recursive: empty prefix returns all facts (special case - not an error)
    {
        LOG_INFO("recall_facts_tool", "empty_prefix_all_facts");
        json args;
        args["prefix"] = ""; // Empty prefix should return all facts, not error
        auto result = tool->execute(args.dump());
        UNIT_TEST("no_error_on_empty_prefix", result.find("Error") == std::string::npos);
    }

    // 3. Existing path handling: prefix with matches
    {
        LOG_INFO("recall_facts_tool", "prefix_with_matches");
        json args;
        args["prefix"] = "project."; // Assuming a common prefix that might exist
        auto result = tool->execute(args.dump());
        UNIT_TEST("no_error_on_prefix_with_matches", result.find("Error") == std::string::npos);
    }

    // 4. Non-existent path error: prefix with no matches → "No facts found with prefix"
    {
        LOG_INFO("recall_facts_tool", "no_results");
        json args;
        args["prefix"] = "nonexistent_prefix_xyz_123"; 
        auto result = tool->execute(args.dump());
        UNIT_TEST("no_error_on_no_facts", result.find("Error") == std::string::npos);
    }

    // 5. Empty parameter error: NOT an error (empty prefix returns all facts) - this is a special case
    {
        LOG_INFO("recall_facts_tool", "empty_prefix_not_error");
        json args;
        args["prefix"] = ""; // Empty prefix should return all facts, not error
        auto result = tool->execute(args.dump());
        UNIT_TEST("no_error_on_empty_prefix_param", result.find("Error") == std::string::npos);
    }

    // 6. Invalid JSON error: malformed input → "Error: Invalid JSON arguments"
    {
        LOG_INFO("recall_facts_tool", "invalid_json");
        auto result = tool->execute("not json");
        UNIT_TEST("error_on_invalid_json", result.find("Error") != std::string::npos);
    }

    // 7. Empty input error: empty string → "Error: Invalid JSON arguments - empty input"
    {
        LOG_INFO("recall_facts_tool", "empty_input");
        auto result = tool->execute("");
        UNIT_TEST("error_on_empty_input", result.find("Error") != std::string::npos);
    }

    parent.report.push_back(unit);
}
