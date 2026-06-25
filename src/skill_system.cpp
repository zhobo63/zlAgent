#include "pch.h"

#include "skill_system.h"
#include "logger.h"

namespace agent {
namespace fs = std::filesystem;

// ============================================================================
// SkillRegistry
// ============================================================================

void SkillRegistry::register_skill(SkillPtr skill) {
    if (skill && !skill->name.empty()) {
        skills_[skill->name] = skill;
    }
}

void SkillRegistry::unregister_skill(const std::string& name) {
    skills_.erase(name);
}

std::vector<SkillPtr> SkillRegistry::get_skills() const {
    std::vector<SkillPtr> result;
    for (const auto& [name, skill] : skills_) {
        result.push_back(skill);
    }
    return result;
}

SkillPtr SkillRegistry::find_skill(const std::string& name) const {
    auto it = skills_.find(name);
    if (it != skills_.end()) return it->second;
    return nullptr;
}

std::string SkillRegistry::build_skill_summary() const {
    std::ostringstream oss;
    oss << "Available skills:\n";
    for (const auto& [name, skill] : skills_) {
        if (!skill->enabled) continue;
        oss << "- **" << name << "**: " << skill->description << "\n";
        oss << "  When to use: " << skill->when_to_use << "\n";
    }
    return oss.str();
}

// ============================================================================
// SkillLoader - parsing helpers
// ============================================================================

std::string SkillLoader::extract_section(const std::string& content, const std::string& heading) {
    // Find "## Heading" and extract text until the next "## " or end of file.
    size_t pos = 0;
    while ((pos = content.find(heading, pos)) != std::string::npos) {
        // Move past the heading line.
        size_t start = content.find('\n', pos);
        if (start == std::string::npos) return "";
        start++;

        // Find next "## " heading or end of file.
        size_t end = content.find("## ", start);
        if (end == std::string::npos) {
            end = content.size();
        }

        // Trim trailing whitespace.
        std::string text = content.substr(start, end - start);
        while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) {
            text.pop_back();
        }
        return text;
    }
    return "";
}

std::vector<std::string> SkillLoader::parse_tool_list(const std::string& section_text) {
    std::vector<std::string> tools;
    std::istringstream iss(section_text);
    std::string line;

    while (std::getline(iss, line)) {
        // Trim.
        auto ltrim = [](std::string& s) {
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](char c){ return !std::isspace(c); }));
        };
        auto rtrim = [](std::string& s) {
            s.erase(std::find_if(s.rbegin(), s.rend(), [](char c){ return !std::isspace(c); }).base(), s.end());
        };
        ltrim(line);
        rtrim(line);

        if (line.empty()) continue;

        // Match "- tool_name" or "  - tool_name".
        if (line[0] == '-') {
            std::string tool = line.substr(1);
            ltrim(tool);
            rtrim(tool);
            if (!tool.empty()) tools.push_back(tool);
        }
    }

    return tools;
}

std::map<std::string, std::string> SkillLoader::parse_config(const std::string& section_text) {
    std::map<std::string, std::string> config;
    std::istringstream iss(section_text);
    std::string line;

    while (std::getline(iss, line)) {
        auto ltrim = [](std::string& s) {
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](char c){ return !std::isspace(c); }));
        };
        auto rtrim = [](std::string& s) {
            s.erase(std::find_if(s.rbegin(), s.rend(), [](char c){ return !std::isspace(c); }).base(), s.end());
        };
        ltrim(line);
        rtrim(line);

        if (line.empty()) continue;

        // Match "key: value".
        auto colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);
            ltrim(key);
            rtrim(key);
            ltrim(value);
            rtrim(value);
            if (!key.empty()) config[key] = value;
        }
    }

    return config;
}

// ============================================================================
// SkillLoader - main parsing & scanning
// ============================================================================

SkillPtr SkillLoader::parse_skill_md(const std::string& md_path) {
    auto skill = std::make_shared<SkillDefinition>();

    // Read file.
    std::ifstream file(md_path);
    if (!file.is_open()) return nullptr;

    std::ostringstream oss;
    oss << file.rdbuf();
    std::string content = oss.str();

    // Extract the skill name from the first "# " heading.
    size_t hash_pos = content.find("# ");
    if (hash_pos != std::string::npos) {
        size_t end_line = content.find('\n', hash_pos);
        std::string title = content.substr(hash_pos + 2, end_line - hash_pos - 2);
        // Trim.
        while (!title.empty() && title.back() == ' ') title.pop_back();

        // Convert to lowercase with underscores for the skill name.
        std::transform(title.begin(), title.end(), title.begin(), [](char c) {
            if (std::isalpha(c)) return static_cast<char>(std::tolower(c));
            if (c == ' ') return '_';
            return c;
        });
        skill->name = title;
    }

    // Extract sections.
    skill->description   = extract_section(content, "## Description");
    skill->when_to_use   = extract_section(content, "## When to Use");
    skill->instructions  = extract_section(content, "## Instructions");
    skill->tools_required = parse_tool_list(extract_section(content, "## Tools Required"));
    skill->config        = parse_config(extract_section(content, "## Configuration"));

    // Source tracking.
    skill->source_path   = fs::path(md_path).parent_path().string();

    return skill;
}

std::vector<SkillPtr> SkillLoader::scan_directory(const std::string& dir, const std::string& source_type) {
    std::vector<SkillPtr> skills;

    if (!fs::exists(dir)) return skills;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_directory()) continue;

        std::string skill_md = entry.path().string() + "/SKILL.md";
        if (!fs::exists(skill_md)) continue;

        auto skill = parse_skill_md(skill_md);
        if (skill && !skill->name.empty()) {
            skill->source_type = source_type;
            skills.push_back(skill);
        }
    }

    return skills;
}

bool SkillLoader::validate_dependencies(SkillPtr skill, const std::vector<std::string>& available_tools) {
    if (!skill || skill->tools_required.empty()) return true;

    for (const auto& tool_name : skill->tools_required) {
        bool found = false;
        for (const auto& avail : available_tools) {
            if (avail == tool_name) { found = true; break; }
        }
        if (!found) {
            LOG_WARN("Skill", "  " + skill->name + ": tool '" + tool_name + "' not found, disabled.");
            skill->enabled = false;
            return false;
        }
    }
    return true;
}

std::vector<SkillPtr> SkillLoader::auto_detect_and_import(
    const std::string& project_dir,
    const std::map<std::string, SkillPtr>& existing_skills) {

    // Cross-agent compatible directories to scan.
    struct DirEntry {
        std::string path;
        std::string label;  // for logging.
    };

    static const DirEntry project_dirs[] = {
        {project_dir + "/.claude/skills",     "[imported from .claude/skills/]"},
        {project_dir + "/.cursor/skills",      "[imported from .cursor/skills/]"},
        {project_dir + "/.gemini/skills",      "[imported from .gemini/skills/]"},
        {project_dir + "/.github/skills",      "[imported from .github/skills/]"},
        {project_dir + "/.windsurf/skills",    "[imported from .windsurf/skills/]"},
        {project_dir + "/.agents/skills",      "[imported from .agents/skills/]"},
    };

    // Also scan global directories.
    static const DirEntry global_dirs[] = {
        {std::getenv("HOME") ? std::string(std::getenv("HOME")) + "/.claude/skills" : "", "[imported from ~/.claude/skills/]"},
        {std::getenv("HOME") ? std::string(std::getenv("HOME")) + "/.cursor/skills" : "", "[imported from ~/.cursor/skills/]"},
        {std::getenv("HOME") ? std::string(std::getenv("HOME")) + "/.agents/skills" : "", "[imported from ~/.agents/skills/]"},
    };

    auto scan_and_import = [&](const DirEntry* dirs, size_t count) {
        std::vector<SkillPtr> imported;
        for (size_t i = 0; i < count; ++i) {
            const auto& dir = dirs[i];
            if (dir.path.empty()) continue;

            auto skills = scan_directory(dir.path, "imported");
            for (auto& skill : skills) {
                // Skip duplicates - native takes priority.
                if (existing_skills.count(skill->name)) {
                    LOG_INFO("Skill", "  " + skill->name + dir.label + " - skipped (duplicate)");
                    continue;
                }
                imported.push_back(skill);
            }
        }
        return imported;
    };

    auto result = scan_and_import(project_dirs, sizeof(project_dirs) / sizeof(DirEntry));
    auto global_result = scan_and_import(global_dirs, sizeof(global_dirs) / sizeof(DirEntry));
    result.insert(result.end(), global_result.begin(), global_result.end());

    return result;
}

} // namespace agent
