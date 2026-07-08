#include <catch2/catch_all.hpp>
#include "tools.h"

using namespace agent;

// ── CreateSkillTool (JSON parsing + error paths only) ──────────
// Note: requires global SkillRegistry; we test JSON validation and null-pointer path.

TEST_CASE("CreateSkillTool: name and description", "[tool][skill]") {
    auto tool = create_create_skill_tool();
    REQUIRE(tool->name() == "create_skill");
    CHECK(!tool->description().empty());
}

TEST_CASE("CreateSkillTool: empty input returns error", "[tool][skill]") {
    auto tool = create_create_skill_tool();
    std::string result = tool->execute("");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("CreateSkillTool: invalid JSON returns error", "[tool][skill]") {
    auto tool = create_create_skill_tool();
    std::string result = tool->execute("not json");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("CreateSkillTool: missing name returns error", "[tool][skill]") {
    auto tool = create_create_skill_tool();
    std::string result = tool->execute(R"({"description": "test", "when_to_use": "always", "instructions": "do it"})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("CreateSkillTool: missing description returns error", "[tool][skill]") {
    auto tool = create_create_skill_tool();
    std::string result = tool->execute(R"({"name": "test", "when_to_use": "always", "instructions": "do it"})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("CreateSkillTool: missing when_to_use returns error", "[tool][skill]") {
    auto tool = create_create_skill_tool();
    std::string result = tool->execute(R"({"name": "test", "description": "desc", "instructions": "do it"})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("CreateSkillTool: missing instructions returns error", "[tool][skill]") {
    auto tool = create_create_skill_tool();
    std::string result = tool->execute(R"({"name": "test", "description": "desc", "when_to_use": "always"})");
    CHECK(result.find("Error") != std::string::npos);
}

// ── DeleteSkillTool (JSON parsing + error paths only) ──────────

TEST_CASE("DeleteSkillTool: name and description", "[tool][skill]") {
    auto tool = create_delete_skill_tool();
    REQUIRE(tool->name() == "delete_skill");
    CHECK(!tool->description().empty());
}

TEST_CASE("DeleteSkillTool: empty input returns error", "[tool][skill]") {
    auto tool = create_delete_skill_tool();
    std::string result = tool->execute("");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("DeleteSkillTool: invalid JSON returns error", "[tool][skill]") {
    auto tool = create_delete_skill_tool();
    std::string result = tool->execute("not json");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("DeleteSkillTool: missing name returns error", "[tool][skill]") {
    auto tool = create_delete_skill_tool();
    std::string result = tool->execute(R"({})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("DeleteSkillTool: missing global registry returns error", "[tool][skill]") {
    auto tool = create_delete_skill_tool();
    // Without set_global_skill_registry(), this should return an error.
    std::string result = tool->execute(R"({"name": "nonexistent"})");
    CHECK(result.find("Error") != std::string::npos);
}

// ── ReloadSkillsTool (JSON parsing + error paths only) ─────────

TEST_CASE("ReloadSkillsTool: name and description", "[tool][skill]") {
    auto tool = create_reload_skills_tool();
    REQUIRE(tool->name() == "reload_skills");
    CHECK(!tool->description().empty());
}

TEST_CASE("ReloadSkillsTool: missing global registry returns error", "[tool][skill]") {
    auto tool = create_reload_skills_tool();
    // Without set_global_skill_registry(), this should return an error.
    std::string result = tool->execute("");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("ReloadSkillsTool: with scan_dirs", "[tool][skill]") {
    auto tool = create_reload_skills_tool();
    // Will fail on null registry, but we verify the JSON is parsed without crash.
    std::string result = tool->execute(R"({"scan_dirs": ["/some/dir"]})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("ReloadSkillsTool: invalid JSON returns error", "[tool][skill]") {
    auto tool = create_reload_skills_tool();
    std::string result = tool->execute("not json");
    CHECK(result.find("Error") != std::string::npos);
}
