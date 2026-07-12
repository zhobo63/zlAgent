#include "pch.h"

#include "tool.h"
#include "skill_system.h"
#include "agent.h"
#include "json.hpp"

namespace agent {
using json = nlohmann::json;
namespace fs = std::filesystem;

// Global skill registry pointer (set by main.cpp).
static SkillRegistry* g_skill_registry = nullptr;

void set_global_skill_registry(SkillRegistry* reg) {
    g_skill_registry = reg;
}

SkillRegistry* get_global_skill_registry() {
    return g_skill_registry;
}

// -----------------------------------------------------------------------
// CreateSkillTool - dynamically create a new skill at runtime
// -----------------------------------------------------------------------
class CreateSkillTool : public Tool {
public:
    std::string name() const override { return "create_skill"; }
    std::string description() const override {
        return "Create a new agent skill dynamically. The skill will be written to .zlagent/skills/<name>/SKILL.md and registered immediately.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["name"]["type"] = "string";
        schema["properties"]["name"]["description"] = "Skill name (lowercase English + underscores)";
        schema["properties"]["description"]["type"] = "string";
        schema["properties"]["description"]["description"] = "Short description of the skill";
        schema["properties"]["when_to_use"]["type"] = "string";
        schema["properties"]["when_to_use"]["description"] = "When this skill should be triggered";
        schema["properties"]["instructions"]["type"] = "string";
        schema["properties"]["instructions"]["description"] = "Step-by-step instructions for the skill workflow";
        schema["properties"]["tools_required"]["type"] = "array";
        schema["properties"]["tools_required"]["items"]["type"] = "string";
        schema["properties"]["tools_required"]["description"] = "List of tool names this skill depends on";
        schema["required"] = {"name", "description", "when_to_use", "instructions"};
        return schema.dump();
    }

    std::string execute(const std::string& json_args) override {
        try {
            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                return "Error: Invalid JSON arguments - not json";
            }
            std::string name = args.value("name", "");
            std::string description = args.value("description", "");
            std::string when_to_use = args.value("when_to_use", "");
            std::string instructions = args.value("instructions", "");

            if (name.empty()) return "Error: Skill name is required.";
            if (description.empty()) return "Error: Description is required.";
            if (when_to_use.empty()) return "Error: When to use is required.";
            if (instructions.empty()) return "Error: Instructions are required.";

            // Check for duplicate.
            if (g_skill_registry && g_skill_registry->find_skill(name)) {
                return "Error: Skill '" + name + "' already exists.";
            }

            // Parse optional tools_required.
            std::vector<std::string> tools;
            if (args.contains("tools_required") && args["tools_required"].is_array()) {
                for (const auto& t : args["tools_required"]) {
                    tools.push_back(t.get<std::string>());
                }
            }

            // Build SKILL.md content.
            std::ostringstream md;
            md << "# " << name << "\n\n";
            md << "## Description\n" << description << "\n\n";
            md << "## When to Use\n" << when_to_use << "\n\n";
            md << "## Instructions\n" << instructions << "\n\n";

            if (!tools.empty()) {
                md << "## Tools Required\n";
                for (const auto& t : tools) {
                    md << "- " << t << "\n";
                }
                md << "\n";
            }

            // Write to zlagent/skills/<name>/SKILL.md.
            std::string skill_dir = DEFAULT_SKILL_DIR "/" + name;
            fs::create_directories(skill_dir);

            std::string md_path = skill_dir + "/SKILL.md";
            std::ofstream out(md_path);
            if (!out.is_open()) {
                return "Error: Failed to write SKILL.md to '" + md_path + "'.";
            }
            out << md.str();
            out.close();

            // Parse and register.
            auto skill = SkillLoader::parse_skill_md(md_path);
            if (!skill) {
                return "Error: Failed to parse the created SKILL.md.";
            }
            skill->source_type = "dynamic";

            if (g_skill_registry) {
                g_skill_registry->register_skill(skill);
            }

            std::string result = "Skill '" + name + "' created successfully at " + md_path;
            result += "\nThe skill is now available for use.";
            return result;

        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }
};

// -----------------------------------------------------------------------
// DeleteSkillTool - remove a registered skill and its SKILL.md
// -----------------------------------------------------------------------
class DeleteSkillTool : public Tool {
public:
    std::string name() const override { return "delete_skill"; }
    std::string description() const override {
        return "Delete an existing agent skill. Removes the SKILL.md file and unregisters it from the registry.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["name"]["type"] = "string";
        schema["properties"]["name"]["description"] = "Name of the skill to delete";
        schema["required"] = {"name"};
        return schema.dump();
    }

    std::string execute(const std::string& json_args) override {
        try {
            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                return "Error: Invalid JSON arguments - not json";
            }
            std::string name = args.value("name", "");

            if (name.empty()) return "Error: Skill name is required.";

            if (!g_skill_registry || !g_skill_registry->find_skill(name)) {
                return "Error: Skill '" + name + "' not found.";
            }

            // Remove SKILL.md file.
            std::string skill_dir = DEFAULT_SKILL_DIR "/" + name;
            std::string md_path = skill_dir + "/SKILL.md";
            if (fs::exists(md_path)) {
                fs::remove(md_path);
            }

            // Unregister from registry.
            g_skill_registry->unregister_skill(name);

            return "Skill '" + name + "' deleted successfully.";

        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }
};

// -----------------------------------------------------------------------
// GetSkillTool - retrieve full content of a registered skill on demand
// -----------------------------------------------------------------------
class GetSkillTool : public Tool {
public:
    std::string name() const override { return "get_skill"; }
    std::string description() const override {
        return "Retrieve the full content of a registered agent skill, including its step-by-step instructions. Use this when you need to follow a specific skill's workflow. The system prompt lists available skills with brief descriptions; call this tool to get the detailed instructions before executing the task.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["name"]["type"] = "string";
        schema["properties"]["name"]["description"] = "Name of the skill to retrieve";
        schema["required"] = {"name"};
        return schema.dump();
    }

    std::string execute(const std::string& json_args) override {
        try {
            if (!g_skill_registry) {
                return "Error: Skill registry not initialized.";
            }

            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                return "Error: Invalid JSON arguments - not json";
            }
            std::string name = args.value("name", "");

            if (name.empty()) return "Error: Skill name is required.";

            auto skill = g_skill_registry->find_skill(name);
            if (!skill) {
                // List available skills to help the caller.
                std::ostringstream oss;
                oss << "Error: Skill '" << name << "' not found.\n";
                oss << "Available skills:\n";
                for (const auto& s : g_skill_registry->get_skills()) {
                    if (!s->enabled) continue;
                    oss << "  - " << s->name << ": " << s->description << "\n";
                }
                return oss.str();
            }

            // Read the raw SKILL.md file from disk.
            std::string md_path = skill->source_path + "/SKILL.md";
            if (!fs::exists(md_path)) {
                return "Error: SKILL.md not found at '" + md_path + "'.";
            }

            std::ifstream file(md_path);
            if (!file.is_open()) {
                return "Error: Failed to open '" + md_path + "'.";
            }

            std::ostringstream oss;
            oss << file.rdbuf();
            return oss.str();

        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }
};

// -----------------------------------------------------------------------
// ReloadSkillsTool - hot-reload native skills from disk
// -----------------------------------------------------------------------
class ReloadSkillsTool : public Tool {
public:
    std::string name() const override { return "reload_skills"; }
    std::string description() const override {
        return "Hot-reload native skills by scanning .zlagent/skills/ for changes. Detects file modifications via mtime, re-parses changed SKILL.md files in-place, and removes deleted ones.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["scan_dirs"]["type"] = "array";
        schema["properties"]["scan_dirs"]["items"]["type"] = "string";
        schema["properties"]["scan_dirs"]["description"] = "Optional list of directories to scan. Defaults to .zlagent/skills/.";
        return schema.dump();
    }

    std::string execute(const std::string& json_args) override {
        try {
            if (!g_skill_registry) {
                return "Error: Skill registry not initialized.";
            }

            // Use Agent::reload_skills() for the full reload flow:
            // hot-reload + re-validate dependencies + re-inject system prompt.
            auto* ag = get_global_agent();
            if (ag) {
                ag->reload_skills();
            } else {
                // Fallback: direct registry reload only.
                std::vector<std::string> dirs;
                if (!json_args.empty()) {
                    auto args = json::parse(json_args);
                    if (!args.is_discarded() && args.contains("scan_dirs") && args["scan_dirs"].is_array()) {
                        for (const auto& d : args["scan_dirs"]) {
                            dirs.push_back(d.get<std::string>());
                        }
                    }
                }
                g_skill_registry->reload_skills(dirs);
            }

            return "Skills reloaded successfully.";

        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        } catch (const std::exception& e) {
            return std::string("Error: ") + e.what();
        }
    }
};

ToolPtr create_get_skill_tool() {
    return std::make_shared<GetSkillTool>();
}

ToolPtr create_create_skill_tool() {
    return std::make_shared<CreateSkillTool>();
}

ToolPtr create_delete_skill_tool() {
    return std::make_shared<DeleteSkillTool>();
}

ToolPtr create_reload_skills_tool() {
    return std::make_shared<ReloadSkillsTool>();
}

} // namespace agent
