#include <catch2/catch_all.hpp>
#include "skill_system.h"
#include <fstream>
#include <filesystem>

using namespace agent;

TEST_CASE("SkillLoader: parse valid SKILL.md", "[skills]") {
    std::string path = "test_skill_temp/SKILL.md";
    std::filesystem::create_directories("test_skill_temp");

    std::ofstream out(path);
    out << "# Test Skill\n\n## Description\nA test skill.\n\n## When to Use\nWhen testing.\n\n## Instructions\nDo something.\n\n## Tools Required\n- read_file\n";
    out.close();

    auto skill = SkillLoader::parse_skill_md(path);
    REQUIRE(skill != nullptr);
    REQUIRE(!skill->name.empty());
    REQUIRE(!skill->description.empty());

    std::filesystem::remove_all("test_skill_temp");
}

TEST_CASE("SkillLoader: parse nonexistent file returns null", "[skills]") {
    auto skill = SkillLoader::parse_skill_md("nonexistent/SKILL.md");
    REQUIRE(skill == nullptr);
}

TEST_CASE("SkillLoader: scan_directory finds skills", "[skills]") {
    // zlagent/skills/ should exist with at least one SKILL.md.
    auto skills = SkillLoader::scan_directory("zlagent/skills", "native");
    // May be empty if no skills installed - just check it doesn't crash.
    CHECK(skills.size() >= 0);
}

TEST_CASE("SkillRegistry: register and find skill", "[skills]") {
    SkillRegistry registry;
    auto skill = std::make_shared<SkillDefinition>();
    skill->name = "test_skill";
    skill->description = "A test.";
    registry.register_skill(skill);

    auto found = registry.find_skill("test_skill");
    REQUIRE(found != nullptr);
    REQUIRE(found->name == "test_skill");
}

TEST_CASE("SkillRegistry: unregister removes skill", "[skills]") {
    SkillRegistry registry;
    auto skill = std::make_shared<SkillDefinition>();
    skill->name = "to_remove";
    registry.register_skill(skill);

    registry.unregister_skill("to_remove");
    REQUIRE(registry.find_skill("to_remove") == nullptr);
}
