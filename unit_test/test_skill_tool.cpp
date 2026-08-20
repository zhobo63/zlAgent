#include "pch.h"
#include "unit_test.h"

#include "tools.h"
#include "skill_system.h"
#include "safety_guard.h"

using namespace agent;
namespace fs = std::filesystem;
using json = nlohmann::json;

static void safe_remove_all(const std::string& path)
{
    try {
        if (fs::exists(path)) fs::remove_all(path);
    } catch (...) {}
}

// ── CreateSkillTool ───────────────────────────────────────

void test_create_skill_tool(UnitReport& parent)
{
    UnitReport unit("create_skill");
    LOG_INFO("test_create_skill", "create_skill");

    // basic skill creation — success
    {
        LOG_INFO("create_skill", "basic_success");
        std::string dir = "test_cs_basic_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        SkillRegistry registry;
        set_global_skill_registry(&registry);

        auto tool = create_create_skill_tool();
        UNIT_TEST("name_is_create_skill", tool->name() == "create_skill");

        json args;
        args["name"] = "test_skill_basic";
        args["description"] = "A basic test skill.";
        args["when_to_use"] = "When testing.";
        args["instructions"] = "Step 1: Do something\nStep 2: Do another thing";

        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        std::cout << TOUT::ANSI_BRIGHT_BLACK << result << TOUT::ANSI_RESET;

        UNIT_TEST("basic_success", result.find("created successfully") != std::string::npos);
        UNIT_TEST("skill_exists_in_registry", registry.find_skill("test_skill_basic") != nullptr);

        safe_remove_all(dir + "/.zlagent");
        set_global_skill_registry(nullptr);
    }

    // skill creation with tools_required array
    {
        LOG_INFO("create_skill", "with_tools_required");
        std::string dir = "test_cs_tools_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        SkillRegistry registry;
        set_global_skill_registry(&registry);

        auto tool = create_create_skill_tool();

        json args;
        args["name"] = "test_skill_with_tools";
        args["description"] = "A skill with tools.";
        args["when_to_use"] = "When testing.";
        args["instructions"] = "Step 1: Do something";
        args["tools_required"] = json::array({"read_file", "write_file"});

        auto args_str = args.dump();
        tool->show_arguments(args_str);
        std::string result = tool->execute(args_str);

        UNIT_TEST("has_tools_section", result.find("created successfully") != std::string::npos);
        // Verify the SKILL.md contains Tools Required section
        auto skill = registry.find_skill("test_skill_with_tools");
        if (skill) {
            UNIT_TEST("tools_required_parsed", !skill->tools_required.empty());
        }

        safe_remove_all(dir + "/.zlagent");
        set_global_skill_registry(nullptr);
    }

    // nested directory creation — skill name with slashes
    {
        LOG_INFO("create_skill", "nested_dir_creation");
        std::string dir = "test_cs_nested_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        SkillRegistry registry;
        set_global_skill_registry(&registry);

        // Change into the temp directory so relative DEFAULT_SKILL_DIR resolves correctly
        auto old_cwd = fs::current_path();
        std::string abs_dir = fs::canonical(dir).string();
        fs::current_path(dir);

        auto tool = create_create_skill_tool();

        json args;
        args["name"] = "test/nested/skill";
        args["description"] = "Nested skill.";
        args["when_to_use"] = "When testing.";
        args["instructions"] = "Step 1: Do something";

        auto args_str = args.dump();
        tool->show_arguments(args_str);
        std::string result = tool->execute(args_str);

        // Use absolute path for assertion since we're inside the temp dir
        UNIT_TEST("nested_dir_created", fs::exists(abs_dir + "/.zlagent/skills/test/nested/skill/SKILL.md"));

        fs::current_path(old_cwd);
        safe_remove_all(dir + "/.zlagent");
        set_global_skill_registry(nullptr);
    }

    // duplicate skill — error
    {
        LOG_INFO("create_skill", "duplicate_error");
        std::string dir = "test_cs_dup_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        SkillRegistry registry;
        set_global_skill_registry(&registry);

        auto tool = create_create_skill_tool();

        json args1;
        args1["name"] = "test_dup";
        args1["description"] = "First.";
        args1["when_to_use"] = "Test.";
        args1["instructions"] = "Do something.";
        std::string result1 = tool->execute(args1.dump());

        json args2;
        args2["name"] = "test_dup";
        args2["description"] = "Second.";
        args2["when_to_use"] = "Test.";
        args2["instructions"] = "Do something else.";
        std::string result2 = tool->execute(args2.dump());

        UNIT_TEST("duplicate_returns_error", result2.find("already exists") != std::string::npos);

        safe_remove_all(dir + "/.zlagent");
        set_global_skill_registry(nullptr);
    }

    // empty name returns error
    {
        LOG_INFO("create_skill", "empty_name_returns_error");
        auto tool = create_create_skill_tool();
        json args;
        args["name"] = "";
        args["description"] = "desc";
        args["when_to_use"] = "use";
        args["instructions"] = "inst";
        std::string result = tool->execute(args.dump());
        UNIT_TEST("empty_name_returns_error", result.find("Error") != std::string::npos);
    }

    // empty description returns error
    {
        LOG_INFO("create_skill", "empty_description_returns_error");
        auto tool = create_create_skill_tool();
        json args;
        args["name"] = "test";
        args["description"] = "";
        args["when_to_use"] = "use";
        args["instructions"] = "inst";
        std::string result = tool->execute(args.dump());
        UNIT_TEST("empty_description_returns_error", result.find("Error") != std::string::npos);
    }

    // empty when_to_use returns error
    {
        LOG_INFO("create_skill", "empty_when_to_use_returns_error");
        auto tool = create_create_skill_tool();
        json args;
        args["name"] = "test";
        args["description"] = "desc";
        args["when_to_use"] = "";
        args["instructions"] = "inst";
        std::string result = tool->execute(args.dump());
        UNIT_TEST("empty_when_to_use_returns_error", result.find("Error") != std::string::npos);
    }

    // empty instructions returns error
    {
        LOG_INFO("create_skill", "empty_instructions_returns_error");
        auto tool = create_create_skill_tool();
        json args;
        args["name"] = "test";
        args["description"] = "desc";
        args["when_to_use"] = "use";
        args["instructions"] = "";
        std::string result = tool->execute(args.dump());
        UNIT_TEST("empty_instructions_returns_error", result.find("Error") != std::string::npos);
    }

    // invalid JSON returns error
    {
        LOG_INFO("create_skill", "invalid_json_returns_error");
        auto tool = create_create_skill_tool();
        std::string result = tool->execute("not json");
        UNIT_TEST("invalid_json_returns_error", result.find("Error") != std::string::npos);
    }

    // empty input returns error
    {
        LOG_INFO("create_skill", "empty_input_returns_error");
        auto tool = create_create_skill_tool();
        std::string result = tool->execute("");
        UNIT_TEST("empty_input_returns_error", result.find("Error") != std::string::npos);
    }

    parent.report.push_back(unit);
}

// ── DeleteSkillTool ───────────────────────────────────────

void test_delete_skill_tool(UnitReport& parent)
{
    UnitReport unit("delete_skill");
    LOG_INFO("test_delete_skill", "delete_skill");

    // basic skill deletion — success
    {
        LOG_INFO("delete_skill", "basic_success");
        std::string dir = "test_ds_basic_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        SkillRegistry registry;
        set_global_skill_registry(&registry);

        // First create a skill to delete
        {
            auto tool = create_create_skill_tool();
            json args;
            args["name"] = "test_to_delete";
            args["description"] = "To be deleted.";
            args["when_to_use"] = "When testing.";
            args["instructions"] = "Step 1: Do something";
            tool->execute(args.dump());
        }

        UNIT_TEST("skill_exists_before_delete", registry.find_skill("test_to_delete") != nullptr);

        // Now delete it
        auto tool = create_delete_skill_tool();
        json args;
        args["name"] = "test_to_delete";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        std::string result = tool->execute(args_str);
        std::cout << TOUT::ANSI_BRIGHT_BLACK << result << TOUT::ANSI_RESET;

        UNIT_TEST("basic_success", result.find("deleted successfully") != std::string::npos);
        UNIT_TEST("skill_not_in_registry_after_delete", registry.find_skill("test_to_delete") == nullptr);

        safe_remove_all(dir + "/.zlagent");
        set_global_skill_registry(nullptr);
    }

    // delete non-existent skill — error
    {
        LOG_INFO("delete_skill", "non_existent_error");
        auto tool = create_delete_skill_tool();
        json args;
        args["name"] = "nonexistent_skill";
        std::string result = tool->execute(args.dump());
        UNIT_TEST("non_existent_returns_error", result.find("Error") != std::string::npos);
    }

    // empty name returns error
    {
        LOG_INFO("delete_skill", "empty_name_returns_error");
        auto tool = create_delete_skill_tool();
        json args;
        args["name"] = "";
        std::string result = tool->execute(args.dump());
        UNIT_TEST("empty_name_returns_error", result.find("Error") != std::string::npos);
    }

    // invalid JSON returns error
    {
        LOG_INFO("delete_skill", "invalid_json_returns_error");
        auto tool = create_delete_skill_tool();
        std::string result = tool->execute("not json");
        UNIT_TEST("invalid_json_returns_error", result.find("Error") != std::string::npos);
    }

    // empty input returns error
    {
        LOG_INFO("delete_skill", "empty_input_returns_error");
        auto tool = create_delete_skill_tool();
        std::string result = tool->execute("");
        UNIT_TEST("empty_input_returns_error", result.find("Error") != std::string::npos);
    }

    // missing global registry returns error
    {
        LOG_INFO("delete_skill", "missing_registry_error");
        auto tool = create_delete_skill_tool();
        json args;
        args["name"] = "test";
        std::string result = tool->execute(args.dump());
        UNIT_TEST("missing_registry_returns_error", result.find("Error") != std::string::npos);
    }

    // delete skill with tools_required — verify SKILL.md content before deletion
    {
        LOG_INFO("delete_skill", "with_tools_before_delete");
        std::string dir = "test_ds_tools_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        SkillRegistry registry;
        set_global_skill_registry(&registry);

        // Change into the temp directory so relative DEFAULT_SKILL_DIR resolves correctly
        auto old_cwd = fs::current_path();
        std::string abs_dir = fs::canonical(dir).string();
        fs::current_path(dir);

        // Create skill with tools_required
        {
            auto tool = create_create_skill_tool();
            json args;
            args["name"] = "test_with_tools_del";
            args["description"] = "With tools.";
            args["when_to_use"] = "When testing.";
            args["instructions"] = "Step 1: Do something";
            args["tools_required"] = json::array({"read_file", "write_file"});
            tool->execute(args.dump());
        }

        // Verify SKILL.md exists before deletion (use absolute path)
        std::string md_path = abs_dir + "/.zlagent/skills/test_with_tools_del/SKILL.md";
        UNIT_TEST("skill_md_exists_before_delete", fs::exists(md_path));

        // Delete the skill
        auto tool = create_delete_skill_tool();
        json args;
        args["name"] = "test_with_tools_del";
        std::string result = tool->execute(args.dump());

        UNIT_TEST("deleted_successfully", result.find("deleted successfully") != std::string::npos);
        // Verify SKILL.md is removed after deletion
        UNIT_TEST("skill_md_removed_after_delete", !fs::exists(md_path));

        fs::current_path(old_cwd);
        safe_remove_all(dir + "/.zlagent");
        set_global_skill_registry(nullptr);
    }

    parent.report.push_back(unit);
}

// ── GetSkillTool ───────────────────────────────────────

void test_get_skill_tool(UnitReport& parent)
{
    UnitReport unit("get_skill");
    LOG_INFO("test_get_skill", "get_skill");

    // basic skill retrieval — success
    {
        LOG_INFO("get_skill", "basic_success");
        std::string dir = "test_gs_basic_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        SkillRegistry registry;
        set_global_skill_registry(&registry);

        // Create a temporary SKILL.md on disk and register it.
        std::string md_path = dir + "/SKILL.md";
        {
            std::ofstream out(md_path);
            out << "# Test Skill\n\n";
            out << "## Description\nA test skill.\n\n";
            out << "## When to Use\nWhen testing.\n\n";
            out << "## Instructions\n1. Do step one\n2. Do step two\n\n";
            out << "## Tools Required\n- read_file\n\n";
        }

        auto skill = std::make_shared<SkillDefinition>();
        skill->name = "test_skill_get";
        skill->description = "A test skill.";
        skill->when_to_use = "When testing.";
        skill->instructions = "1. Do step one\n2. Do step two";
        skill->source_path = dir;
        registry.register_skill(skill);

        auto tool = create_get_skill_tool();
        UNIT_TEST("name_is_get_skill", tool->name() == "get_skill");

        json args;
        args["name"] = "test_skill_get";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        std::string result = tool->execute(args_str);
        std::cout << TOUT::ANSI_BRIGHT_BLACK << result << TOUT::ANSI_RESET;

        UNIT_TEST("basic_success", result.find("# Test Skill") != std::string::npos);
        UNIT_TEST("has_description", result.find("## Description") != std::string::npos);
        UNIT_TEST("has_instructions", result.find("## Instructions") != std::string::npos);

        safe_remove_all(dir);
        set_global_skill_registry(nullptr);
    }

    // skill not found — lists available skills
    {
        LOG_INFO("get_skill", "not_found_lists_available");
        std::string dir = "test_gs_notfound_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        SkillRegistry registry;
        set_global_skill_registry(&registry);

        // Register an enabled skill to show in available list.
        auto skill = std::make_shared<SkillDefinition>();
        skill->name = "existing_skill";
        skill->description = "An existing skill.";
        skill->enabled = true;
        registry.register_skill(skill);

        auto tool = create_get_skill_tool();
        json args;
        args["name"] = "nonexistent";
        std::string result = tool->execute(args.dump());

        UNIT_TEST("not_found_error", result.find("Error") != std::string::npos);
        UNIT_TEST("has_available_list", result.find("Available skills:") != std::string::npos);
        UNIT_TEST("lists_existing_skill", result.find("existing_skill") != std::string::npos);

        safe_remove_all(dir);
        set_global_skill_registry(nullptr);
    }

    // missing global registry — error
    {
        LOG_INFO("get_skill", "missing_registry_error");
        auto tool = create_get_skill_tool();
        json args;
        args["name"] = "test";
        std::string result = tool->execute(args.dump());
        UNIT_TEST("missing_registry_returns_error", result.find("Error") != std::string::npos);
    }

    // empty name returns error
    {
        LOG_INFO("get_skill", "empty_name_returns_error");
        auto tool = create_get_skill_tool();
        json args;
        args["name"] = "";
        std::string result = tool->execute(args.dump());
        UNIT_TEST("empty_name_returns_error", result.find("Error") != std::string::npos);
    }

    // invalid JSON returns error
    {
        LOG_INFO("get_skill", "invalid_json_returns_error");
        auto tool = create_get_skill_tool();
        std::string result = tool->execute("not json");
        UNIT_TEST("invalid_json_returns_error", result.find("Error") != std::string::npos);
    }

    // empty input returns error
    {
        LOG_INFO("get_skill", "empty_input_returns_error");
        auto tool = create_get_skill_tool();
        std::string result = tool->execute("");
        UNIT_TEST("empty_input_returns_error", result.find("Error") != std::string::npos);
    }

    // SKILL.md not found on disk — error
    {
        LOG_INFO("get_skill", "skill_md_not_found");
        std::string dir = "test_gs_nofile_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        SkillRegistry registry;
        set_global_skill_registry(&registry);

        // Register a skill whose SKILL.md doesn't exist.
        auto skill = std::make_shared<SkillDefinition>();
        skill->name = "no_file";
        skill->source_path = "/nonexistent/path";
        registry.register_skill(skill);

        auto tool = create_get_skill_tool();
        json args;
        args["name"] = "no_file";
        std::string result = tool->execute(args.dump());

        UNIT_TEST("skill_md_not_found_error", result.find("Error") != std::string::npos);
        UNIT_TEST("has_skil_lmd_in_error", result.find("SKILL.md") != std::string::npos || result.find("not found") != std::string::npos);

        safe_remove_all(dir);
        set_global_skill_registry(nullptr);
    }

    // get disabled skill — should still return content if SKILL.md exists
    {
        LOG_INFO("get_skill", "disabled_skill_retrieval");
        std::string dir = "test_gs_disabled_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        SkillRegistry registry;
        set_global_skill_registry(&registry);

        // Create a temporary SKILL.md on disk.
        std::string md_path = dir + "/SKILL.md";
        {
            std::ofstream out(md_path);
            out << "# Disabled Skill\n\n";
            out << "## Description\nA disabled skill.\n\n";
            out << "## Instructions\n1. Do something\n";
        }

        auto skill = std::make_shared<SkillDefinition>();
        skill->name = "disabled_skill";
        skill->description = "A disabled skill.";
        skill->enabled = false;  // disabled
        skill->source_path = dir;
        registry.register_skill(skill);

        auto tool = create_get_skill_tool();
        json args;
        args["name"] = "disabled_skill";
        std::string result = tool->execute(args.dump());

        // Should still return content even if disabled.
        UNIT_TEST("disabled_skill_retrievable", result.find("# Disabled Skill") != std::string::npos);

        safe_remove_all(dir);
        set_global_skill_registry(nullptr);
    }

    parent.report.push_back(unit);
}

// ── ReloadSkillsTool ───────────────────────────────────────

void test_reload_skills_tool(UnitReport& parent)
{
    UnitReport unit("reload_skills");
    LOG_INFO("test_reload_skills", "reload_skills");

    // basic reload — success with empty input (uses default directory)
    {
        LOG_INFO("reload_skills", "basic_success_empty_input");
        std::string dir = "test_rs_basic_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        SkillRegistry registry;
        set_global_skill_registry(&registry);

        auto tool = create_reload_skills_tool();
        std::string result = tool->execute("");

        // Should not return error (empty input uses default directory).
        UNIT_TEST("no_error_on_empty_input", result.find("Error") == std::string::npos);

        safe_remove_all(dir);
        set_global_skill_registry(nullptr);
    }

    // reload with scan_dirs parameter
    {
        LOG_INFO("reload_skills", "with_scan_dirs");
        std::string dir = "test_rs_scandirs_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        SkillRegistry registry;
        set_global_skill_registry(&registry);

        auto tool = create_reload_skills_tool();
        json args;
        args["scan_dirs"] = json::array({dir});
        std::string result = tool->execute(args.dump());

        // Should not return error (empty scan dirs is valid).
        UNIT_TEST("no_error_on_scan_dirs", result.find("Error") == std::string::npos);

        safe_remove_all(dir);
        set_global_skill_registry(nullptr);
    }

    // missing global registry — error
    {
        LOG_INFO("reload_skills", "missing_registry_error");
        auto tool = create_reload_skills_tool();
        std::string result = tool->execute("");
        UNIT_TEST("missing_registry_returns_error", result.find("Error") != std::string::npos);
    }

    // invalid JSON returns error
    {
        LOG_INFO("reload_skills", "invalid_json_returns_error");
        auto tool = create_reload_skills_tool();
        std::string result = tool->execute("not json");
        UNIT_TEST("invalid_json_returns_error", result.find("Error") != std::string::npos);
    }

    // empty name returns error (for scan_dirs with invalid type)
    {
        LOG_INFO("reload_skills", "empty_scan_dirs_array");

        SkillRegistry registry;
        set_global_skill_registry(&registry);

        auto tool = create_reload_skills_tool();
        json args;
        args["scan_dirs"] = json::array({});
        std::string result = tool->execute(args.dump());

        // Empty scan dirs should not return error.
        UNIT_TEST("empty_scan_dirs_no_error", result.find("Error") == std::string::npos);

        set_global_skill_registry(nullptr);
    }

    parent.report.push_back(unit);
}

// ── Entry point ────────────────────────────────────────────────

void test_skill_tool(UnitReport& parent)
{
    // Disable SafetyGuard for tests.
    auto& sg = SafetyGuard::get_instance();
    sg.reset_path_whitelist();
    sg.set_working_directory("");

    UnitReport unit("skill_tools");
    LOG_INFO("test_skill_tools", "skill_tools");

    test_create_skill_tool(unit);
    test_delete_skill_tool(unit);
    test_get_skill_tool(unit);
    test_reload_skills_tool(unit);

    parent.report.push_back(unit);
}
