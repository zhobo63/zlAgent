#include <catch2/catch_all.hpp>
#include "tools.h"

using namespace agent;

// ── SearchMemoriesTool (JSON parsing + error paths only) ───────
// Note: requires global LongTermMemory; we test JSON validation and null-pointer path.

TEST_CASE("SearchMemoriesTool: name and description", "[tool][memory]") {
    auto tool = create_search_memories_tool();
    REQUIRE(tool->name() == "search_memories");
    CHECK(!tool->description().empty());
}

TEST_CASE("SearchMemoriesTool: empty input returns error", "[tool][memory]") {
    auto tool = create_search_memories_tool();
    std::string result = tool->execute("");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("SearchMemoriesTool: invalid JSON returns error", "[tool][memory]") {
    auto tool = create_search_memories_tool();
    std::string result = tool->execute("not json");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("SearchMemoriesTool: empty query returns error", "[tool][memory]") {
    auto tool = create_search_memories_tool();
    std::string result = tool->execute(R"({"query": ""})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("SearchMemoriesTool: missing global LTM returns error", "[tool][memory]") {
    // Without set_global_long_term_memory(), this should return an error.
    auto tool = create_search_memories_tool();
    std::string result = tool->execute(R"({"query": "test"})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("SearchMemoriesTool: custom top_k", "[tool][memory]") {
    auto tool = create_search_memories_tool();
    // Will fail on null LTM, but we verify the JSON is parsed without crash.
    std::string result = tool->execute(R"({"query": "test", "top_k": 10})");
    CHECK(result.find("Error") != std::string::npos);
}

// ── RecallFactsTool (JSON parsing + error paths only) ──────────

TEST_CASE("RecallFactsTool: name and description", "[tool][memory]") {
    auto tool = create_recall_facts_tool();
    REQUIRE(tool->name() == "recall_facts");
    CHECK(!tool->description().empty());
}

TEST_CASE("RecallFactsTool: empty input returns error", "[tool][memory]") {
    auto tool = create_recall_facts_tool();
    std::string result = tool->execute("");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("RecallFactsTool: invalid JSON returns error", "[tool][memory]") {
    auto tool = create_recall_facts_tool();
    std::string result = tool->execute("not json");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("RecallFactsTool: missing global LTM returns error", "[tool][memory]") {
    auto tool = create_recall_facts_tool();
    // Without set_global_long_term_memory(), this should return an error.
    std::string result = tool->execute(R"({})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("RecallFactsTool: with prefix", "[tool][memory]") {
    auto tool = create_recall_facts_tool();
    // Will fail on null LTM, but we verify the JSON is parsed without crash.
    std::string result = tool->execute(R"({"prefix": "project."})");
    CHECK(result.find("Error") != std::string::npos);
}
