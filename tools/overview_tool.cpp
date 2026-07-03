#include "pch.h"

#include <cstdio>
#include <filesystem>
#include <algorithm>

#include "tool.h"
#include "agent.h"
#include "json.hpp"

namespace agent {
using json = nlohmann::json;

// -----------------------------------------------------------------------
// ProjectOverviewTool - gives a bird's-eye view of the entire project
// in one call, instead of requiring multiple tool invocations.
// -----------------------------------------------------------------------
class ProjectOverviewTool : public Tool {
public:
    std::string name() const override { return "project_overview"; }

    std::string description() const override {
        return u8"Get a overview of the project in one call within a directory. "
               "Use this at the start of any task to quickly understand the project landscape.";
    }

    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["directory"]["type"] = "string";
        schema["properties"]["directory"]["description"] = "Directory to search in (default: current dir)";
        return schema.dump();
    }

    std::string execute(const std::string& json_args) override {
        try {
            auto args = json::parse(json_args);
            std::string directory = args.value("directory", "");
            if (directory.empty()) directory = ".";

            std::string result = overview(directory);

            // Trigger local tool discovery using the generated overview.
            Agent* ag = get_global_agent();
            if (ag) {
                ag->discover_local_tools_from_overview(result);
            }

            return result;
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        } catch (const std::exception& e) {
            return "Error: " + std::string(e.what());
        }
    }

    std::string overview(const std::string &directory) {
        LOG_DEBUG("ProjectOverviewTool", directory);

        std::ostringstream oss;

        // ── Header ───────────────────────────────────────────────
        oss << "\n";
        oss << u8"📂 PROJECT OVERVIEW\n";
        oss << u8"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n";

        // ── Build System ─────────────────────────────────────────
        append_build_info(oss, directory);

        // ── Code Metrics ─────────────────────────────────────────
        int depth = 5;
        append_code_metrics(oss, depth, directory);

        // ── VCS Status ───────────────────────────────────────────
        append_vcs_status(oss, directory);

        return oss.str();
    }

private:
    // ── Helpers ────────────────────────────────────────────────────────

    static bool should_include(const std::vector<std::string>& sections, const char* target) {
        for (const auto& s : sections) {
            if (s == "all" || s == target) return true;
        }
        return false;
    }

    // Count files and estimate lines in a directory tree.
    static void count_files(const std::string& dir, int max_depth, int current_depth,
                            std::unordered_map<std::string, int>& file_counts,
                            std::unordered_map<std::string, size_t>& line_estimates) {
        if (current_depth > max_depth) return;

        namespace fs = std::filesystem;
        try {
            for (const auto& entry : fs::recursive_directory_iterator(dir)) {
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                file_counts[ext]++;
                // Rough line estimate: ~50 bytes per line average.
                line_estimates[ext] += static_cast<size_t>(entry.file_size()) / 50;
            }
        } catch (const std::exception& /*e*/) {
            // Ignore inaccessible directories.
        }

        if (current_depth < max_depth) {
            try {
                for (const auto& entry : fs::directory_iterator(dir)) {
                    if (!entry.is_directory()) continue;
                    const auto& dname = entry.path().filename().string();
                    // Skip hidden and build dirs.
                    if (dname.substr(0, 1) == "." || dname == "build" || dname == ".git") continue;
                    count_files(entry.path().string(), max_depth, current_depth + 1,
                                file_counts, line_estimates);
                }
            } catch (const std::exception& /*e*/) {
                // Ignore.
            }
        }
    }

    void append_build_info(std::ostringstream& oss, const std::string& dir) {
        oss << u8"🔧 BUILD SYSTEM\n";
        namespace fs = std::filesystem;

        // Detect build system files in the target directory.
        struct BuildFile { std::string pattern; std::string label; };
        static const std::vector<BuildFile> known_files = {
            { "CMakeLists.txt",    "CMake" },
            { "Makefile",          "Make" },
            { ".sln",             "Visual Studio (MSVC)" },
            { ".vcxproj",         "Visual Studio (MinGW/MSBuild)" },
            { "package.json",      "Node.js / npm" },
            { "pom.xml",           "Maven" },
            { "build.gradle",      "Gradle" },
            { "Cargo.toml",        "Rust (Cargo)" },
            { "go.mod",            "Go modules" },
            { "setup.py",          "Python (setuptools)" },
            { "pyproject.toml",    "Python (PEP 517/518)" },
        };

        bool found = false;
        for (const auto& bf : known_files) {
            if (bf.pattern[0] == '.') {
                // Glob-like: check any file with this extension.
                try {
                    for (const auto& entry : fs::directory_iterator(dir)) {
                        if (!entry.is_regular_file()) continue;
                        if (entry.path().extension().string() == bf.pattern) {
                            oss << "   - " << bf.label << ": " << entry.path().filename().string() << "\n";
                            found = true;
                        }
                    }
                } catch (...) {}
            } else {
                if (fs::exists(dir + "/" + bf.pattern)) {
                    oss << "   - " << bf.label << ": " << bf.pattern << "\n";
                    found = true;
                }
            }
        }

        if (!found) {
            oss << "   - No known build system detected\n";
        }
    }

    void append_code_metrics(std::ostringstream& oss, int depth, const std::string& dir) {
        oss << u8"📊 CODE METRICS (MAX DEPTH=" << std::to_string(depth) << ")\n";

        namespace fs = std::filesystem;

        // Count source files by extension.
        std::unordered_map<std::string, int> file_counts;
        try {
            for (const auto& entry : fs::recursive_directory_iterator(dir)) {
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                if (ext.empty()) continue;
                file_counts[ext]++;
            }
        } catch (...) {}

        // Print sorted by count descending.
        std::vector<std::pair<std::string, int>> sorted(file_counts.begin(), file_counts.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

        for (const auto& [ext, count] : sorted) {
            oss << "   - " << ext << ": " << count << " files\n";
        }

        if (sorted.empty()) {
            oss << "   - No source files found\n";
        }

        // Top-level directory summary.
        try {
            for (const auto& entry : fs::directory_iterator(dir)) {
                if (!entry.is_directory()) continue;
                const auto& dname = entry.path().filename().string();
                if (dname.substr(0, 1) == "." || dname == "build" || dname == ".git") continue;

                oss << u8"📁 " << dname << "/\n";
                // List immediate subdirectories and files.
                try {
                    for (const auto& sub : fs::directory_iterator(entry.path())) {
                        std::string label;
                        if (sub.is_directory()) {
                            label = u8"  📂 " + sub.path().filename().string() + u8"/";
                        } else {
                            label = u8"  📄 " + sub.path().filename().string();
                        }
                        oss << label << "\n";
                    }
                } catch (...) {}
            }
        } catch (...) {}

        oss << "\n";
    }

    void append_vcs_status(std::ostringstream& oss, const std::string& dir) {
        oss << u8"📦 CURRENT STATUS\n";

        namespace fs = std::filesystem;

        // Detect which VCS is present.
        bool has_git  = fs::exists(dir + "/.git");
        bool has_hg   = fs::exists(dir + "/.hg");
        bool has_svn  = fs::exists(dir + "/.svn");

        if (has_git) {
            append_git_status(oss, dir);
        }
        if (has_hg) {
            append_hg_status(oss, dir);
        }
        if (has_svn) {
            append_svn_status(oss, dir);
        }

        if (!has_git && !has_hg && !has_svn) {
            oss << "   - No version control detected\n";
        }

        // Check if build directory exists.
        if (fs::exists(dir + "/build")) {
            oss << "   - Build dir: exists\n";
        } else {
            oss << "   - Build dir: not found\n";
        }
        oss << "\n";
    }

    void append_git_status(std::ostringstream& oss, const std::string& dir) {
        auto output = execute_shell_command("git -C " + dir + " status --porcelain");
        if (output.empty()) {
            oss << u8"   - Git: clean working tree\n";
        } else {
            int modified = 0, added = 0, deleted = 0, untracked = 0;
            for (const auto& line : output) {
                if (line.size() >= 2) {
                    char s = line[0]; // staged
                    char c = line[1]; // unstaged
                    if (s == ' ' && c != ' ') untracked++;
                    else if (s == 'A' || s == 'M') added++;
                    else if (s == 'D') deleted++;
                    else modified++;
                }
            }
            oss << "   - Git: ";
            bool first = true;
            auto append_part = [&](int count, const char* label) {
                if (count > 0) {
                    if (!first) oss << ", ";
                    oss << count << " " << label;
                    first = false;
                }
            };
            append_part(modified, "modified");
            append_part(added, "added");
            append_part(deleted, "deleted");
            append_part(untracked, "untracked");
            if (first) oss << "clean";
            oss << "\n";
        }

        // Git submodule status.
#if defined(_WIN32)
        auto sub_output = execute_shell_command("git -C " + dir + " submodule status --recursive 2>NUL");
#else
        auto sub_output = execute_shell_command("git -C " + dir + " submodule status --recursive 2>/dev/null");
#endif
        if (!sub_output.empty()) {
            int sub_count = static_cast<int>(sub_output.size());
            oss << "   - Git submodules: " << sub_count << " found\n";
            for (const auto& line : sub_output) {
                oss << "      " << line << "\n";
            }
        } else {
            oss << "   - Git submodules: none\n";
        }
    }

    void append_hg_status(std::ostringstream& oss, const std::string& dir) {
        auto output = execute_shell_command("hg -R " + dir + " status");
        if (output.empty()) {
            oss << u8"   - Mercurial: clean working tree\n";
        } else {
            int modified = 0, added = 0, removed = 0, deleted = 0, untracked = 0;
            for (const auto& line : output) {
                if (!line.empty()) {
                    char status = line[0];
                    switch (status) {
                        case 'M': modified++; break;
                        case 'A': added++; break;
                        case 'R': removed++; break;
                        case '!': deleted++; break;
                        case '?': untracked++; break;
                    }
                }
            }
            oss << "   - Mercurial: ";
            bool first = true;
            auto append_part = [&](int count, const char* label) {
                if (count > 0) {
                    if (!first) oss << ", ";
                    oss << count << " " << label;
                    first = false;
                }
            };
            append_part(modified, "modified");
            append_part(added, "added");
            append_part(removed, "removed");
            append_part(deleted, "deleted");
            append_part(untracked, "untracked");
            if (first) oss << "clean";
            oss << "\n";
        }
    }

    void append_svn_status(std::ostringstream& oss, const std::string& dir) {
        auto output = execute_shell_command("svn status --quiet " + dir);
        if (output.empty()) {
            oss << u8"   - SVN: clean working tree\n";
        } else {
            int modified = 0, added = 0, deleted = 0, untracked = 0, conflicted = 0;
            for (const auto& line : output) {
                if (!line.empty()) {
                    char status = line[0];
                    switch (status) {
                        case 'M': modified++; break;
                        case 'A': added++; break;
                        case 'D': deleted++; break;
                        case '?': untracked++; break;
                        case 'C': conflicted++; break;
                    }
                }
            }
            oss << "   - SVN: ";
            bool first = true;
            auto append_part = [&](int count, const char* label) {
                if (count > 0) {
                    if (!first) oss << ", ";
                    oss << count << " " << label;
                    first = false;
                }
            };
            append_part(modified, "modified");
            append_part(added, "added");
            append_part(deleted, "deleted");
            append_part(untracked, "untracked");
            append_part(conflicted, "conflicted");
            if (first) oss << "clean";
            oss << "\n";
        }
    }

    // Execute a shell command and return output lines.
    static std::vector<std::string> execute_shell_command(const std::string& cmd) {
        LOG_DEBUG("execute_shell_command", cmd);
        std::vector<std::string> result;
#if defined(_WIN32)
        FILE* pipe = _popen(cmd.c_str(), "r");
#else
        FILE* pipe = popen(cmd.c_str(), "r");
#endif
        if (!pipe) return result;

        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe)) {
            std::string line(buffer);
            // Remove trailing newline.
            if (!line.empty() && line.back() == '\n') line.pop_back();
            if (!line.empty() && line.back() == '\r') line.pop_back();
            result.push_back(line);
        }

#if defined(_WIN32)
        _pclose(pipe);
#else
        pclose(pipe);
#endif
        std::ostringstream result_log;
        for (size_t i = 0; i < result.size(); ++i) {
            if (i > 0) result_log << "\n";
            result_log << result[i];
        }
        LOG_DEBUG("execute_shell_command", "result:" + result_log.str());
        return result;
    }
};

// Factory function (matching the pattern of other tools).
ToolPtr create_project_overview_tool() {
    return std::make_shared<ProjectOverviewTool>();
}

} // namespace agent
