#include "pch.h"
#include "unit_test.h"
#include "skill_system.h"
#include "safety_guard.h"

using namespace agent;
namespace fs = std::filesystem;

static void safe_remove_all(const std::string& path)
{
    try {
        if (fs::exists(path)) fs::remove_all(path);
    } catch (...) {}
}

// ============================================================
// SkillRegistry tests
// ============================================================

static void test_skill_registry_class(UnitReport& parent)
{
    UnitReport unit("skill_registry_class");
    LOG_INFO("skill_registry", "entry");

    // --- Test 1: register null skill (should not register) ---
    {
        LOG_INFO("skill_registry", "register_null_skill");
        SkillRegistry registry;
        SkillPtr null_skill = nullptr;
        registry.register_skill(null_skill);
        UNIT_TEST("null_not_registered", registry.get_skills().empty());
    }

    // --- Test 2: register skill with empty name (should not register) ---
    {
        LOG_INFO("skill_registry", "register_empty_name");
        SkillRegistry registry;
        auto skill = std::make_shared<SkillDefinition>();
        skill->name = "";
        skill->description = "test description";
        registry.register_skill(skill);
        UNIT_TEST("empty_name_not_registered", registry.get_skills().empty());
    }

    // --- Test 3: register success ---
    {
        LOG_INFO("skill_registry", "register_success");
        SkillRegistry registry;
        auto skill = std::make_shared<SkillDefinition>();
        skill->name = "test_skill";
        skill->description = "A test skill";
        skill->when_to_use = "When testing";
        skill->instructions = "Do something";
        registry.register_skill(skill);

        auto skills = registry.get_skills();
        UNIT_TEST("skill_count_is_1", skills.size() == 1);
        UNIT_TEST("find_skill_found", registry.find_skill("test_skill") != nullptr);
    }

    // --- Test 4: unregister existing skill ---
    {
        LOG_INFO("skill_registry", "unregister_existing");
        SkillRegistry registry;
        auto skill = std::make_shared<SkillDefinition>();
        skill->name = "test_skill";
        skill->description = "A test skill";
        registry.register_skill(skill);

        UNIT_TEST("skill_exists_before", !registry.get_skills().empty());
        registry.unregister_skill("test_skill");
        UNIT_TEST("skill_removed_after", registry.get_skills().empty());
    }

    // --- Test 5: unregister non-existent skill (should not crash) ---
    {
        LOG_INFO("skill_registry", "unregister_nonexistent");
        SkillRegistry registry;
        auto skills_before = registry.get_skills();
        registry.unregister_skill("nonexistent");
        UNIT_TEST("still_empty_after_unregister", registry.get_skills().empty());
    }

    // --- Test 6: get_skills with empty registry ---
    {
        LOG_INFO("skill_registry", "get_skills_empty");
        SkillRegistry registry;
        auto skills = registry.get_skills();
        UNIT_TEST("empty_result", skills.empty());
    }

    // --- Test 7: get_skills with multiple skills ---
    {
        LOG_INFO("skill_registry", "get_skills_with_skills");
        SkillRegistry registry;
        auto skill1 = std::make_shared<SkillDefinition>();
        skill1->name = "skill_a";
        skill1->description = "Skill A";
        auto skill2 = std::make_shared<SkillDefinition>();
        skill2->name = "skill_b";
        skill2->description = "Skill B";
        registry.register_skill(skill1);
        registry.register_skill(skill2);

        auto skills = registry.get_skills();
        UNIT_TEST("two_skills_returned", skills.size() == 2);
    }

    // --- Test 8: find_skill found ---
    {
        LOG_INFO("skill_registry", "find_skill_found");
        SkillRegistry registry;
        auto skill = std::make_shared<SkillDefinition>();
        skill->name = "test_find";
        skill->description = "Find me";
        registry.register_skill(skill);

        auto found = registry.find_skill("test_find");
        UNIT_TEST("found_not_null", found != nullptr);
        UNIT_TEST("found_name_matches", found && found->name == "test_find");
    }

    // --- Test 9: find_skill not found ---
    {
        LOG_INFO("skill_registry", "find_skill_not_found");
        SkillRegistry registry;
        auto skill = std::make_shared<SkillDefinition>();
        skill->name = "test_find";
        skill->description = "Find me";
        registry.register_skill(skill);

        auto found = registry.find_skill("not_exist");
        UNIT_TEST("null_when_not_found", found == nullptr);
    }

    // --- Test 10: build_summary with no skills ---
    {
        LOG_INFO("skill_registry", "build_summary_no_skills");
        SkillRegistry registry;
        auto summary = registry.build_skill_summary();
        UNIT_TEST("has_header", summary.find("Available skills:") != std::string::npos);
        UNIT_TEST("no_skills_listed", summary.find("- **") == std::string::npos);
    }

    // --- Test 11: build_summary with enabled skill only ---
    {
        LOG_INFO("skill_registry", "build_summary_enabled_only");
        SkillRegistry registry;
        auto skill = std::make_shared<SkillDefinition>();
        skill->name = "enabled_skill";
        skill->description = "An enabled skill";
        skill->when_to_use = "When needed";
        skill->source_path = "/path/to/skill";
        registry.register_skill(skill);

        auto summary = registry.build_skill_summary();
        UNIT_TEST("has_header", summary.find("Available skills:") != std::string::npos);
        UNIT_TEST("skill_name_in_summary", summary.find("**enabled_skill**") != std::string::npos);
        UNIT_TEST("description_in_summary", summary.find("An enabled skill") != std::string::npos);
    }

    // --- Test 12: build_summary excludes disabled skills ---
    {
        LOG_INFO("skill_registry", "build_summary_excludes_disabled");
        SkillRegistry registry;
        auto enabled_skill = std::make_shared<SkillDefinition>();
        enabled_skill->name = "enabled";
        enabled_skill->description = "Enabled skill";
        enabled_skill->enabled = true;

        auto disabled_skill = std::make_shared<SkillDefinition>();
        disabled_skill->name = "disabled";
        disabled_skill->description = "Disabled skill";
        disabled_skill->enabled = false;

        registry.register_skill(enabled_skill);
        registry.register_skill(disabled_skill);

        auto summary = registry.build_skill_summary();
        UNIT_TEST("enabled_in_summary", summary.find("**enabled**") != std::string::npos);
        UNIT_TEST("disabled_not_in_summary", summary.find("**disabled**") == std::string::npos);
    }

    // --- Test 13: reload_skills with no changes (no directory) ---
    {
        LOG_INFO("skill_registry", "reload_no_changes");
        SkillRegistry registry;
        auto skill = std::make_shared<SkillDefinition>();
        skill->name = "existing_skill";
        skill->description = "Existing";
        skill->source_type = "native";
        skill->source_path = "/nonexistent/path";
        registry.register_skill(skill);

        // Scan a non-existent directory - no changes expected
        auto result = registry.reload_skills({"/nonexistent/dir"});
        UNIT_TEST("no_changes_in_result", result.find("0 updated") != std::string::npos || result.find("unchanged") != std::string::npos);
    }

    // --- Test 14: reload_skills with updated file ---
    {
        LOG_INFO("skill_registry", "reload_updated_file");
        SkillRegistry registry;
        std::string dir = "test_sr_reload_temp";
        safe_remove_all(dir);
        fs::create_directories(fs::path(dir) / "my_skill");

        // Create initial SKILL.md inside a subdirectory (reload_skills expects parent/skill_dir/SKILL.md)
        {
            std::ofstream f(fs::path(dir) / "my_skill" / "SKILL.md");
            f << "---\nname: reload_test\ndescription: Original description\n---\n";
            f << "## Instructions\nDo something original.\n";
        }

        // Parse and register the skill with proper source tracking
        auto skill = SkillLoader::parse_skill_md((fs::path(dir) / "my_skill" / "SKILL.md").string());
        if (skill && !skill->name.empty()) {
            skill->source_type = "native";
            skill->source_path = (fs::path(dir) / "my_skill").string();
            registry.register_skill(skill);
        }

        // Wait a moment and modify the file
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        {
            std::ofstream f(fs::path(dir) / "my_skill" / "SKILL.md");
            f << "---\nname: reload_test\ndescription: Updated description\n---\n";
            f << "## Instructions\nDo something updated.\n";
        }

        // Reload - should detect the change (reload_skills scans parent dir for subdirs)
        auto result = registry.reload_skills({dir});
        UNIT_TEST("updated_in_result", result.find("1 updated") != std::string::npos);

        safe_remove_all(dir);
    }

    // --- Test 15: reload_skills with removed skill directory ---
    {
        LOG_INFO("skill_registry", "reload_removed_skill");
        SkillRegistry registry;
        std::string dir = "test_sr_rm_temp";
        safe_remove_all(dir);
        fs::create_directories(fs::path(dir) / "my_skill");

        // Create initial SKILL.md inside a subdirectory
        {
            std::ofstream f(fs::path(dir) / "my_skill" / "SKILL.md");
            f << "---\nname: to_be_removed\ndescription: Will be removed\n---\n";
            f << "## Instructions\nDo something.\n";
        }

        // Parse and register the skill with proper source tracking
        auto skill = SkillLoader::parse_skill_md((fs::path(dir) / "my_skill" / "SKILL.md").string());
        if (skill && !skill->name.empty()) {
            skill->source_type = "native";
            skill->source_path = (fs::path(dir) / "my_skill").string();
            registry.register_skill(skill);
        }

        UNIT_TEST("skill_exists_before", registry.find_skill("to_be_removed") != nullptr);

        // Remove the entire skill subdirectory so reload_skills can't find it on disk
        safe_remove_all((fs::path(dir) / "my_skill").string());

        // Reload - should detect removal
        auto result = registry.reload_skills({dir});
        UNIT_TEST("removed_in_result", result.find("1 removed") != std::string::npos);

        safe_remove_all(dir);
    }

    parent.report.push_back(unit);
}

// ============================================================
// SkillLoader tests (static methods)
// ============================================================

static void test_skill_loader_class(UnitReport& parent)
{
    UnitReport unit("skill_loader_class");
    LOG_INFO("skill_loader", "entry");

    // --- Test 1: extract_section found ---
    {
        LOG_INFO("skill_loader", "extract_section_found");
        std::string content = "## Description\nThis is the description.\n";
        content += "## Instructions\nDo something.\n";

        auto section = SkillLoader::extract_section(content, "## Description");
        UNIT_TEST("section_not_empty", !section.empty());
        UNIT_TEST("description_content", section.find("This is the description.") != std::string::npos);
    }

    // --- Test 2: extract_section not found ---
    {
        LOG_INFO("skill_loader", "extract_section_not_found");
        std::string content = "## Description\nSome text.\n";

        auto section = SkillLoader::extract_section(content, "## Not Found");
        UNIT_TEST("empty_when_not_found", section.empty());
    }

    // --- Test 3: parse_tool_list basic ---
    {
        LOG_INFO("skill_loader", "parse_tool_list_basic");
        std::string content = "- tool1\n- tool2\n- tool3\n";

        auto tools = SkillLoader::parse_tool_list(content);
        UNIT_TEST("three_tools_found", tools.size() == 3);
        UNIT_TEST("first_tool", tools[0] == "tool1");
        UNIT_TEST("second_tool", tools[1] == "tool2");
        UNIT_TEST("third_tool", tools[2] == "tool3");
    }

    // --- Test 4: parse_tool_list with indentation ---
    {
        LOG_INFO("skill_loader", "parse_tool_list_with_indentation");
        std::string content = "  - tool1\n- tool2\n    - tool3\n";

        auto tools = SkillLoader::parse_tool_list(content);
        UNIT_TEST("three_tools_found", tools.size() == 3);
        UNIT_TEST("first_tool_trimmed", tools[0] == "tool1");
        UNIT_TEST("second_tool_trimmed", tools[1] == "tool2");
        UNIT_TEST("third_tool_trimmed", tools[2] == "tool3");
    }

    // --- Test 5: parse_tool_list empty ---
    {
        LOG_INFO("skill_loader", "parse_tool_list_empty");
        std::string content = "";

        auto tools = SkillLoader::parse_tool_list(content);
        UNIT_TEST("empty_result", tools.empty());
    }

    // --- Test 6: parse_config basic ---
    {
        LOG_INFO("skill_loader", "parse_config_basic");
        std::string content = "key1: value1\nkey2: value2\n";

        auto config = SkillLoader::parse_config(content);
        UNIT_TEST("two_keys_found", config.size() == 2);
        UNIT_TEST("first_key_value", config["key1"] == "value1");
        UNIT_TEST("second_key_value", config["key2"] == "value2");
    }

    // --- Test 7: parse_config with spaces around colon ---
    {
        LOG_INFO("skill_loader", "parse_config_with_spaces");
        std::string content = "key1 : value1\nkey2 : value2\n";

        auto config = SkillLoader::parse_config(content);
        UNIT_TEST("two_keys_found", config.size() == 2);
        UNIT_TEST("first_key_trimmed", config["key1"] == "value1");
        UNIT_TEST("second_key_trimmed", config["key2"] == "value2");
    }

    // --- Test 8: parse_config empty ---
    {
        LOG_INFO("skill_loader", "parse_config_empty");
        std::string content = "";

        auto config = SkillLoader::parse_config(content);
        UNIT_TEST("empty_result", config.empty());
    }

    // --- Test 9: parse_skill_md with frontmatter ---
    {
        LOG_INFO("skill_loader", "parse_skill_md_with_frontmatter");
        std::string dir = "test_sl_fm_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        // Create SKILL.md with frontmatter
        {
            std::ofstream f(fs::path(dir) / "SKILL.md");
            f << "---\nname: fm_skill\ndescription: Frontmatter description\nwhen_to_use: When testing\ninstructions: Step by step.\n";
            f << "---\n";
            f << "## Tools Required\n- tool1\n";
        }

        auto skill = SkillLoader::parse_skill_md((fs::path(dir) / "SKILL.md").string());
        UNIT_TEST("skill_not_null", skill != nullptr);
        UNIT_TEST("name_from_frontmatter", skill && skill->name == "fm_skill");
        UNIT_TEST("description_from_frontmatter", skill && skill->description == "Frontmatter description");
        UNIT_TEST("when_to_use_from_frontmatter", skill && skill->when_to_use == "When testing");
        UNIT_TEST("instructions_from_frontmatter", skill && skill->instructions == "Step by step.");

        safe_remove_all(dir);
    }

    // --- Test 10: parse_skill_md without frontmatter (markdown sections only) ---
    {
        LOG_INFO("skill_loader", "parse_skill_md_without_frontmatter");
        std::string dir = "test_sl_nfm_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        // Create SKILL.md without frontmatter
        {
            std::ofstream f(fs::path(dir) / "SKILL.md");
            f << "# My Skill\n";
            f << "## Description\nMarkdown description.\n";
            f << "## When to Use\nWhen needed.\n";
            f << "## Instructions\nDo something.\n";
        }

        auto skill = SkillLoader::parse_skill_md((fs::path(dir) / "SKILL.md").string());
        UNIT_TEST("skill_not_null", skill != nullptr);
        UNIT_TEST("name_from_heading", skill && skill->name == "my_skill");
        UNIT_TEST("description_from_markdown", skill && skill->description == "Markdown description.");
        UNIT_TEST("when_to_use_from_markdown", skill && skill->when_to_use == "When needed.");
        UNIT_TEST("instructions_from_markdown", skill && skill->instructions == "Do something.");

        safe_remove_all(dir);
    }

    // --- Test 11: parse_skill_md with frontmatter name fallback to heading ---
    {
        LOG_INFO("skill_loader", "parse_skill_md_name_fallback");
        std::string dir = "test_sl_nf_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        // Create SKILL.md with frontmatter but no name field
        {
            std::ofstream f(fs::path(dir) / "SKILL.md");
            f << "---\ndescription: No name in frontmatter.\n";
            f << "---\n";
            f << "# Fallback Name\n";
        }

        auto skill = SkillLoader::parse_skill_md((fs::path(dir) / "SKILL.md").string());
        UNIT_TEST("skill_not_null", skill != nullptr);
        UNIT_TEST("name_fallback_to_heading", skill && skill->name == "fallback_name");

        safe_remove_all(dir);
    }

    // --- Test 12: parse_skill_md with non-existent file returns null ---
    {
        LOG_INFO("skill_loader", "parse_skill_md_nonexistent_file");
        std::string dir = "test_sl_nf_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        auto skill = SkillLoader::parse_skill_md((fs::path(dir) / "nonexistent.md").string());
        UNIT_TEST("null_when_file_not_found", skill == nullptr);
    }

    // --- Test 13: scan_directory with non-existent directory returns empty ---
    {
        LOG_INFO("skill_loader", "scan_directory_no_dir");
        auto skills = SkillLoader::scan_directory("/nonexistent/directory");
        UNIT_TEST("empty_result", skills.empty());
    }

    // --- Test 14: scan_directory with directory but no SKILL.md returns empty ---
    {
        LOG_INFO("skill_loader", "scan_directory_no_skill_md");
        std::string dir = "test_sl_scan_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        // Create a subdirectory without SKILL.md
        fs::create_directories(fs::path(dir) / "no_skill_dir");

        auto skills = SkillLoader::scan_directory(dir);
        UNIT_TEST("empty_result", skills.empty());

        safe_remove_all(dir);
    }

    // --- Test 15: validate_dependencies all met ---
    {
        LOG_INFO("skill_loader", "validate_dependencies_met");
        auto skill = std::make_shared<SkillDefinition>();
        skill->name = "test_skill";
        skill->tools_required = {"tool_a", "tool_b"};

        bool result = SkillLoader::validate_dependencies(skill, {"tool_a", "tool_b", "tool_c"});
        UNIT_TEST("all_deps_met", result);
        UNIT_TEST("skill_still_enabled", skill->enabled == true);
    }

    // --- Test 16: validate_dependencies not met (missing tool) ---
    {
        LOG_INFO("skill_loader", "validate_dependencies_not_met");
        auto skill = std::make_shared<SkillDefinition>();
        skill->name = "test_skill";
        skill->tools_required = {"tool_a", "tool_missing"};

        bool result = SkillLoader::validate_dependencies(skill, {"tool_a", "tool_b"});
        UNIT_TEST("deps_not_met", !result);
        UNIT_TEST("skill_disabled", skill->enabled == false);
    }

    // --- Test 17: validate_dependencies with null skill returns true ---
    {
        LOG_INFO("skill_loader", "validate_dependencies_null_skill");
        bool result = SkillLoader::validate_dependencies(nullptr, {"tool_a"});
        UNIT_TEST("null_returns_true", result);
    }

    // --- Test 18: validate_dependencies with no tools_required returns true ---
    {
        LOG_INFO("skill_loader", "validate_dependencies_no_requirements");
        auto skill = std::make_shared<SkillDefinition>();
        skill->name = "test_skill";
        skill->tools_required.clear();

        bool result = SkillLoader::validate_dependencies(skill, {"tool_a"});
        UNIT_TEST("no_deps_returns_true", result);
    }

    // --- Test 19: extract_section with trailing whitespace trimmed ---
    {
        LOG_INFO("skill_loader", "extract_section_trailing_trimmed");
        std::string content = "## Description\nSome text.\n\n";

        auto section = SkillLoader::extract_section(content, "## Description");
        UNIT_TEST("trailing_newline_removed", !section.empty() && section.back() != '\n');
    }

    // --- Test 20: parse_tool_list with empty lines and spaces ---
    {
        LOG_INFO("skill_loader", "parse_tool_list_empty_lines");
        std::string content = "- tool1\n\n- tool2\n   \n- tool3\n";

        auto tools = SkillLoader::parse_tool_list(content);
        UNIT_TEST("three_tools_found", tools.size() == 3);
    }

    parent.report.push_back(unit);
}

// ============================================================
// Entry point for skill system tests
// ============================================================

void test_skill_system(UnitReport& parent)
{
    auto& sg = SafetyGuard::get_instance();
    sg.set_path_whitelist({fs::current_path().string()});

    UnitReport unit("skill_system");
    LOG_INFO("skill_system", "entry");

    test_skill_registry_class(unit);
    test_skill_loader_class(unit);

    parent.report.push_back(unit);
}
