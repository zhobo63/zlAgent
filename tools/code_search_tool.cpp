#include "tool.h"
#include "encoding.h"
#include "wide_string.h"
#include "json.hpp"
#include <fstream>
#include <sstream>
#include <vector>
#include <regex>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>
#endif

namespace agent {
using json = nlohmann::json;

class CodeSearchTool : public Tool {
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
            auto args = json::parse(json_args);
            std::string pattern = args.value("pattern", "");
            std::string directory = args.value("directory", "");
            std::string file_pattern = args.value("file_pattern", "");

            if (pattern.empty()) return "Error: No search pattern provided.";
            if (directory.empty()) directory = ".";

            try {
                std::regex re(pattern);

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
#ifdef _WIN32
        std::string search_path = dir + "\\*";
        if (!file_pattern.empty()) {
            search_path = dir + "\\" + file_pattern;
        }

        std::wstring wsearch = agent::utf8_to_wide(search_path);
        WIN32_FIND_DATAW find_data;
        HANDLE hFind = FindFirstFileW(wsearch.c_str(), &find_data);
        if (hFind == INVALID_HANDLE_VALUE) return;

        do {
            std::string fname = agent::wide_to_utf8(find_data.cFileName);
            if (fname == "." || fname == "..") continue;

            std::string full_path = dir + "\\" + fname;

            if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                search_directory(full_path, file_pattern, re, results, match_count, max_results);
            } else {
                search_file(full_path, re, results, match_count, max_results);
            }

            if (match_count >= max_results) break;
        } while (FindNextFileW(hFind, &find_data) && match_count < max_results);

        FindClose(hFind);
#else
        DIR* dir_handle = opendir(dir.c_str());
        if (!dir_handle) return;

        struct dirent* entry;
        while ((entry = readdir(dir_handle)) != nullptr && match_count < max_results) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;

            // Check file pattern filter
            if (!file_pattern.empty() && !match_glob(name, file_pattern)) continue;

            std::string full_path = dir + "/" + name;

            struct stat st;
            if (stat(full_path.c_str(), &st) != 0) continue;

            if (S_ISDIR(st.st_mode)) {
                search_directory(full_path, file_pattern, re, results, match_count, max_results);
            } else {
                search_file(full_path, re, results, match_count, max_results);
            }
        }

        closedir(dir_handle);
#endif
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
