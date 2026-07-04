#include "pch.h"

#include "tool.h"
#include "encoding.h"
#include <regex>
#include <cstring>
#include <set>

namespace agent {
using json = nlohmann::json;

class CodeSearchTool : public Tool {
private:
    // Load excluded directory names from .gitignore / .hgignore
    std::set<std::string> load_ignored_dirs(const std::filesystem::path& root) {
        std::set<std::string> ignored;
        for (const auto& ignore_file : {".gitignore", ".hgignore"}) {
            std::ifstream f(root / ignore_file);
            if (!f.is_open()) continue;
            std::string line;
            while (std::getline(f, line)) {
                // Trim whitespace
                size_t start = line.find_first_not_of(" \t\r\n");
                if (start == std::string::npos) continue;
                size_t end = line.find_last_not_of(" \t\r\n");
                line = line.substr(start, end - start + 1);

                // Skip comments and empty lines
                if (line.empty() || line[0] == '#') continue;

                // Strip trailing slash to get directory name
                std::string dir_name = line;
                if (!dir_name.empty() && dir_name.back() == '/')
                    dir_name.pop_back();

                // Only add simple directory names (no wildcards, no path separators)
                if (!dir_name.empty() &&
                    dir_name.find('*') == std::string::npos &&
                    dir_name.find('?') == std::string::npos &&
                    dir_name.find('/') == std::string::npos) {
                    ignored.insert(dir_name);
                }
            }
        }
        return ignored;
    }

public:
    std::string name() const override { return "search_code"; }
    std::string description() const override {
        return "Search for text patterns in C++ source files within a directory. "
               "Supports regex patterns and file glob filters (e.g., *.cpp, *.h). "
               "Returns matching lines with file path and line number.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["pattern"]["type"] = "string";
        schema["properties"]["pattern"]["description"] = "Regex pattern to search for";
        schema["properties"]["directory"]["type"] = "string";
        schema["properties"]["directory"]["description"] = "Directory to search in (default: current dir)";
        schema["properties"]["file_pattern"]["type"] = "string";
        schema["properties"]["file_pattern"]["description"] = "File glob filter, e.g. '*.cpp' or '*.h'";
        schema["required"] = {"pattern"};
        return schema.dump();
    }

    std::string execute(const std::string& json_args) override {
        try {
            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
            auto args = json::parse(json_args);
            std::string pattern = args.value("pattern", "");
            std::string directory = args.value("directory", "");
            std::string file_pattern = args.value("file_pattern", "");

            if (pattern.empty()) return "Error: No search pattern provided.";
            if (directory.empty()) directory = ".";

            try {
                std::regex re(pattern, std::regex_constants::icase);

                std::ostringstream results;
                int match_count = 0;
                const int MAX_RESULTS = 50;

                search_directory(directory, file_pattern, re, results, match_count, MAX_RESULTS);

                if (match_count == 0) {
                    return "No matches found for pattern '" + pattern + "' in '" + directory + "'";
                }

                return results.str();
            } catch (const std::regex_error& e) {
                return "Invalid regex pattern: " + std::string(e.what());
            }
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }

private:

    void search_directory(const std::string& dir, const std::string& file_pattern,
                          const std::regex& re, std::ostringstream& results,
                          int& match_count, int max_results) {
        namespace fs = std::filesystem;
        LOG_DEBUG("CodeSearchTool", "search_directory:" + dir + " pattern:" + file_pattern);
        if (dir.empty()) {
            LOG_DEBUG("CodeSearchTool", "search_directory ignore: empty dir");
            return;
        }

        // Resolve the path so that "." and relative paths work correctly.
        fs::path resolved = fs::absolute(dir);
        if (!fs::exists(resolved) || !fs::is_directory(resolved)) {
            LOG_DEBUG("CodeSearchTool", "search_directory ignore: not a valid directory");
            return;
        }

        // Load ignored directories from .gitignore / .hgignore at the search root
        auto ignored_dirs = load_ignored_dirs(resolved);
        if (!ignored_dirs.empty()) {
            LOG_DEBUG("CodeSearchTool", "ignored dirs: " + std::string("") + ", count=" + std::to_string(ignored_dirs.size()));
        }

        search_dir_recursive(resolved, file_pattern, re, results, match_count, max_results, ignored_dirs);
    }

    // Manual recursive traversal so we can skip hidden directories entirely.
    void search_dir_recursive(const std::filesystem::path& dir, const std::string& file_pattern,
                              const std::regex& re, std::ostringstream& results,
                              int& match_count, int max_results,
                              const std::set<std::string>& ignored_dirs) {
        namespace fs = std::filesystem;
        try {
            for (const auto& entry : fs::directory_iterator(dir)) {
                if (match_count >= max_results) break;

                // Skip hidden directories (starting with '.') like .git, .hg, .vs, .vscode etc.
                if (entry.is_directory()) {
                    std::string dirname = entry.path().filename().string();
                    if (!dirname.empty() && dirname[0] == '.') continue;
                    // Skip directories listed in .gitignore / .hgignore
                    if (ignored_dirs.count(dirname)) continue;
                    search_dir_recursive(entry.path(), file_pattern, re, results, match_count, max_results, ignored_dirs);
                    continue;
                }

                if (entry.is_regular_file()) {
                    // Check file pattern filter
                    std::string fname = entry.path().filename().string();
                    if (!file_pattern.empty() && !match_glob(fname, file_pattern)) continue;

                    search_file(entry.path().string(), re, results, match_count, max_results);
                }
            }
        } catch (const fs::filesystem_error&) {
            // Skip directories we can't access.
        }
    }

    // Simple glob matcher for patterns like "*.cpp" or "*.h"
    static bool match_glob(const std::string& filename, const std::string& pattern) {
        if (pattern.empty()) return true;

        // Handle wildcard at start: *.ext
        size_t star = pattern.find('*');
        if (star == 0 && star + 1 < pattern.size()) {
            std::string suffix = pattern.substr(star + 1);
            size_t fpos = filename.rfind(suffix);
            return fpos != std::string::npos && fpos + suffix.size() == filename.size();
        }

        // Exact match fallback
        return filename == pattern;
    }

    void search_file(const std::string& filepath, const std::regex& re,
                     std::ostringstream& results, int& match_count, int max_results) {
        LOG_DEBUG("CodeSearchTool", "search_file:" + filepath);
        std::ifstream file(filepath);
        if (!file.is_open()) return;

        std::string line;
        int line_num = 0;
        while (std::getline(file, line) && match_count < max_results) {
            line_num++;
            if (std::regex_search(line, re)) {
                results << filepath << ":" << line_num << ": " << line << "\n";
                match_count++;
            }
        }
    }
};

ToolPtr create_code_search_tool() {
    return std::make_shared<CodeSearchTool>();
}

} // namespace agent
