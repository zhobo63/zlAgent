#include "pch.h"

#include "tool.h"
#include "safety_guard.h"
#include "safety_guard.h"
#include "httplib.h"
#include "file_utils.h"
#include <iomanip>
#include <regex>

#ifndef _WIN32
#include <cstdio>
#include <cstdlib>
#endif

namespace agent {
using json = nlohmann::json;
namespace fs = std::filesystem;

// -----------------------------------------------------------------------
// Shared utility: cross-platform command execution (used by run_build + git tools)
// -----------------------------------------------------------------------
static std::string execute_shell_command(const std::string& cmd, const std::string& cwd) {
    std::string shell_cmd = cmd;
    if (!cwd.empty()) {
        shell_cmd = "cd '" + cwd + "' && " + cmd;
    }

    FILE* pipe = popen(shell_cmd.c_str(), "r");
    if (!pipe) return "Error: Failed to execute command.";

    std::string output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        output += buffer;
    }

    pclose(pipe);
    return output;
}

// -----------------------------------------------------------------------
// CreateDirectoryTool - mkdir -p equivalent
// -----------------------------------------------------------------------
class CreateDirectoryTool : public Tool {
public:
    std::string name() const override { return "create_directory"; }
    std::string description() const override {
        return "Create a new directory and all necessary parent directories. "
               "Similar to 'mkdir -p'. Returns confirmation or error.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["path"]["type"] = "string";
        schema["properties"]["path"]["description"] = "The directory path to create";
        schema["required"] = {"path"};
        return schema.dump();
    }

    std::string execute(const std::string& json_args) override {
        try {
            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                return "Error: Invalid JSON arguments - not json";
            }
            std::string path = args.value("path", "");
            if (path.empty()) return "Error: No directory path provided.";

            std::error_code ec;
            bool created = fs::create_directories(path, ec);

            if (ec) {
                return "Error: Failed to create directory '" + path + "' - " + ec.message();
            }

            if (!created) {
                // Path already exists - check what it is
                if (fs::is_directory(path)) {
                    return "Directory '" + path + "' already exists.";
                }
                return "Error: A file with name '" + path + "' already exists and is not a directory.";
            }

            return "Successfully created directory '" + path + "'";
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }
};

// -----------------------------------------------------------------------
// DeletePathTool - rm / rm -rf equivalent
// -----------------------------------------------------------------------
class DeletePathTool : public Tool {
public:
    std::string name() const override { return "delete_path"; }
    std::string description() const override {
        return "Delete a file or directory. If the path is a directory, it will be removed recursively (like 'rm -rf'). Returns confirmation or error.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["path"]["type"] = "string";
        schema["properties"]["path"]["description"] = "The file or directory path to delete";
        schema["required"] = {"path"};
        return schema.dump();
    }

    std::string execute(const std::string& json_args) override {
        try {
            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                return "Error: Invalid JSON arguments - not json";
            }
            std::string path = args.value("path", "");
            if (path.empty()) return "Error: No path provided.";

            // Safety: integrated path check (working dir + whitelist + strict mode).
            auto check_result = SafetyGuard::get_instance().is_path_ok(path);
            if (check_result == PathCheckResult::Denied) {
                return "Error: Path '" + path + "' is outside allowed directories. Operation denied.";
            }
            if (!fs::exists(path)) {
                return "Error: Path '" + path + "' does not exist.";
            }

            std::error_code ec;
            if (fs::is_directory(path)) {
                fs::remove_all(path, ec);
            } else {
                fs::remove(path, ec);
            }

            if (ec) {
                return "Error: Failed to delete '" + path + "' - " + ec.message();
            }

            return "Successfully deleted '" + path + "'";
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }
};

// -----------------------------------------------------------------------
// CopyPathTool - cp / cp -r equivalent
// -----------------------------------------------------------------------
class CopyPathTool : public Tool {
public:
    std::string name() const override { return "copy_path"; }
    std::string description() const override {
        return "Copy a file or directory to a new location. Directory contents are copied recursively. Returns confirmation or error.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["source_path"]["type"] = "string";
        schema["properties"]["source_path"]["description"] = "The source file or directory path to copy from";
        schema["properties"]["destination_path"]["type"] = "string";
        schema["properties"]["destination_path"]["description"] = "The destination path to copy to";
        schema["required"] = {"source_path", "destination_path"};
        return schema.dump();
    }

    std::string execute(const std::string& json_args) override {
        try {
            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                return "Error: Invalid JSON arguments - not json";
            }
            std::string source = args.value("source_path", "");
            std::string dest   = args.value("destination_path", "");

            if (source.empty()) return "Error: No source path provided.";
            if (dest.empty())   return "Error: No destination path provided.";

            // Safety: integrated path check (working dir + whitelist + strict mode).
            auto src_result = SafetyGuard::get_instance().is_path_ok(source);
            if (src_result == PathCheckResult::Denied) {
                return "Error: Source path '" + source + "' is outside allowed directories. Operation denied.";
            }
            auto dst_result = SafetyGuard::get_instance().is_path_ok(dest);
            if (dst_result == PathCheckResult::Denied) {
                return "Error: Destination path '" + dest + "' is outside allowed directories. Operation denied.";
            }

            // Check source exists
            if (!fs::exists(source)) {
                return "Error: Source path '" + source + "' does not exist.";
            }

            // Check destination doesn't already exist
            if (fs::exists(dest)) {
                return "Error: Destination path '" + dest + "' already exists.";
            }

            std::error_code ec;
            if (fs::is_directory(source)) {
                fs::copy(source, dest, fs::copy_options::recursive, ec);
            } else {
                fs::copy_file(source, dest, fs::copy_options::none, ec);
            }

            if (ec) {
                return "Error: Failed to copy '" + source + "' to '" + dest + "' - " + ec.message();
            }

            return "Successfully copied '" + source + "' to '" + dest + "'";
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }
};

// -----------------------------------------------------------------------
// MovePathTool - mv / rename equivalent
// -----------------------------------------------------------------------
class MovePathTool : public Tool {
public:
    std::string name() const override { return "move_path"; }
    std::string description() const override {
        return "Move or rename a file or directory. If source and destination are in the same directory, this performs a rename. Returns confirmation or error.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["source_path"]["type"] = "string";
        schema["properties"]["source_path"]["description"] = "The source file or directory path to move from";
        schema["properties"]["destination_path"]["type"] = "string";
        schema["properties"]["destination_path"]["description"] = "The destination path to move to (or new name)";
        schema["required"] = {"source_path", "destination_path"};
        return schema.dump();
    }

    std::string execute(const std::string& json_args) override {
        try {
            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                return "Error: Invalid JSON arguments - not json";
            }
            std::string source = args.value("source_path", "");
            std::string dest   = args.value("destination_path", "");

            if (source.empty()) return "Error: No source path provided.";
            if (dest.empty())   return "Error: No destination path provided.";

            // Safety: integrated path check (working dir + whitelist + strict mode).
            auto src_result = SafetyGuard::get_instance().is_path_ok(source);
            if (src_result == PathCheckResult::Denied) {
                return "Error: Source path '" + source + "' is outside allowed directories. Operation denied.";
            }
            auto dst_result = SafetyGuard::get_instance().is_path_ok(dest);
            if (dst_result == PathCheckResult::Denied) {
                return "Error: Destination path '" + dest + "' is outside allowed directories. Operation denied.";
            }

            // Check source exists
            if (!fs::exists(source)) {
                return "Error: Source path '" + source + "' does not exist.";
            }

            // Check destination doesn't already exist
            if (fs::exists(dest)) {
                return "Error: Destination path '" + dest + "' already exists.";
            }

            std::error_code ec;
            fs::rename(source, dest, ec);

            if (ec) {
                return "Error: Failed to move '" + source + "' to '" + dest + "' - " + ec.message();
            }

            return "Successfully moved '" + source + "' to '" + dest + "'";
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }
};

// -----------------------------------------------------------------------
// Factory functions
// -----------------------------------------------------------------------

ToolPtr create_create_directory_tool() {
    return std::make_shared<CreateDirectoryTool>();
}

ToolPtr create_delete_path_tool() {
    return std::make_shared<DeletePathTool>();
}

ToolPtr create_copy_path_tool() {
    return std::make_shared<CopyPathTool>();
}

ToolPtr create_move_path_tool() {
    return std::make_shared<MovePathTool>();
}

// -----------------------------------------------------------------------
// FindFilesTool - glob-based file path search (like find_path)
// -----------------------------------------------------------------------
class FindFilesTool : public Tool {
public:
    std::string name() const override { return "find_files"; }
    std::string description() const override {
        return "Find file paths that match a glob pattern recursively. "
               "Supports patterns like '**/*.cpp', '*.h', 'src/**/*.ts'. "
               "Returns matching file paths, one per line.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["glob"]["type"] = "string";
        schema["properties"]["glob"]["description"] = "Glob pattern to match (e.g. '**/*.cpp', '*.h')";
        schema["properties"]["directory"]["type"] = "string";
        schema["properties"]["directory"]["description"] = "Directory to search in (default: current dir)";
        schema["required"] = {"glob"};
        return schema.dump();
    }

    std::string execute(const std::string& json_args) override {
        try {
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                return "Error: Invalid JSON arguments - not json";
            }
            std::string glob = args.value("glob", args.value("pattern", ""));
            std::string directory = args.value("directory", ".");

            if (glob.empty()) return "Error: No glob pattern provided.";

            // Parse the glob into a base path and filename pattern
            GlobPattern parsed = parse_glob(glob);

            std::ostringstream results;
            int count = 0;
            const int MAX_RESULTS = 50;

            search_directory(directory, parsed, results, count, MAX_RESULTS);

            if (count == 0) {
                return "No files found matching '" + glob + "' in '" + directory + "'";
            }

            return results.str();
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }

private:
    struct GlobPattern {
        bool recursive = false;  // true if pattern contains **
        std::string dir_prefix;  // directory prefix before filename pattern (e.g. "src/")
        std::string file_pattern; // filename glob (e.g. "*.cpp")
    };

    GlobPattern parse_glob(const std::string& glob) {
        GlobPattern result;

        // Check for **/ prefix (recursive search)
        size_t double_star = glob.find("**/");
        if (double_star != std::string::npos) {
            result.recursive = true;
            result.dir_prefix = glob.substr(0, double_star);
            result.file_pattern = glob.substr(double_star + 3);
        } else {
            // Non-recursive: just match filename in the given directory
            size_t last_sep = glob.find_last_of("/\\\x5c");
            if (last_sep != std::string::npos) {
                result.dir_prefix = glob.substr(0, last_sep + 1);
                result.file_pattern = glob.substr(last_sep + 1);
            } else {
                result.file_pattern = glob;
            }
        }

        return result;
    }

    void search_directory(const std::string& dir,
                          const GlobPattern& pattern,
                          std::ostringstream& results,
                          int& count, int max_results) {
        try {
            for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied);
                 it != fs::recursive_directory_iterator();
                 ++it) {

                if (count >= max_results) break;

                auto& entry = *it;
                if (!entry.is_regular_file()) continue;

                std::string filename = entry.path().filename().string();

                // Check if the file matches our pattern
                if (match_glob(filename, pattern.file_pattern)) {
                    results << entry.path().string() << "\n";
                    count++;
                }
            }
        } catch (const fs::filesystem_error& e) {
            return; // Silently skip inaccessible directories
        }
    }
};

ToolPtr create_find_files_tool() {
    return std::make_shared<FindFilesTool>();
}

// -----------------------------------------------------------------------
// FileOutlineTool - extract symbol summary from large files
// -----------------------------------------------------------------------
class FileOutlineTool : public Tool {
public:
    std::string name() const override { return "get_file_outline"; }
    std::string description() const override {
        return "Get a symbol outline of a file (functions, classes, structs, namespaces) with line numbers. "
               "Useful for understanding large files without reading the full content. "
               "Supports C/C++, Python, JavaScript/TypeScript.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["path"]["type"] = "string";
        schema["properties"]["path"]["description"] = "The file path to analyze";
        schema["properties"]["start_line"]["type"] = "integer";
        schema["properties"]["start_line"]["description"] = "Optional: start line (1-based) for range analysis";
        schema["properties"]["end_line"]["type"] = "integer";
        schema["properties"]["end_line"]["description"] = "Optional: end line (1-based, inclusive) for range analysis";
        schema["required"] = {"path"};
        return schema.dump();
    }

    std::string execute(const std::string& json_args) override {
        try {
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                return "Error: Invalid JSON arguments - not json";
            }
            std::string path = args.value("path", "");
            int start_line = args.value("start_line", 0);
            int end_line   = args.value("end_line", -1);

            if (path.empty()) return "Error: No file path provided.";

            std::ifstream file(path);
            if (!file.is_open()) {
                return "Error: Cannot open file '" + path + "'";
            }

            // Read all lines
            std::vector<std::string> lines;
            std::string line;
            while (std::getline(file, line)) {
                lines.push_back(line);
            }
            file.close();

            if (lines.empty()) return "File is empty.";

            // Determine range to scan
            int from = start_line > 0 ? start_line - 1 : 0;  // convert to 0-based
            int to   = end_line > 0 && end_line <= static_cast<int>(lines.size())
                         ? end_line
                         : static_cast<int>(lines.size());

            if (from >= static_cast<int>(lines.size())) {
                return "Error: start_line exceeds file length (" + std::to_string(lines.size()) + " lines).";
            }

            // Detect language from extension
            std::string ext = get_extension(path);

            std::ostringstream outline;
            int symbol_count = 0;

            for (int i = from; i < to; i++) {
                auto symbols = extract_symbols(lines[i], ext, i + 1);  // 1-based line number
                for (const auto& sym : symbols) {
                    outline << "  " << std::setw(5) << sym.line << ": "
                            << sym.kind << " " << sym.name << "\n";
                    symbol_count++;
                }
            }

            if (symbol_count == 0) {
                return "No symbols found in '" + path + "'.";
            }

            std::ostringstream header;
            header << "# File: " << path << "\n"
                   << "# Lines: " << lines.size() << "\n";
            if (start_line > 0) {
                header << "# Range: lines " << start_line << "-" << end_line << "\n";
            }

            return header.str() + outline.str();
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }

private:
    struct Symbol {
        int line;
        std::string kind;  // "class", "struct", "namespace", "function", "method"
        std::string name;
    };

    static std::string get_extension(const std::string& path) {
        auto pos = path.rfind('.');
        if (pos == std::string::npos) return "";
        std::string ext = path.substr(pos);
        // lowercase
        for (auto& c : ext) c = static_cast<char>(std::tolower(c));
        return ext;
    }

    static std::vector<Symbol> extract_symbols(const std::string& line, const std::string& ext, int line_num) {
        std::vector<Symbol> symbols;
        std::string trimmed = trim(line);

        // Skip empty lines and comments
        if (trimmed.empty() || trimmed[0] == '/' || trimmed[0] == '#' || trimmed[0] == '//')
            return symbols;

        if (ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".cc" || ext == ".cxx") {
            parse_cpp_line(trimmed, line_num, symbols);
        } else if (ext == ".py") {
            parse_python_line(trimmed, line_num, symbols);
        } else if (ext == ".js" || ext == ".ts" || ext == ".jsx" || ext == ".tsx") {
            parse_js_line(trimmed, line_num, symbols);
        }

        return symbols;
    }

    static void parse_cpp_line(const std::string& line, int line_num, std::vector<Symbol>& out) {
        // namespace Name {  or  namespace Name
        auto ns = line.find("namespace ");
        if (ns != std::string::npos && !is_in_comment(line)) {
            std::string name = extract_identifier(line, ns + 10);
            if (!name.empty()) {
                out.push_back({line_num, "namespace", name});
            }
            return;
        }

        // class Name {  or  struct Name {
        for (const auto& kw : {std::string("class "), std::string("struct ")}) {
            size_t pos = line.find(kw);
            if (pos != std::string::npos && !is_in_comment(line)) {
                std::string name = extract_identifier(line, pos + kw.size());
                if (!name.empty()) {
                    out.push_back({line_num, kw.substr(0, kw.size()-1), name});
                }
                return;
            }
        }

        // Function: look for patterns like "type name(" or "type ClassName::name("
        // Skip lines that are just declarations inside class (no opening brace on same line usually)
        if (!is_in_comment(line)) {
            size_t paren = line.find('(');
            if (paren != std::string::npos && paren > 0) {
                // Check this looks like a function definition
                std::string before_paren = trim(line.substr(0, paren));
                if (!before_paren.empty() && !is_keyword(before_paren)) {
                    std::string name = extract_function_name(before_paren);
                    if (!name.empty()) {
                        // Determine return type (simplified)
                        out.push_back({line_num, "function", name});
                    }
                }
            }
        }
    }

    static void parse_python_line(const std::string& line, int line_num, std::vector<Symbol>& out) {
        // def function_name(
        if (line.substr(0, 4) == "def ") {
            size_t paren = line.find('(');
            if (paren != std::string::npos) {
                std::string name = trim(line.substr(4, paren - 4));
                if (!name.empty()) {
                    out.push_back({line_num, "function", name});
                }
            }
        }
        // class Name:
        else if (line.substr(0, 6) == "class ") {
            size_t colon = line.find(':');
            size_t paren = line.find('(');
            size_t end = std::min((colon != std::string::npos ? colon : line.size()),
                                  (paren != std::string::npos ? paren : line.size()));
            if (end > 6) {
                std::string name = trim(line.substr(6, end - 6));
                if (!name.empty()) {
                    out.push_back({line_num, "class", name});
                }
            }
        }
    }

    static void parse_js_line(const std::string& line, int line_num, std::vector<Symbol>& out) {
        // function Name(
        if (line.find("function ") != std::string::npos) {
            size_t pos = line.find("function ");
            size_t paren = line.find('(', pos);
            if (paren != std::string::npos && paren > pos + 9) {
                std::string name = trim(line.substr(pos + 9, paren - pos - 9));
                if (!name.empty()) {
                    out.push_back({line_num, "function", name});
                }
            }
        }
        // class Name
        else if (line.find("class ") != std::string::npos) {
            size_t pos = line.find("class ");
            size_t brace = line.find('{');
            size_t paren = line.find('(');
            size_t end = line.size();
            if (brace != std::string::npos) end = std::min((size_t)end, brace);
            if (paren != std::string::npos) end = std::min((size_t)end, paren);
            if (end > pos + 6) {
                std::string name = trim(line.substr(pos + 6, end - pos - 6));
                if (!name.empty()) {
                    out.push_back({line_num, "class", name});
                }
            }
        }
    }

    static bool is_in_comment(const std::string& line) {
        // Simple check: first non-whitespace chars are // or /*
        for (char c : line) {
            if (c == ' ' || c == '\t' || c == '\r') continue;
            return (line.size() >= 2 && ((c == '/' && line[1] == '/') ||
                     (c == '/' && line[1] == '*')));
        }
        return false;
    }

    static std::string extract_identifier(const std::string& s, size_t pos) {
        if (pos >= s.size()) return "";
        // Skip whitespace
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) pos++;
        if (pos >= s.size()) return "";

        size_t start = pos;
        while (pos < s.size() && (std::isalnum(static_cast<unsigned char>(s[pos])) || s[pos] == '_'))
            pos++;

        std::string result = s.substr(start, pos - start);
        // Remove trailing '{' or ':' if present
        return trim(result);
    }

    static std::string extract_function_name(const std::string& before_paren) {
        std::string trimmed = trim(before_paren);
        if (trimmed.empty()) return "";

        // Find the last word (the function name)
        size_t last_space = trimmed.find_last_of(' ');
        if (last_space == std::string::npos || last_space == 0) {
            // Could be just a name, or starts with type
            if (!is_keyword(trimmed)) return trimmed;
            return "";
        }

        std::string name = trimmed.substr(last_space + 1);
        if (name.empty() || is_keyword(name)) return "";
        return name;
    }

    static bool is_keyword(const std::string& s) {
        static const char* keywords[] = {
            "if", "else", "for", "while", "do", "switch", "case",
            "return", "break", "continue", "goto", "try", "catch",
            "throw", "new", "delete", "sizeof", "typeof",
            "int", "float", "double", "char", "bool", "void",
            "long", "short", "unsigned", "signed", "const", "static",
            "virtual", "override", "final", "inline", "explicit",
            "public", "private", "protected", "friend", "typedef",
            "using", "template", "typename", "auto", "constexpr",
            "nullptr", "true", "false", "namespace", "class", "struct"
        };
        for (const auto* kw : keywords) {
            if (s == kw) return true;
        }
        return false;
    }

    static std::string trim(const std::string& s) {
        size_t start = 0, end = s.size();
        while (start < end && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r')) start++;
        while (end > start && (s[end-1] == ' ' || s[end-1] == '\t' || s[end-1] == '\r' || s[end-1] == '{' || s[end-1] == ':')) end--;
        return s.substr(start, end - start);
    }
};

ToolPtr create_get_file_outline_tool() {
    return std::make_shared<FileOutlineTool>();
}

// -----------------------------------------------------------------------
// GrepWithContextTool - regex search with before/after context lines
// -----------------------------------------------------------------------
class GrepWithContextTool : public Tool {
public:
    std::string name() const override { return "grep_with_context"; }
    std::string description() const override {
        return "Search for a regex pattern in a file and return matching lines with context. "
               "Similar to 'grep -B/-A'. Matched lines are prefixed with '>' and context lines with '-'.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["regex"]["type"] = "string";
        schema["properties"]["regex"]["description"] = "Regex pattern to search for";
        schema["properties"]["path"]["type"] = "string";
        schema["properties"]["path"]["description"] = "File path to search in (required)";
        schema["properties"]["before"]["type"] = "integer";
        schema["properties"]["before"]["description"] = "Number of context lines before match (default: 0)";
        schema["properties"]["after"]["type"] = "integer";
        schema["properties"]["after"]["description"] = "Number of context lines after match (default: 0)";
        schema["required"] = {"regex", "path"};
        return schema.dump();
    }

    std::string execute(const std::string& json_args) override {
        try {
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                return "Error: Invalid JSON arguments - not json";
            }
            std::string regex_str = args.value("regex", "");
            std::string path      = args.value("path", "");
            int before = args.value("before", 0);
            int after  = args.value("after", 0);

            if (regex_str.empty()) return "Error: No regex pattern provided.";
            if (path.empty())      return "Error: No file path provided.";

            std::ifstream file(path);
            if (!file.is_open()) {
                return "Error: Cannot open file '" + path + "'";
            }

            // Read all lines
            std::vector<std::string> lines;
            std::string line;
            while (std::getline(file, line)) {
                lines.push_back(line);
            }
            file.close();

            std::regex re;
            try {
                re = std::regex(regex_str);
            } catch (const std::regex_error& e) {
                return "Invalid regex pattern: " + std::string(e.what());
            }

            // Find all matching line indices
            std::vector<int> matches;
            for (int i = 0; i < static_cast<int>(lines.size()); i++) {
                if (std::regex_search(lines[i], re)) {
                    matches.push_back(i);
                }
            }

            if (matches.empty()) {
                return "No matches found for pattern '" + regex_str + "' in '" + path + "'.";
            }

            // Build output with context, respecting max results
            std::ostringstream result;
            int match_count = 0;
            const int MAX_RESULTS = 50;

            for (int m : matches) {
                if (match_count >= MAX_RESULTS) break;

                int start_idx = std::max(0, m - before);
                int end_idx   = std::min(static_cast<int>(lines.size()), m + after + 1);

                for (int i = start_idx; i < end_idx; i++) {
                    if (i == m) {
                        result << "> " << path << ":" << (i + 1) << ": " << lines[i] << "\n";
                    } else {
                        result << "- " << path << ":" << (i + 1) << ": " << lines[i] << "\n";
                    }
                }

                match_count++;
            }

            return result.str();
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }
};

ToolPtr create_grep_with_context_tool() {
    return std::make_shared<GrepWithContextTool>();
}

// -----------------------------------------------------------------------
// RunBuildTool - compile and parse errors/warnings
// -----------------------------------------------------------------------
class RunBuildTool : public Tool {
public:
    std::string name() const override { return "run_build"; }
    std::string description() const override {
        return "Run a build command (g++, clang++, cmake, make, etc.) and parse compiler output. "
               "Extracts errors and warnings with file:line:message format. "
               "Returns structured results showing all compilation issues.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["command"]["type"] = "string";
        schema["properties"]["command"]["description"] = "The build command to execute (e.g. 'g++ main.cpp -o main')";
        schema["properties"]["cwd"]["type"] = "string";
        schema["properties"]["cwd"]["description"] = "Working directory (optional)";
        schema["required"] = {"command"};
        return schema.dump();
    }

    std::string execute(const std::string& json_args) override {
        try {
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                return "Error: Invalid JSON arguments - not json";
            }
            std::string command = args.value("command", "");
            std::string cwd     = args.value("cwd", "");

            if (command.empty()) return "Error: No build command provided.";

            // Execute the command and capture output
            std::string output = execute_shell_command(command, cwd);

            // Parse compiler errors/warnings from output
            auto [errors, warnings] = parse_compiler_output(output);

            std::ostringstream result;

            if (errors.empty() && warnings.empty()) {
                result << "Build succeeded.\n";
                if (!output.empty()) {
                    result << "Output:\n" << output;
                }
            } else {
                bool has_issues = false;

                if (!errors.empty()) {
                    has_issues = true;
                    result << "# Errors (" << errors.size() << ")\n";
                    for (const auto& err : errors) {
                        result << "  ERROR: " << err.file << ":" << err.line
                               << (err.column > 0 ? ":" + std::to_string(err.column) : "")
                               << ": " << err.message << "\n";
                    }
                }

                if (!warnings.empty()) {
                    has_issues = true;
                    result << "\n# Warnings (" << warnings.size() << ")\n";
                    for (const auto& warn : warnings) {
                        result << "  WARNING: " << warn.file << ":" << warn.line
                               << (warn.column > 0 ? ":" + std::to_string(warn.column) : "")
                               << ": " << warn.message << "\n";
                    }
                }

                if (!has_issues && !output.empty()) {
                    result << output;
                }
            }

            return result.str();
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }

private:
    struct CompilerIssue {
        std::string file;
        int line = 0;
        int column = 0;
        std::string message;
    };



    static std::pair<std::vector<CompilerIssue>, std::vector<CompilerIssue>> parse_compiler_output(const std::string& output) {
        std::vector<CompilerIssue> errors, warnings;

        // Split into lines
        std::istringstream stream(output);
        std::string line;
        while (std::getline(stream, line)) {
            CompilerIssue issue;
            bool is_error = false;

            // Try g++/clang++ format: file:line:col: error: message
            if (parse_gcc_style(line, issue)) {
                std::string lower_line = to_lower_copy(line);
                if (lower_line.find("error") != std::string::npos) {
                    errors.push_back(issue);
                } else if (lower_line.find("warning") != std::string::npos) {
                    warnings.push_back(issue);
                }
            }
            // Try MSVC format: file(line): error Cxxxx: message
            else if (parse_msvc_style(line, issue)) {
                std::string lower_line = to_lower_copy(line);
                if (lower_line.find("error") != std::string::npos) {
                    errors.push_back(issue);
                } else if (lower_line.find("warning") != std::string::npos) {
                    warnings.push_back(issue);
                }
            }
        }

        return {errors, warnings};
    }

    // Parse: file.cpp:123:45: error: message
    static bool parse_gcc_style(const std::string& line, CompilerIssue& issue) {
        try {
            std::regex re(R"((.+?):(\d+)(?::(\d+))?:\s*(error|warning):\s*(.*))");
            std::smatch match;
            if (std::regex_search(line, match, re) && match.size() >= 5) {
                issue.file = trim(match[1].str());
                issue.line = std::stoi(match[2].str());
                issue.column = match[3].matched ? std::stoi(match[3].str()) : 0;
                issue.message = trim(match[5].str());
                return true;
            }
        } catch (...) {}
        return false;
    }

    // Parse: file.cpp(123): error Cxxxx: message
    static bool parse_msvc_style(const std::string& line, CompilerIssue& issue) {
        try {
            std::regex re(R"((.+?)\((\d+)\):\s*(warning|error)\s*([Cc]?\w+)?[:\s]*(.*))");
            std::smatch match;
            if (std::regex_search(line, match, re) && match.size() >= 5) {
                issue.file = trim(match[1].str());
                issue.line = std::stoi(match[2].str());
                issue.column = 0;
                issue.message = trim(match[4].matched ? match[4].str() + ": " + match[5].str()
                                                      : match[5].str());
                return true;
            }
        } catch (...) {}
        return false;
    }

    static std::string to_lower_copy(const std::string& s) {
        std::string result = s;
        for (auto& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return result;
    }

    static std::string trim(const std::string& s) {
        size_t start = 0, end = s.size();
        while (start < end && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r')) start++;
        while (end > start && (s[end-1] == ' ' || s[end-1] == '\t' || s[end-1] == '\r')) end--;
        return s.substr(start, end - start);
    }
};

ToolPtr create_run_build_tool() {
    return std::make_shared<RunBuildTool>();
}

// -----------------------------------------------------------------------
// GitStatusTool - structured git status output
// -----------------------------------------------------------------------
class GitStatusTool : public Tool {
public:
    std::string name() const override { return "git_status"; }
    std::string description() const override {
        return "Get the git status of a repository in porcelain format. "
               "Returns structured file change statuses: M=modified, A=added, D=deleted, "
               "??=untracked, R=renamed, C=copied, U=unmerged.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["path"]["type"] = "string";
        schema["properties"]["path"]["description"] = "Repository path (default: current directory)";
        return schema.dump();
    }

    std::string execute(const std::string& json_args) override {
        try {
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                return "Error: Invalid JSON arguments - not json";
            }
            std::string path = args.value("path", ".");

            // Resolve relative paths to absolute paths so that cwd is always unambiguous.
            fs::path abs_path = fs::absolute(path);
            if (!fs::is_directory(abs_path)) {
                return "Error: Path '" + path + "' does not exist or is not a directory.";
            }

            // Check if it's a git repo first. Use cwd parameter to avoid nested-quote issues on Windows.
            std::string check_cmd = "git rev-parse --is-inside-work-tree 2>&1";
            std::string check_output = execute_shell_command(check_cmd, abs_path.string());
            if (check_output.find("true") == std::string::npos) {
                return "Error: '" + path + "' is not a git repository.";
            }

            // Use cwd parameter instead of -C to avoid nested-quote issues on Windows.
            std::string cmd = "git status --porcelain 2>&1";
            std::string output = execute_shell_command(cmd, abs_path.string());

            if (output.empty()) {
                return "Working tree clean. No changes in '" + path + "'.";
            }

            // Parse porcelain format: XY path (or XYSN old -> new for renames/copies)
            std::ostringstream result;
            result << "# Git Status for: " << abs_path.string() << "\n";

            int modified = 0, added = 0, deleted = 0, untracked = 0, renamed = 0, copied = 0, unmerged = 0, other = 0;

            std::istringstream stream(output);
            std::string line;
            while (std::getline(stream, line)) {
                if (line.size() < 3) continue;

                char index_status = line[0];   // Index state
                char work_status  = line[1];   // Working tree state

                // Classify by index status first (what will be committed),
                // fall back to working-tree status if index is clean.
                // This correctly handles compound states like "AM" (added+modified)
                // as "added", and "MD" (modified+deleted) as "modified".
                char primary_status = (index_status != ' ') ? index_status : work_status;

                switch (primary_status) {
                    case 'M': modified++; break;
                    case 'A': added++;    break;
                    case 'D': deleted++;  break;
                    case 'R': renamed++;  break;
                    case 'C': copied++;   break;
                    case 'U': unmerged++; break;
                    case '?': untracked++; break;
                    default: other++;     break;
                }

                result << "  " << line << "\n";
            }

            result << "\n# Summary: "
                   << modified << " modified, "
                   << added << " added, "
                   << deleted << " deleted, "
                   << untracked << " untracked, "
                   << renamed << " renamed, "
                   << copied << " copied, "
                   << unmerged << " unmerged";

            return result.str();
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }
};

// -----------------------------------------------------------------------
// GitDiffTool - structured git diff output
// -----------------------------------------------------------------------
class GitDiffTool : public Tool {
public:
    std::string name() const override { return "git_diff"; }
    std::string description() const override {
        return "Get the git diff of a repository. Returns unified diff format showing line-by-line changes. "
               "Use 'staged=true' to see staged changes instead.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["path"]["type"] = "string";
        schema["properties"]["path"]["description"] = "Repository path (default: current directory)";
        schema["properties"]["staged"]["type"] = "boolean";
        schema["properties"]["staged"]["description"] = "If true, show staged changes (--cached). Default: false.";
        return schema.dump();
    }

    std::string execute(const std::string& json_args) override {
        try {
            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                return "Error: Invalid JSON arguments - not json";
            }
            std::string path   = args.value("path", ".");
            bool staged        = args.value("staged", false);

            // Resolve relative paths to absolute paths so that cwd is always unambiguous.
            fs::path abs_path = fs::absolute(path);
            if (!fs::is_directory(abs_path)) {
                return "Error: Path '" + path + "' does not exist or is not a directory.";
            }

            // Check if it's a git repo. Use cwd parameter to avoid nested-quote issues on Windows.
            std::string check_cmd = "git rev-parse --is-inside-work-tree 2>&1";
            std::string check_output = execute_shell_command(check_cmd, abs_path.string());
            if (check_output.find("true") == std::string::npos) {
                return "Error: '" + path + "' is not a git repository.";
            }

            // Use cwd parameter instead of -C to avoid nested-quote issues on Windows.
            std::string cmd = "git diff";
            if (staged) cmd += " --cached";
            cmd += " 2>&1";

            std::string output = execute_shell_command(cmd, abs_path.string());

            if (output.empty()) {
                return staged ? "No staged changes in '" + path + "'."
                              : "No unstaged changes in '" + path + "'.";
            }

            std::ostringstream result;
            result << "# Git Diff" << (staged ? " (staged)" : "") << " for: " << abs_path.string() << "\n\n";
            result << output;

            return result.str();
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }
};

ToolPtr create_git_status_tool() {
    return std::make_shared<GitStatusTool>();
}

ToolPtr create_git_diff_tool() {
    return std::make_shared<GitDiffTool>();
}

// -----------------------------------------------------------------------
// FetchUrlTool - fetch web content and convert to Markdown
// -----------------------------------------------------------------------
class FetchUrlTool : public Tool {
public:
    std::string name() const override { return "fetch_url"; }
    std::string description() const override {
        return "Fetch a URL and convert its HTML content to Markdown. "
               "Useful for reading documentation, API references, or web pages. "
               "Timeout: 15 seconds.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["url"]["type"] = "string";
        schema["properties"]["url"]["description"] = "The URL to fetch (e.g. 'https://example.com')";
        schema["required"] = {"url"};
        return schema.dump();
    }

    std::string execute(const std::string& json_args) override {
        try {
            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                return "Error: Invalid JSON arguments - not json";
            }
            std::string url = args.value("url", "");

            if (url.empty()) return "Error: No URL provided.";

            // Parse host and path from URL
            size_t scheme_end = url.find("://");
            std::string host, path, protocol;
            int port = 443;

            if (scheme_end != std::string::npos) {
                protocol = url.substr(0, scheme_end);
                url = url.substr(scheme_end + 3);
            } else {
                protocol = "https";
            }

            size_t host_end = url.find('/');
            if (host_end != std::string::npos) {
                host = url.substr(0, host_end);
                path = "/" + url.substr(host_end);
            } else {
                host = url;
                path = "/";
            }

            // Check for port in host
            size_t port_pos = host.find(':');
            if (port_pos != std::string::npos) {
                try {
                    port = std::stoi(host.substr(port_pos + 1));
                } catch (...) {}
                host = host.substr(0, port_pos);
            }

            // Use httplib.h to fetch the URL
            bool is_https = (protocol == "https");
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            if (is_https) {
                httplib::SSLClient cli(host.c_str(), port);
                cli.set_connection_timeout(CONNECT_TIMEOUT, 0);
                cli.set_read_timeout(READ_TIMEOUT, 0);
                cli.set_write_timeout(WRITE_TIMEOUT, 0);
                auto res = cli.Get(path.c_str());
                if (!res || res->status != 200) {
                    return "Error: HTTP status " + std::to_string(res ? res->status : 0);
                }
                return html_to_markdown(truncate_body(res->body), url);
            }
#endif
            {
                // HTTP or fallback when OpenSSL not available
                std::string http_host = host;
                if (is_https) port = 443; else port = 80;

                httplib::Client cli(http_host.c_str(), port);
                cli.set_connection_timeout(CONNECT_TIMEOUT, 0);
                cli.set_read_timeout(READ_TIMEOUT, 0);
                cli.set_write_timeout(WRITE_TIMEOUT, 0);
                auto res = cli.Get(path.c_str());
                if (!res || res->status != 200) {
                    return "Error: HTTP status " + std::to_string(res ? res->status : 0) +
                           " (HTTPS requires OpenSSL support).";
                }
                return html_to_markdown(truncate_body(res->body), url);
            }
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        } catch (const std::exception& e) {
            return "Error fetching URL: " + std::string(e.what());
        }
    }

private:
    // Simple HTML-to-Markdown converter
    static std::string html_to_markdown(const std::string& html, const std::string& source_url) {
        std::ostringstream md;
        md << "# Source: " << source_url << "\n\n";

        std::string text = html;

        // 1. Extract title if present
        auto title_start = find_tag(text, "title");
        auto title_end   = find_closing_tag(text, "title", title_start);
        if (title_start != std::string::npos && title_end != std::string::npos) {
            std::string title = text.substr(title_start + 7, title_end - title_start - 7);
            md << "# " << strip_tags(title) << "\n\n";
        }

        // 2. Remove script and style blocks
        remove_blocks(text, "script");
        remove_blocks(text, "style");

        // 3. Convert headings
        for (int i = 1; i <= 6; i++) {
            std::string tag_open = "<h" + std::to_string(i) + ">";
            std::string tag_close = "</h" + std::to_string(i) + ">";
            convert_tags(text, tag_open, tag_close, i);
        }

        // 4. Convert links: <a href="url">text</a> -> [text](url)
        convert_links(text);

        // 5. Convert images: <img src="url" alt="text"> -> ![alt](src)
        convert_images(text);

        // 6. Convert bold/italic
        text = replace_all(text, "<strong>", "**");
        text = replace_all(text, "</strong>", "**");
        text = replace_all(text, "<b>", "**");
        text = replace_all(text, "</b>", "**");
        text = replace_all(text, "<em>", "*");
        text = replace_all(text, "</em>", "*");
        text = replace_all(text, "<i>", "*");
        text = replace_all(text, "</i>", "*");

        // 7. Convert lists
        convert_lists(text);

        // 8. Convert line breaks and paragraphs
        text = replace_all(text, "<br/>", "\n");
        text = replace_all(text, "<br />", "\n");
        text = replace_all(text, "<br>", "\n");
        text = replace_all(text, "</p>", "\n\n");

        // 9. Remove remaining HTML tags
        text = strip_tags(text);

        // 10. Clean up whitespace
        std::istringstream iss(text);
        std::string line;
        bool first = true;
        while (std::getline(iss, line)) {
            line = trim(line);
            if (!line.empty()) {
                if (!first) md << "\n";
                md << line;
                first = false;
            }
        }

        std::string result = md.str();
        // Limit output size for practicality
        if (result.size() > MAX_MARKDOWN_OUTPUT_SIZE) {
            result = result.substr(0, MAX_MARKDOWN_OUTPUT_SIZE);
            result += "\n\n... (truncated, content too large)";
        }

        return result;
    }

    static size_t find_tag(const std::string& text, const std::string& tag) {
        std::string search = "<" + tag;
        for (size_t i = 0; i < text.size(); i++) {
            if (text.compare(i, search.size(), search) == 0)
                return i;
        }
        return std::string::npos;
    }

    static size_t find_closing_tag(const std::string& text, const std::string& tag, size_t from) {
        if (from == std::string::npos) return std::string::npos;
        std::string search = "</" + tag + ">";
        auto pos = text.find(search, from);
        return pos != std::string::npos ? pos : std::string::npos;
    }

    static void remove_blocks(std::string& text, const std::string& tag) {
        std::string open = "<" + tag;
        std::string close = "</" + tag + ">";
        size_t start = 0;
        while ((start = find_opening_block(text, open, start)) != std::string::npos) {
            size_t end = text.find(close, start);
            if (end != std::string::npos) {
                text.erase(start, end + close.size() - start);
            } else {
                break;
            }
        }
    }

    static size_t find_opening_block(const std::string& text, const std::string& open_tag, size_t from) {
        for (size_t i = from; i < text.size(); i++) {
            if (text.compare(i, open_tag.size(), open_tag) == 0)
                return i;
        }
        return std::string::npos;
    }

    static void convert_tags(std::string& text, const std::string& open, const std::string& close, int level) {
        size_t start = 0;
        while ((start = text.find(open, start)) != std::string::npos) {
            size_t end = text.find(close, start);
            if (end != std::string::npos) {
                std::string content = strip_tags(text.substr(start + open.size(), end - start - open.size()));
                std::string prefix(level, '#');
                text.replace(start, end + close.size() - start, "\n" + prefix + " " + content + "\n");
                start += prefix.size() + content.size() + 3;
            } else {
                break;
            }
        }
    }

    static void convert_links(std::string& text) {
        // Simple: <a href="...">...</a>
        size_t pos = 0;
        while ((pos = text.find("<a ", pos)) != std::string::npos) {
            size_t href_start = text.find("href=\"", pos);
            if (href_start == std::string::npos || href_start > pos + 100) { pos++; continue; }
            href_start += 6;
            size_t href_end = text.find('\"', href_start);
            if (href_end == std::string::npos) break;

            std::string url = text.substr(href_start, href_end - href_start);

            // Find closing </a>
            size_t close_a = text.find("</a>", pos);
            if (close_a == std::string::npos) break;

            std::string link_text = strip_tags(text.substr(pos + 3, close_a - pos - 3));
            text.replace(pos, close_a + 4 - pos, "[" + link_text + "](" + url + ")");
            pos += link_text.size() + url.size() + 3;
        }
    }

    static void convert_images(std::string& text) {
        size_t pos = 0;
        while ((pos = text.find("<img", pos)) != std::string::npos) {
            // Find src
            size_t src_start = text.find("src=\"", pos);
            if (src_start == std::string::npos || src_start > pos + 200) { pos++; continue; }
            src_start += 5;
            size_t src_end = text.find('\"', src_start);
            if (src_end == std::string::npos) break;

            std::string src = text.substr(src_start, src_end - src_start);

            // Find alt
            std::string alt = "";
            size_t alt_start = text.find("alt=\"", pos);
            if (alt_start != std::string::npos && alt_start < src_end) {
                alt_start += 5;
                size_t alt_end = text.find('\"', alt_start);
                if (alt_end != std::string::npos)
                    alt = text.substr(alt_start, alt_end - alt_start);
            }

            // Find closing >
            size_t close_tag = text.find('>', pos);
            if (close_tag == std::string::npos) break;

            text.replace(pos, close_tag + 1 - pos, "![" + alt + "](" + src + ")");
            pos += alt.size() + src.size() + 4;
        }
    }

    static void convert_lists(std::string& text) {
        // <li>item</li> -> * item
        size_t pos = 0;
        while ((pos = text.find("<li>", pos)) != std::string::npos) {
            size_t end = text.find("</li>", pos);
            if (end == std::string::npos) break;
            std::string item = strip_tags(text.substr(pos + 4, end - pos - 4));
            text.replace(pos, end + 5 - pos, "\n* " + item);
            pos += item.size() + 3;
        }
    }

    static std::string strip_tags(const std::string& html) {
        std::ostringstream result;
        bool in_tag = false;
        for (char c : html) {
            if (c == '<') { in_tag = true; continue; }
            if (c == '>') { in_tag = false; continue; }
            if (!in_tag) result << c;
        }
        return result.str();
    }

private:
    static constexpr size_t MAX_BODY_SIZE = 100 * 1024;   // 5MB
    static constexpr size_t MAX_MARKDOWN_OUTPUT_SIZE = 50 * 1024;

    static std::string truncate_body(const std::string& body) {
        if (body.size() <= MAX_BODY_SIZE) return body;
        return body.substr(0, MAX_BODY_SIZE);
    }

public:
    static std::string& replace_all(std::string& str, const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = str.find(from, pos)) != std::string::npos) {
            str.replace(pos, from.size(), to);
            pos += to.size();
        }
        return str;
    }

    static std::string trim(const std::string& s) {
        size_t start = 0, end = s.size();
        while (start < end && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r')) start++;
        while (end > start && (s[end-1] == ' ' || s[end-1] == '\t' || s[end-1] == '\r')) end--;
        return s.substr(start, end - start);
    }
};

ToolPtr create_fetch_url_tool() {
    return std::make_shared<FetchUrlTool>();
}

} // namespace agent