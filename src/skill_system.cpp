#include "pch.h"

#include "skill_system.h"
#include "logger.h"

namespace agent {
namespace fs = std::filesystem;

// ============================================================================
// SkillRegistry
// ============================================================================

// Normalize a skill name by replacing hyphens with underscores for fuzzy matching.
static std::string normalize_skill_name(const std::string& name) {
    std::string result = name;
    std::replace(result.begin(), result.end(), '-', '_');
    return result;
}

void SkillRegistry::register_skill(SkillPtr skill) {
    if (skill && !skill->name.empty()) {
        skill->name = normalize_skill_name(skill->name);
        skills_[skill->name] = skill;
    }
}

void SkillRegistry::unregister_skill(const std::string& _name) {
    auto name = normalize_skill_name(_name);
    skills_.erase(name);
}

std::vector<SkillPtr> SkillRegistry::get_skills() const {
    std::vector<SkillPtr> result;
    for (const auto& [name, skill] : skills_) {
        if (!skill->enabled) continue;
        result.push_back(skill);
    }
    return result;
}

SkillPtr SkillRegistry::find_skill(const std::string& _name) const {
    auto name = normalize_skill_name(_name);
    // Exact match first.
    auto it = skills_.find(name);
    if (it != skills_.end()) return it->second;

    // Fuzzy match: treat hyphens and underscores as equivalent.
    for (const auto& [key, skill] : skills_) {
        if (key == name) return skill;
    }
    return nullptr;
}

std::string SkillRegistry::build_skill_summary() const {
    std::ostringstream oss;
    oss << "Available skills:\n";
    for (const auto& [name, skill] : skills_) {
        if (!skill->enabled) continue;
        oss << "- **" << name << "**: " << skill->description << "\n";
        if (!skill->when_to_use.empty()) {
            oss << "  When to use: " << skill->when_to_use << "\n";
        }
        if (!skill->source_path.empty()) {
            oss << "  Source: " << skill->source_path << "\n";
        }
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

// Trim whitespace from both ends of a string.
static std::string trim(const std::string& s) {
    auto ltrim = [](std::string& str) {
        str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](char c){ return !std::isspace(c); }));
    };
    auto rtrim = [](std::string& str) {
        str.erase(std::find_if(str.rbegin(), str.rend(), [](char c){ return !std::isspace(c); }).base(), str.end());
    };
    std::string result = s;
    ltrim(result);
    rtrim(result);
    return result;
}

// Extract YAML frontmatter from the beginning of a SKILL.md file.
// Returns true if frontmatter was found; populates the given map with key-value pairs.
static bool extract_frontmatter(const std::string& content, std::map<std::string, std::string>& kv) {
    // Must start with "---" on the first line.
    size_t pos = 0;
    while (pos < content.size() && std::isspace(content[pos])) ++pos;
    if (content.compare(pos, 3, "---") != 0) return false;

    // Find closing "---".
    size_t end = content.find("\n---", pos + 3);
    if (end == std::string::npos) return false;

    // Parse key: value lines between the delimiters.
    std::string block = content.substr(pos + 3, end - pos - 3);
    std::istringstream iss(block);
    std::string line;
    while (std::getline(iss, line)) {
        auto colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = trim(line.substr(0, colon_pos));
            std::string value = trim(line.substr(colon_pos + 1));
            if (!key.empty()) kv[key] = value;
        }
    }
    return true;
}

SkillPtr SkillLoader::parse_skill_md(const std::string& md_path) {
    auto skill = std::make_shared<SkillDefinition>();

    // Read file.
    std::ifstream file(md_path);
    if (!file.is_open()) return nullptr;

    std::ostringstream oss;
    oss << file.rdbuf();
    std::string content = oss.str();

    // --- YAML frontmatter (optional, takes priority) ---
    std::map<std::string, std::string> frontmatter;
    bool has_frontmatter = extract_frontmatter(content, frontmatter);

    if (has_frontmatter && !frontmatter.empty()) {
        // Use frontmatter values with fallback to markdown sections.
        skill->name          = frontmatter["name"];
        skill->description   = frontmatter["description"];
        skill->when_to_use   = frontmatter["when_to_use"];
        skill->instructions  = frontmatter["instructions"];

        // If name is not in frontmatter, fall back to the first "# " heading.
        if (skill->name.empty()) {
            size_t hash_pos = content.find("# ");
            if (hash_pos != std::string::npos) {
                size_t end_line = content.find('\n', hash_pos);
                std::string title = content.substr(hash_pos + 2, end_line - hash_pos - 2);
                while (!title.empty() && title.back() == ' ') title.pop_back();
                std::transform(title.begin(), title.end(), title.begin(), [](char c) {
                    if (std::isalpha(c)) return static_cast<char>(std::tolower(c));
                    if (c == ' ') return '_';
                    return c;
                });
                skill->name = title;
            }
        }
    } else {
        // --- Markdown sections only ---

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
    }

    // Tools required and config always come from markdown sections (not frontmatter).
    skill->tools_required = parse_tool_list(extract_section(content, "## Tools Required"));
    skill->config         = parse_config(extract_section(content, "## Configuration"));

    // Source tracking.
    skill->source_path   = fs::path(md_path).parent_path().string();

    return skill;
}

std::vector<SkillPtr> SkillLoader::scan_directory(const std::string& dir, const std::string& source_type) {
    std::vector<SkillPtr> skills;

    if (!fs::exists(dir)) return skills;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_directory()) continue;
        LOG_DEBUG("SkillLoader", "scan_directory:" + entry.path().string());

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

std::string SkillRegistry::reload_skills(const std::vector<std::string>& scan_dirs) {
    // Default: scan .zlagent/skills/ if no dirs specified.
    std::vector<std::string> dirs = scan_dirs.empty() ? std::vector<std::string>{DEFAULT_SKILL_DIR} : scan_dirs;

    std::ostringstream oss;
    int updated = 0, removed = 0, unchanged = 0, errors = 0;

    // Collect all skill directories and their mtimes from disk.
    std::map<std::string, fs::path> disk_skills;   // dir -> SKILL.md path
    std::set<std::string> disk_skill_names;          // names found on disk

    for (const auto& dir : dirs) {
        if (!fs::exists(dir)) continue;
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (!entry.is_directory()) continue;
            std::string md_path = entry.path().string() + "/SKILL.md";
            if (!fs::exists(md_path)) continue;

            // Get mtime of the SKILL.md file.
            auto mtime = fs::last_write_time(md_path);
            disk_skills[entry.path().string()] = md_path;

            // Parse temporarily to get skill name for tracking.
            auto tmp = SkillLoader::parse_skill_md(md_path);
            if (tmp && !tmp->name.empty()) {
                disk_skill_names.insert(tmp->name);
            }
        }
    }

    // 1. Check existing skills: update changed, remove deleted.
    std::vector<std::string> to_remove;
    for (auto& [name, skill] : skills_) {
        if (skill->source_type != "native") continue;

        auto it = disk_skills.find(skill->source_path);
        if (it == disk_skills.end()) {
            // Skill directory no longer exists — remove.
            to_remove.push_back(name);
            continue;
        }

        auto mtime_it = skill_mtime_.find(skill->source_path);
        if (mtime_it != skill_mtime_.end() && mtime_it->second == fs::last_write_time(it->second)) {
            // No change.
            ++unchanged;
            continue;
        }

        // File changed — re-parse and update in-place.
        auto new_skill = SkillLoader::parse_skill_md(it->second.string());
        if (new_skill && !new_skill->name.empty()) {
            *skill = *new_skill;  // Preserve the shared_ptr, update contents.
            skill->source_type = "native";
            ++updated;
            LOG_INFO("Skill", u8"  \u2714 reload: " + name);
        } else {
            ++errors;
            LOG_WARN("Skill", "  failed to re-parse: " + it->second.string());
        }
    }

    // Remove skills that no longer exist on disk.
    std::set<std::string> removed_paths;
    for (const auto& name : to_remove) {
        auto it = skills_.find(name);
        if (it != skills_.end()) {
            skill_mtime_.erase(it->second->source_path);
            removed_paths.insert(it->second->source_path);
        }
        skills_.erase(name);
        ++removed;
        LOG_INFO("Skill", u8"  \u2718 removed: " + name);
    }

    // Update tracked mtimes.
    for (const auto& [dir_path, md_path] : disk_skills) {
        skill_mtime_[dir_path] = fs::last_write_time(md_path.string());
    }

    // 2. Discover new skills on disk that are not yet in the registry.
    for (const auto& [dir_path, md_path] : disk_skills) {
        if (removed_paths.count(dir_path)) continue;
        if (skill_mtime_.count(dir_path)) continue;  // already tracked

        auto new_skill = SkillLoader::parse_skill_md(md_path.string());
        if (new_skill && !new_skill->name.empty()) {
            new_skill->source_type = "native";
            register_skill(new_skill);
            skill_mtime_[dir_path] = fs::last_write_time(md_path.string());
            ++updated;
            LOG_INFO("Skill", u8"  \u2714 added: " + new_skill->name);
        }
    }

    oss << u8"Skill hot-reload complete: "
        << updated << u8" updated, "
        << removed << u8" removed, "
        << unchanged << u8" unchanged";
    if (errors > 0) {
        oss << ", " << errors << u8" errors";
    }

    return oss.str();
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
