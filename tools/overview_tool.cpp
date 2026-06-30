#include "pch.h"

#include <cstdio>
#include <filesystem>
#include <algorithm>

#include "tool.h"
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
        return u8"Get a overview of the project in one call. "
               "Use this at the start of any task to quickly understand the project landscape.";
    }

    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["query"]["type"] = "string";
        schema["properties"]["query"]["description"] = "What you're looking for (natural language)";
        schema["required"] = { "query" };
        return schema.dump();
    }

    std::string execute(const std::string& json_args) override {
        try {
            // Parse optional arguments
            int depth = 2;
            std::vector<std::string> sections = {"all"};

			LOG_DEBUG("ProjectOverviewTool", "Executing"); 

            std::ostringstream oss;

            // ── Header ───────────────────────────────────────────────
            oss << "\n";
            oss << u8"📂 PROJECT OVERVIEW\n";
            oss << u8"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n";

            // ── Build System ─────────────────────────────────────────
            append_build_info(oss);
            
            // ── Code Metrics ─────────────────────────────────────────
            append_code_metrics(oss, depth);
            
            // ── Git Status ───────────────────────────────────────────
            append_git_status(oss);
            
            return oss.str();

        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        } catch (const std::exception& e) {
            return "Error: " + std::string(e.what());
        }
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

    void append_build_info(std::ostringstream& oss) {
        oss << u8"🔧 BUILD SYSTEM\n";
    }

    void append_code_metrics(std::ostringstream& oss, int depth) {
        oss << u8"📊 CODE METRICS\n";

        // Top-level directory summary.
        namespace fs = std::filesystem;
        try {
            for (const auto& entry : fs::directory_iterator(".")) {
                if (!entry.is_directory()) continue;
                const auto& dname = entry.path().filename().string();
                if (dname.substr(0, 1) == "." || dname == "build" || dname == ".git") continue;

                int sub_count = 0;
                try {
                    for (const auto& _ : fs::recursive_directory_iterator(entry.path())) {
                        ++sub_count;
                    }
                } catch (...) {}

                if (entry.is_directory()) {
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
            }
        } catch (...) {}

        oss << "\n";
    }

    void append_git_status(std::ostringstream& oss) {
        oss << u8"📦 CURRENT STATUS\n";

        // Try to get git status via shell command.
        auto output = execute_shell_command("git status --porcelain");
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

        // Check if build directory exists.
        namespace fs = std::filesystem;
        if (fs::exists("build")) {
            oss << "   - Build dir: exists\n";
        } else {
            oss << "   - Build dir: not found\n";
        }
        oss << "\n";
    }

    // Execute a shell command and return output lines.
    static std::vector<std::string> execute_shell_command(const std::string& cmd) {
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
        return result;
    }
};

// Factory function (matching the pattern of other tools).
ToolPtr create_project_overview_tool() {
    return std::make_shared<ProjectOverviewTool>();
}

} // namespace agent
