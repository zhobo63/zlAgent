#include <catch2/catch_all.hpp>
#include "tools.h"
#include "skill_system.h"

using namespace agent;

// ── GetSkillTool ───────────────────────────────────────────────

TEST_CASE("GetSkillTool: name and description", "[tool][skill]") {
    auto tool = create_get_skill_tool();
    REQUIRE(tool->name() == "get_skill");
    CHECK(!tool->description().empty());
}

TEST_CASE("GetSkillTool: missing global registry returns error", "[tool][skill]") {
    auto tool = create_get_skill_tool();
    std::string result = tool->execute(R"({"name": "test"})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("GetSkillTool: empty input returns error", "[tool][skill]") {
    auto tool = create_get_skill_tool();
    std::string result = tool->execute("");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("GetSkillTool: invalid JSON returns error", "[tool][skill]") {
    auto tool = create_get_skill_tool();
    std::string result = tool->execute("not json");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("GetSkillTool: missing name returns error", "[tool][skill]") {
    auto tool = create_get_skill_tool();
    std::string result = tool->execute(R"({})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("GetSkillTool: returns full skill content when registered", "[tool][skill]") {
    SkillRegistry registry;
    set_global_skill_registry(&registry);

    // Create a temporary SKILL.md on disk.
    std::string dir = "test_get_skill_temp";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(dir);

    std::ofstream out(fs::path(dir) / "SKILL.md");
    out << "# Test Skill\n\n";
    out << "## Description\nA test skill.\n\n";
    out << "## When to Use\nWhen testing.\n\n";
    out << "## Instructions\n1. Do step one\n2. Do step two\n\n";
    out << "## Tools Required\n- read_file\n\n";
    out << "## Configuration\nmax_retries: 3\n";
    out.close();

    auto skill = std::make_shared<SkillDefinition>();
    skill->name = "test_skill";
    skill->source_path = dir;
    registry.register_skill(skill);

    auto tool = create_get_skill_tool();
    std::string result = tool->execute(R"({"name": "test_skill"})");

    CHECK(result.find("# Test Skill") != std::string::npos);
    CHECK(result.find("## Description") != std::string::npos);
    CHECK(result.find("A test skill.") != std::string::npos);
    CHECK(result.find("## Instructions") != std::string::npos);
    CHECK(result.find("1. Do step one") != std::string::npos);
    CHECK(result.find("2. Do step two") != std::string::npos);

    fs::remove_all(dir);
    set_global_skill_registry(nullptr);
}

TEST_CASE("GetSkillTool: missing SKILL.md returns error", "[tool][skill]") {
    SkillRegistry registry;
    set_global_skill_registry(&registry);

    auto skill = std::make_shared<SkillDefinition>();
    skill->name = "no_file";
    skill->source_path = "/nonexistent/path";
    registry.register_skill(skill);

    auto tool = create_get_skill_tool();
    std::string result = tool->execute(R"({"name": "no_file"})");

    CHECK(result.find("Error") != std::string::npos);
    CHECK(result.find("not found") != std::string::npos || result.find("SKILL.md") != std::string::npos);

    set_global_skill_registry(nullptr);
}

TEST_CASE("GetSkillTool: not found lists available skills", "[tool][skill]") {
    SkillRegistry registry;
    set_global_skill_registry(&registry);

    auto skill = std::make_shared<SkillDefinition>();
    skill->name = "existing_skill";
    skill->description = "An existing skill.";
    skill->enabled = true;
    registry.register_skill(skill);

    auto tool = create_get_skill_tool();
    std::string result = tool->execute(R"({"name": "nonexistent"})");

    CHECK(result.find("Error") != std::string::npos);
    CHECK(result.find("not found") != std::string::npos);
    CHECK(result.find("Available skills:") != std::string::npos);
    CHECK(result.find("existing_skill") != std::string::npos);

    set_global_skill_registry(nullptr);
}

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
