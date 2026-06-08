#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>

namespace agent {

/**
 * A parsed skill definition loaded from a SKILL.md file.
 */
struct SkillDefinition {
    std::string name;              // e.g., "code_review"
    std::string description;       // short summary for LLM selection
    std::string when_to_use;       // trigger conditions
    std::string instructions;      // step-by-step workflow template
    std::vector<std::string> tools_required; // tool names this skill depends on
    std::map<std::string, std::string> config; // key-value configuration
    bool enabled = true;           // false if dependency check failed

    // Source tracking.
    std::string source_path;       // directory containing SKILL.md
    std::string source_type;       // "native" | "imported"
};

using SkillPtr = std::shared_ptr<SkillDefinition>;

/**
 * Registry that holds all available skills.
 */
class SkillRegistry {
public:
    void register_skill(SkillPtr skill);
    void unregister_skill(const std::string& name);
    std::vector<SkillPtr> get_skills() const;
    SkillPtr find_skill(const std::string& name) const;

    // Build a summary string of all skills for LLM selection context.
    std::string build_skill_summary() const;

private:
    std::map<std::string, SkillPtr> skills_;
};

// Global skill registry accessor (set by main.cpp, used by skill tools).
SkillRegistry* get_global_skill_registry();
void set_global_skill_registry(SkillRegistry* reg);

/**
 * Loads skills from directories by parsing SKILL.md files.
 */
class SkillLoader {
public:
    // Parse a single SKILL.md file into a SkillDefinition.
    static SkillPtr parse_skill_md(const std::string& md_path);

    // Scan a directory for subdirectories containing SKILL.md and load them all.
    static std::vector<SkillPtr> scan_directory(
        const std::string& dir,
        const std::string& source_type = "native");

    // Validate that all tools_required are available in the given ToolRegistry.
    // Returns true if all dependencies met; sets skill->enabled = false otherwise.
    static bool validate_dependencies(SkillPtr skill, const std::vector<std::string>& available_tools);

    // Auto-detect and import skills from cross-agent directories (.claude/skills/, .cursor/skills/, etc.).n    // Returns imported skills (duplicates are skipped).
    static std::vector<SkillPtr> auto_detect_and_import(
        const std::string& project_dir,
        const std::map<std::string, SkillPtr>& existing_skills);

private:
    SkillLoader() = default;

    // Extract a section from SKILL.md content (e.g., "## Description" → text).
    static std::string extract_section(const std::string& content, const std::string& heading);

    // Parse the tools_required list from a section.
    static std::vector<std::string> parse_tool_list(const std::string& section_text);

    // Parse key-value config lines (e.g., "max_files: 5").
    static std::map<std::string, std::string> parse_config(const std::string& section_text);
};

} // namespace agent
