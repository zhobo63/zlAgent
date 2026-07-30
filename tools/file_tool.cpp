#include "pch.h"

#include <cstring>
#include <iostream>

#include "tool.h"
#include "file_utils.h"
#include "tui.h"
#include "key_watcher.h"
#include "safety_guard.h"

const uint8_t bom[] = { 0xEF, 0xBB, 0xBF };

namespace fs = std::filesystem;
namespace agent {
using json = nlohmann::json;

static bool is_json_array(const json& obj, const char* key) {
    return obj.contains(key) && obj[key].is_array();
}

class ReadFileTool : public Tool {
public:
    std::string name() const override { return "read_file"; }
    std::string description() const override {
        return "Read the contents of a file. If start_line and end_line are provided, reads only that range; otherwise reads the entire file.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["path"]["type"] = "string";
        schema["properties"]["path"]["description"] = "The file path to read";
        schema["properties"]["start_line"]["type"] = "integer";
        schema["properties"]["start_line"]["description"] = "Optional: starting line number (1-based)";
        schema["properties"]["end_line"]["type"] = "integer";
        schema["properties"]["end_line"]["description"] = "Optional: ending line number (inclusive, 1-based)";
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
            int start_line = args.value("start_line", -1);
            int end_line   = args.value("end_line", -1);

            if (path.empty()) return "Error: No file path provided.";

            // Safety: integrated path check (working dir + whitelist + strict mode).
            auto check_result = SafetyGuard::get_instance().is_path_ok(path);
            if (check_result == PathCheckResult::Denied) {
                return "Error: Path '" + path + "' is outside allowed directories. Operation denied.";
            }

            // Read all lines once into memory.
            std::ifstream file(path);
            if (!file.is_open()) {
                return "Error: Cannot open file '" + path + "'";
            }
            std::vector<std::string> lines;
            std::string fline;
            while (std::getline(file, fline)) lines.push_back(fline);
            file.close();

            int line_count = static_cast<int>(lines.size());
            if (end_line < 0) { end_line = line_count; }

            // Line range mode: extract the requested lines.
            if (start_line >= 0 && end_line >= 0 && end_line >= start_line) {
                std::ostringstream oss;
                int width = static_cast<int>(std::to_string(line_count).size());
                for (int i = start_line - 1; i < end_line && i < line_count; ++i) {
                    oss << std::setw(width) << (i + 1) << " " << lines[i] << "\n";
                }
                return "# File for " + path + " (" + std::to_string(line_count) + ")\n" + oss.str();
            }

            // Large file: show outline instead of full content.
            const int OUTLINE_THRESHOLD = 200; // lines
            if (line_count > OUTLINE_THRESHOLD) {
                std::string outline = agent::GenerateFileOutline(path);
                if (!outline.empty()) {
                    return outline;
                }
            }

            // Small file: show full content with line numbers.
            int width = static_cast<int>(std::to_string(line_count).size());
            std::ostringstream oss;
            oss << "# File for " << path << " (" << line_count << ")\n";
            for (int i = 0; i < line_count; ++i) {
                oss << std::setw(width) << (i + 1) << " " << lines[i] << "\n";
            }
            return oss.str();
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }
};

// -----------------------------------------------------------------------
// ReadFilesTool - Batch read multiple files (paths or directory+glob)
// -----------------------------------------------------------------------
class ReadFilesTool : public Tool {
public:
    std::string name() const override { return "read_files"; }
    std::string description() const override {
        return
            "Read the contents of multiple files. Supports three input modes that can be combined in a single call:\n"
            "  (1) paths — string array\n"
            "      Requires top-level outline: true for file outlines (symbol names and line numbers), false for full content.\n"
            "  (2) files — object array with per-file options\n"
            "      Each file can have its own outline (bool, inherits from top-level if omitted; defaults to true), start_line/end_line for range. In outline mode, range may narrow the scope of symbols returned depending on language support.\n"
            "  (3) directory + glob\n"
            "      Both directory and glob must be provided together; requires top-level outline.\n"
            "*outline mode support C/C++, Python, JavaScript/TypeScript, Go, Rust, Java, Markdown\n";
    }
    std::string parameters_schema() const override {
        static std::string schema = json::parse(R"({
            "type": "object",
            "properties": {
                "paths": {
                    "type": "array",
                    "items": {"type": "string"},
                    "description": "List of file paths to read as a string array. E.g. [\"file1.cpp\", \"file2.h\"]."
                },
                "files": {
                    "type": "array",
                    "items": {"type": "object"},
                    "description": "List of file objects with per-file options. Each object: {\"path\": \"file.cpp\", \"outline\": false, \"start_line\": 1, \"end_line\": 100}."
                },
                "directory": {
                    "type": "string",
                    "description": "Directory to search in. Used with glob."
                },
                "glob": {
                    "type": "string",
                    "description": "File name pattern to match (default: '*'). Used with directory."
                },
                "outline": {
                    "type": "boolean",
                    "description": "Read file outlines (symbol names and line numbers) instead of full content."
                }
            },
            "required": ["outline"]
        })").dump();
        return schema;
    }

    void show_arguments(const std::string& json_args) override {
        try {
            auto args = json::parse(json_args);
            if (args.is_discarded()) return;

            // paths mode
            if (args.contains("paths") && args["paths"].is_array()) {
                int count = static_cast<int>(args["paths"].size());
                std::cout << "  paths: " << count << " file(s)\n";
                for (const auto& p : args["paths"]) {
                    if (p.is_string())
                        std::cout << "    - '" << p.get<std::string>() << "'\n";
                }
            }

            // files mode
            if (args.contains("files") && args["files"].is_array()) {
                int count = static_cast<int>(args["files"].size());
                std::cout << "  files: " << count << " file(s)\n";
                for (const auto& f : args["files"]) {
                    if (!f.is_object()) continue;
                    std::string path = f.value("path", "?");
                    bool outline = f.value("outline", false);
                    int start_line = f.value("start_line", 0);
                    int end_line   = f.value("end_line", 0);
                    if (start_line > 0 && end_line >= start_line) {
                        std::cout << "    - '" << path << "' lines " << start_line << "-" << end_line;
                        if (outline) std::cout << " [outline]";
                        std::cout << '\n';
                    } else {
                        std::cout << "    - '" << path << "'";
                        if (outline) std::cout << " [outline]";
                        std::cout << '\n';
                    }
                }
            }

            // directory + glob mode
            if (args.contains("directory") && args["directory"].is_string()) {
                std::string dir = args.value("directory", "");
                std::string glob = args.value("glob", "*");
                bool outline = args.value("outline", false);
                std::cout << "  directory: '" << dir << "'";
                if (glob != "*") std::cout << " glob: '" << glob << "'";
                if (outline) std::cout << " [outline]";
                std::cout << '\n';
            }

            // top-level outline flag (when not already shown)
            if (!args.contains("directory") && args.contains("outline") && args["outline"].is_boolean()) {
                bool outline = args.value("outline", false);
                if (outline) std::cout << "  outline: true\n";
            }
        } catch (...) {}
    }

    bool needs_user_reply(UserReplyMode mode) const override {
        return mode == UserReplyMode::Edit || mode == UserReplyMode::Always;
    }

private:
    // Process a single file: read content or outline, append formatted text to result
    void process_file(const std::string& path, bool want_outline,
                      int start_line, int end_line,
                      std::ostringstream& oss) {
        // Safety check
        auto check_result = SafetyGuard::get_instance().is_path_ok(path);
        if (check_result == PathCheckResult::Denied) {
            oss << "# Error: " << path << " - Path is outside allowed directories. Operation denied.";
            return;
        }

        // Outline mode
        if (want_outline) {
            int sl = start_line > 0 ? start_line : 0;
            int el = end_line >= start_line && start_line > 0 ? end_line : -1;
            std::string outline = agent::GenerateFileOutline(path, sl, el);
            if (!outline.empty()) {
                oss << outline;
            } else {
                oss << "# Error: " << path << " - Failed to generate outline";
            }
            return;
        }

        // Read all lines once into memory.
        std::ifstream file(path);
        if (!file.is_open()) {
            oss << "# Error: " << path << " - Cannot open file";
            return;
        }
        std::vector<std::string> lines;
        std::string fline;
        while (std::getline(file, fline)) lines.push_back(fline);
        file.close();

        int line_count = static_cast<int>(lines.size());

        // Line range mode (object mode only)
        if (start_line > 0 && end_line >= start_line) {
            std::ostringstream oss_range;
            int width = static_cast<int>(std::to_string(line_count).size());
            for (int i = start_line - 1; i < end_line && i < line_count; ++i) {
                oss_range << std::setw(width) << (i + 1) << " " << lines[i] << "\n";
            }
            if (!oss_range.str().empty()) {
                oss << "# File for " << path << " (" << line_count << ")\n";
                oss << oss_range.str();
            } else {
                oss << "# Error: " << path << " - Line range out of bounds or cannot read file";
            }
            return;
        }

        // Full content mode
        int width = static_cast<int>(std::to_string(line_count).size());
        oss << "# File for " << path << " (" << line_count << ")\n";
        for (int i = 0; i < line_count; ++i) {
            oss << std::setw(width) << (i + 1) << " " << lines[i] << "\n";
        }
    }

public:
    std::string execute(const std::string& json_args) override {
        try {
            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                return "Error: Invalid JSON arguments - not json";
            }

            // Build file list with per-file options
            struct FileTask { std::string path; bool outline; int start_line; int end_line; };
            std::vector<FileTask> tasks;

            // Determine top-level outline default
            bool has_outline = args.contains("outline");
            bool top_outline = has_outline ? args["outline"] : true;

            // Mode 1: string array paths — uses top-level outline
            if (args.contains("paths")) {
                auto paths = args["paths"];
                if (!has_outline) {
                    return "Error: 'outline' is required when using 'paths'.";
                }
                for (const auto& p : paths) {
                    tasks.push_back({p.get<std::string>(), top_outline, 0, 0});
                }
            }

            // Mode 2: object array — each file can have its own outline (defaults to top-level)
            if (args.contains("files")) {
                auto files = args["files"];
                for (const auto& obj : files) {
                    FileTask task;
                    task.path = obj.value("path", "");
                    task.outline = obj.value("outline", top_outline);
                    task.start_line = obj.value("start_line", 0);
                    task.end_line   = obj.value("end_line", 0);
                    tasks.push_back(task);
                }
            }

            // Mode 3: directory + glob — uses top-level outline
            if (args.contains("directory") && args.contains("glob")) {
                if (!has_outline) {
                    return "Error: 'outline' is required when using 'directory' and 'glob'.";
                }
                auto directory = args.value("directory", "");
                auto glob_pattern = args.value("glob", "*");

                for (const auto& entry : fs::recursive_directory_iterator(directory)) {
                    if (!entry.is_regular_file()) continue;
                    std::string filename = entry.path().filename().string();
                    if (match_glob(filename, glob_pattern)) {
                        tasks.push_back({entry.path().string(), top_outline, 0, 0});
                    }
                }
            }

            // At least one mode must be provided
            if (!args.contains("paths") && !args.contains("files") && !(args.contains("directory") && args.contains("glob"))) {
                return "Error: Must provide at least one of 'paths', 'files', or both 'directory' and 'glob'.";
            }

            if (tasks.empty()) {
                return "No files found matching the criteria.";
            }

            // Process each file into plain text output
            std::ostringstream oss;
            bool first = true;
            for (const auto& task : tasks) {
                if (!first) oss << "\n";
                first = false;
                process_file(task.path, task.outline, task.start_line, task.end_line, oss);
            }

            return oss.str();
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }
};

class WriteFileTool : public Tool {
public:
    std::string name() const override { return "write_file"; }
    std::string description() const override {
        return "Write content to a file. Creates the file or overwrites it.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["path"]["type"] = "string";
        schema["properties"]["path"]["description"] = "The file path to write";
        schema["properties"]["content"]["type"] = "string";
        schema["properties"]["content"]["description"] = "The content to write";
        schema["required"] = {"path", "content"};
        return schema.dump();
    }

    void show_arguments(const std::string& json_args) override {
        try {
            auto args = json::parse(json_args);
            if (args.is_discarded()) return;
            std::cout << "  path: '" << args.value("path", "") << "'\n";
            std::string content = args.value("content", "");
            // Show content line-by-line with line numbers
            int line_num = 1;
            for (const auto& ch : content) {
                if (ch == '\n') {
                    ++line_num;
                }
            }
            std::cout << "  content: " << line_num << " lines\n";
        } catch (...) {}
    }

    void show_preview(const std::string& json_args) override {
        try {
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                return;
            }
            std::string path = args.value("path", "");
            std::string new_content = args.value("content", "");
            if (!path.empty()) {
                std::ifstream existing(path);
                if (existing.is_open()) {
                    std::stringstream ss;
                    ss << existing.rdbuf();
                    std::string old_content = ss.str();
                    existing.close();
                    std::string diff = DiffEdit(old_content, new_content, 1);
                    std::cout << std::endl << path << std::endl << diff << std::endl;
                }
            }
        } catch (...) {}
    }

    bool needs_user_reply(UserReplyMode mode) const override {
        return mode == UserReplyMode::Edit || mode == UserReplyMode::Always;
    }

    std::string execute(const std::string& json_args) override {
        try {
            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                return "Error: Invalid JSON arguments - not json";
            }
            std::string path = args.value("path", "");
            std::string content = args.value("content", "");

            if (path.empty()) return "Error: No file path provided.";
            if (content.empty()) return "Error: No content provided. For large files, consider using edit_file to make targeted changes instead of write_file.";

            // Safety: integrated path check (working dir + whitelist + strict mode).
            auto check_result = SafetyGuard::get_instance().is_path_ok(path);
            if (check_result == PathCheckResult::Denied) {
                return "Error: Path '" + path + "' is outside allowed directories. Operation denied.";
            }

            // Ensure parent directory exists
            auto parent_dir = fs::path(path).parent_path();
            if (!parent_dir.empty()) {
                std::error_code ec;
                fs::create_directories(parent_dir, ec);
                if (ec) {
                    return "Error: Failed to create directory '" + parent_dir.string() + "': " + ec.message();
                }
            }

            // Capture old content if file exists
            std::string old_content;
            {
                std::ifstream existing(path);
                if (existing.is_open()) {
                    std::stringstream ss;
                    ss << existing.rdbuf();
                    old_content = ss.str();
                    existing.close();
                }
            }

            std::ofstream file(path, std::ios::trunc);
            if (!file.is_open()) {
                return "Error: Cannot create/open file '" + path + "'";
            }
            file << content;
            file.close();

            std::ostringstream oss;
            oss << "OK: " << path << " (" << content.size() << " bytes)\n";

            if (!old_content.empty()) {
                std::string edited = EditedLines(old_content, content, 1);
                if (!edited.empty()) {
                    oss << edited;
                }
            }
            return oss.str();
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what()) +
                   ". The response may have been truncated due to token limits. For large files, use edit_file for targeted changes instead.";
        }
    }
};


// -----------------------------------------------------------------------
// DeleteFilesTool - Batch delete multiple files (paths or directory+glob)
// -----------------------------------------------------------------------

static std::time_t to_time_t(fs::file_time_type ftime) {
    using namespace std::chrono;

    // Calculate the difference between the file time and the current file clock time,
    // then apply that duration offset to the current system clock time.
    auto system_time = time_point_cast<system_clock::duration>(
        ftime - fs::file_time_type::clock::now() + system_clock::now()
    );

    return system_clock::to_time_t(system_time);
}

class DeleteFilesTool : public Tool {
public:
    std::string name() const override { return "delete_files"; }
    std::string description() const override {
        return "Delete multiple files. Supports two modes: (1) specify paths list, or (2) directory + glob pattern.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["paths"]["type"] = "array";
        schema["properties"]["paths"]["items"]["type"] = "string";
        schema["properties"]["paths"]["description"] = "List of file paths to delete (mutually exclusive with directory+glob)";
        schema["properties"]["directory"]["type"] = "string";
        schema["properties"]["directory"]["description"] = "Directory to search in (default: current directory)";
        schema["properties"]["glob"]["type"] = "string";
        schema["properties"]["glob"]["description"] = "File pattern to match (mutually exclusive with paths, default: '*')";
        schema["properties"]["dry_run"]["type"] = "boolean";
        schema["properties"]["dry_run"]["description"] = "Preview mode: only show files that would be deleted without actually deleting them (default: false)";
        schema["properties"]["recursive"]["type"] = "boolean";
        schema["properties"]["recursive"]["description"] = "Whether to search subdirectories recursively (only valid in glob mode, default: true)";
        return schema.dump();
    }

    void show_preview(const std::string& json_args) override {
        try {
            auto args = json::parse(json_args);
            bool dry_run = args.value("dry_run", false);
            if (!dry_run) return; // Only preview in dry_run mode

            if (args.contains("paths")) {
                auto paths = args["paths"].get<std::vector<std::string>>();
                for (const auto& path : paths) {
                    int size_bytes = 0;
                    try {
                        std::ifstream file(path);
                        if (file.is_open()) {
                            file.seekg(0, std::ios::end);
                            size_bytes = static_cast<int>(file.tellg());
                            file.close();
                        }
                    } catch (...) {}
                    LOG_DEBUG("DeleteFilesTool", "preview path:" + path + " size:" + std::to_string(size_bytes) + "bytes");
                }
            } else if (args.contains("directory") && args.contains("glob")) {
                auto directory = args.value("directory", "");
                auto glob_pattern = args.value("glob", "*");
                bool recursive = args.value("recursive", true);

                for (const auto& entry : fs::recursive_directory_iterator(directory)) {
                    if (!entry.is_regular_file()) continue;
                    std::string ext = entry.path().extension().string();
                    if (ext == glob_pattern) {
                        int size_bytes = static_cast<int>(entry.file_size());
                        LOG_DEBUG("DeleteFilesTool", "preview path:" + entry.path().string() + " size:" + std::to_string(size_bytes) + "bytes");
                    }
                }
            }
        } catch (...) {}
    }

    bool needs_user_reply(UserReplyMode mode) const override {
        return mode == UserReplyMode::Edit || mode == UserReplyMode::Always;
    }

    std::string execute(const std::string& json_args) override {
        try {
            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                return "Error: Invalid JSON arguments - not json";
            }
            bool recursive = args.value("recursive", true);

            // Determine mode: paths or directory+glob
            std::vector<std::string> file_paths;

            if (args.contains("paths")) {
                auto paths = args["paths"].get<std::vector<std::string>>();
                for (const auto& path : paths) {
                    auto check_result = SafetyGuard::get_instance().is_path_ok(path);
                    if (check_result == PathCheckResult::Denied) {
                        return "Error: Path '" + path + "' is outside allowed directories. Operation denied.";
                    }
                    file_paths.push_back(path);
                }
            } else if (args.contains("directory") && args.contains("glob")) {
                auto directory = args.value("directory", "");
                auto glob_pattern = args.value("glob", "*");

                for (const auto& entry : fs::recursive_directory_iterator(directory)) {
                    if (!entry.is_regular_file()) continue;
                    std::string filename = entry.path().filename().string();
                    if (match_glob(filename, glob_pattern)) {
                        auto check_result = SafetyGuard::get_instance().is_path_ok(entry.path().string());
                        if (check_result != PathCheckResult::Denied) {
                            file_paths.push_back(entry.path().string());
                        }
                    }
                }
            } else {
                return "Error: Must provide either 'paths' or both 'directory' and 'glob'.";
            }

            if (file_paths.empty()) {
                return "No files found matching the criteria.";
            }

            std::string output;

            for (const auto& path : file_paths) {
                // Delete the file
                if (fs::exists(path)) {
                    auto ec = fs::remove(path);
                    if (!ec) {
                        output += "DELETED: " + path + "\n";
                    } else {
                        output += "FAIL: " + path + " - " + std::to_string(ec) + "\n";
                    }
                } else {
                    output += "FAIL: " + path + " - File not found\n";
                }
            }

            return output;
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }
};

// -----------------------------------------------------------------------
// EditFileTool - Line-level precise editing (find old_text, replace with new_text)
// -----------------------------------------------------------------------
class EditFileTool : public Tool {
public:
    std::string name() const override { return "edit_file"; }
    std::string description() const override {
        return R"(Apply precise edits to an existing file.
Provide an "edits" array with old_text and new_text for each edit operation.
All line numbers are based on the original file before any edits — DO NOT overlap operations on the same lines.)";
    }
    std::string parameters_schema() const override {
        static std::string schema = json::parse(R"({
            "type": "object",
            "properties": {
                "path": {"type": "string", "description": "The file path to edit"},
                "edits": {
                    "type": "array",
                    "description": "Array of edit operations. Each must have old_text and new_text.",
                    "items": {
                        "type": "object",
                        "properties": {
                            "old_text": {"type": "string", "description": "Exact text to find and replace. Must match exactly."},
                            "new_text": {"type": "string", "description": "The replacement text"},
                            "start_line": {"type": "integer", "description": "Optional: starting line number (1-based, inclusive) to limit search range for old_text. Defaults to 1."},
                            "end_line": {"type": "integer", "description": "Optional: ending line number (1-based, inclusive) to limit search range for old_text. Defaults to last line."}
                        },
                        "required": ["old_text", "new_text"]
                    }
                }
            },
            "required": ["path", "edits"]
        })").dump();
        return schema;
    }

    void show_preview(const std::string& json_args) override {
        try {
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                Tool::show_preview(json_args);
                return;
            }
            std::string path = args.value("path", "");
            if (!is_json_array(args, "edits"))
                return;

            agent::EditFile ef;
            if (!ef.read_file(path)) return;
            for (const auto& edit : args["edits"])
                apply_single_edit(ef, edit);
            std::string error;
            if (!ef.validate_blocks(error)) {
                std::cerr << "show_preview validation failed: " << path << " - " << error << '\n';
                return;
            }
            agent::EditLines result;
            ef.apply_blocks(result);
            std::string old_content = ef.to_string();
            std::string new_content = result.to_string();
            std::cout << "\n" << path << "\n" << DiffEdit(old_content, new_content, 1) << "\n";
        } catch (...) {}
    }

    bool needs_user_reply(UserReplyMode mode) const override {
        return mode == UserReplyMode::Edit || mode == UserReplyMode::Always;
    }

private:
    /// Apply a single edit to an EditFile: finds old_text in the original file,
    /// optionally limited by start_line/end_line, replaces it within the full line
    /// context, then adds a block with the resulting full-line content.
    /// The matching should be flexible with leading whitespace - tabs and spaces at the start of lines should be ignored when matching
    static void apply_single_edit(agent::EditFile& ef, const json& edit) {
        std::string old_text = edit.value("old_text", "");
        std::string new_text = edit.value("new_text", "");

        if (old_text.empty()) {
            ef.error_message = "old_text is required.";
            return;
        }

        int start_line = edit.value("start_line", 1);
        int end_line   = edit.value("end_line", static_cast<int>(ef.lines.size()));

        auto trim_leading_ws = [](const std::string& s) -> std::string {
            size_t p = s.find_first_not_of(" \t");
            return (p == std::string::npos) ? "" : s.substr(p);
        };

        EditLines old_el, new_el;
        old_el.parse(old_text);
        new_el.parse(new_text);

        // Build trimmed_old: each line of old_text with leading ws removed, joined by '\n'
        std::string trimmed_old;
        for (int j = 0; j < static_cast<int>(old_el.lines.size()); ++j) {
            if (j > 0) trimmed_old += "\n";
            trimmed_old += trim_leading_ws(old_el.lines[j]);
        }

        // Build trimmed_content: each line in [start_line, end_line] with leading ws removed,
        // joined by '\n'. Also record the original indent per line and the offset of each
        // line start within trimmed_content.
        std::vector<std::string> indents;       // indent of each source line
        std::vector<size_t> line_offsets;       // byte offset in trimmed_content where each line starts
        std::string trimmed_content;
        for (int i = start_line - 1; i < end_line; ++i) {
            if (!indents.empty()) trimmed_content += "\n";
            const std::string& orig = ef.lines[i];
            std::string trimmed = trim_leading_ws(orig);
            indents.push_back(orig.substr(0, orig.size() - trimmed.size()));
            line_offsets.push_back(trimmed_content.size());
            trimmed_content += trimmed;
        }

        // Find trimmed_old in trimmed_content
        size_t pos = trimmed_content.find(trimmed_old);
        if (pos == std::string::npos) {
            std::ostringstream oss;
            oss << "old_text not found in lines " << start_line << "-" << end_line << ".";
            ef.error_message = oss.str();
            return;
        }

        // Map a byte position within trimmed_content to the 1-based source line number.
        // Uses our recorded offsets — safe even when lines contain literal '\n' characters.
        auto pos_to_line = [&](size_t p) -> int {
            for (int i = 0; i < static_cast<int>(line_offsets.size()); ++i) {
                size_t next_offset = (i + 1 < static_cast<int>(line_offsets.size()))
                    ? line_offsets[i + 1]
                    : trimmed_content.size();
                if (p >= line_offsets[i] && p < next_offset)
                    return start_line + i;
            }
            return start_line + static_cast<int>(line_offsets.size()) - 1;
        };

        int match_start = pos_to_line(pos);                          // 1-based
        int match_end   = pos_to_line(pos + trimmed_old.size() - 1); // 1-based inclusive

        if (match_start == match_end) {
            // Single-line match — check if it's a partial inline replacement
            int line_idx = match_start - start_line; // 0-based into indents
            size_t line_start_in_tc = line_offsets[line_idx];
            size_t line_end_in_tc   = (line_idx + 1 < static_cast<int>(line_offsets.size()))
                ? line_offsets[line_idx + 1]
                : trimmed_content.size();

            bool is_partial = (pos != line_start_in_tc ||
                               pos + trimmed_old.size() < line_end_in_tc);

            if (is_partial) {
                // Inline replacement: replace only the matched portion within the original line
                const std::string& orig_line = ef.lines[match_start - 1];
                size_t indent_len = indents[line_idx].size();
                size_t match_pos_in_trimmed = pos - line_start_in_tc;
                size_t orig_match_start = indent_len + match_pos_in_trimmed;

                std::string new_line = orig_line.substr(0, orig_match_start) +
                                       new_text +
                                       orig_line.substr(orig_match_start + trimmed_old.size());

                ef.replace_line_range(match_start, match_end, new_line);
            } else {
                // Full-line replacement: if new_text has its own indent, use it; otherwise inherit source indent
                const std::string& new_line = new_el.lines[0];
                bool has_own_indent = !new_line.empty() && (new_line.front() == ' ' || new_line.front() == '\t');
                std::string replacement = has_own_indent ? new_line : indents[line_idx] + new_line;
                ef.replace_line_range(match_start, match_end, replacement);
            }
        } else {
            // Multi-line replacement: each line of new_text uses its own indent if present,
            // otherwise inherits the indent from the corresponding matched source line.
            std::string replacement;
            int n_new = static_cast<int>(new_el.lines.size());

            for (int j = 0; j < n_new; ++j) {
                if (j > 0) replacement += "\n";
                const std::string& new_line = new_el.lines[j];
                bool has_own_indent = !new_line.empty() && (new_line.front() == ' ' || new_line.front() == '\t');
                int src_idx = match_start - 1 + j;  // 0-based into indents
                const std::string& indent = (src_idx < static_cast<int>(indents.size())) ? indents[src_idx] : "";
                replacement += has_own_indent ? new_line : indent + new_line;
            }

            // Preserve trailing content on the last matched source line if old_text ended mid-line
            int last_line_idx = match_end - start_line;  // index into indents/line_offsets
            size_t last_line_start_in_tc = line_offsets[last_line_idx];
            size_t last_line_end_in_tc   = (last_line_idx + 1 < static_cast<int>(line_offsets.size()))
                ? line_offsets[last_line_idx + 1]
                : trimmed_content.size();

            if (pos + trimmed_old.size() < last_line_end_in_tc) {
                // old_text ended mid-line; preserve the rest of that original line
                size_t remaining_offset = pos + trimmed_old.size() - last_line_start_in_tc;
                const std::string& orig_last = ef.lines[match_end - 1];  // 0-based into ef.lines
                size_t indent_len = indents[last_line_idx].size();
                replacement += orig_last.substr(indent_len + remaining_offset);
            }

            ef.replace_line_range(match_start, match_end, replacement);
        }

    }

public:
    std::string execute(const std::string& json_args) override {
        try {
            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                return "Error: Invalid JSON arguments - not json";
            }
            std::string path = args.value("path", "");

            if (path.empty()) return "Error: No file path provided.";

            // Safety: integrated path check.
            auto check_result = SafetyGuard::get_instance().is_path_ok(path);
            if (check_result == PathCheckResult::Denied) {
                return "Error: Path '" + path + "' is outside allowed directories. Operation denied.";
            }

            if (!is_json_array(args, "edits"))
                return "Error: No edits";

            agent::EditFile ef;
            if (!ef.read_file(path)) {
                return "Error: Cannot open file '" + path + "'";
            }

            for (const auto& edit : args["edits"])
                apply_single_edit(ef, edit);

            // Check for errors from text-based resolution.
            if (!ef.error_message.empty()) {
                return "Error: " + ef.error_message;
            }

            std::string error;
            if (!ef.validate_blocks(error)) {
                return "Error: " + error;
            }

            // Capture old content for diff.
            std::string old_content = ef.to_string();

            agent::EditLines result;
            ef.apply_blocks(result);

            if (!result.write_file(path)) {
                return "Error: Cannot write to file '" + path + "'";
            }

            // Build output.
            int original_lines = static_cast<int>(ef.lines.size());
            int new_lines      = static_cast<int>(result.lines.size());
            std::ostringstream oss;
            oss << "edited: " << path << " (" << original_lines << " -> " << new_lines << " lines)\n";

            std::string edited = EditedLines(old_content, result.to_string(), 1);
            if (!edited.empty()) {
                oss << edited;
            }
            return oss.str();
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }
};

// -----------------------------------------------------------------------
// EditFilesTool - Batch edit multiple files with block-based approach
// -----------------------------------------------------------------------
class EditFilesTool : public Tool {
public:
    std::string name() const override { return "edit_files"; }
    std::string description() const override {
        return R"(Edit one or more files in a single call.
All line numbers are based on the original file before any edits — DO NOT overlap operations on the same lines.
Operations per file are atomic (failure rolls back that file); files are independent.)";
    }
    std::string parameters_schema() const override {
        static std::string schema = json::parse(R"({
            "type": "object",
            "properties": {
                "replace_line_range": {
                    "type": "array",
                    "description": "Replace a range of lines with new text. All line numbers are based on the original file.",
                    "items": {
                        "type": "object",
                        "required": ["path", "start_line", "end_line", "new_text"],
                        "properties": {
                            "path": {"type": "string", "description": "File path to edit (relative to project root) "},
                            "start_line": {"type": "integer", "description": "Starting line number (1-based, inclusive) "},
                            "end_line": {"type": "integer", "description": "Ending line number (1-based, inclusive) "},
                            "new_text": {"type": "string", "description": "Replacement text. Empty string deletes the range."}
                        }
                    }
                },
                "insert_before_line": {
                    "type": "array",
                    "description": "Insert new text before the specified line.",
                    "items": {
                        "type": "object",
                        "required": ["path", "start_line", "new_text"],
                        "properties": {
                            "path": {"type": "string", "description": "File path to edit (relative to project root) "},
                            "start_line": {"type": "integer", "description": "Line number to insert before (1-based). 1 means insert at the very beginning."},
                            "new_text": {"type": "string", "description": "Text to insert"}
                        }
                    }
                },
                "insert_after_line": {
                    "type": "array",
                    "description": "Insert new text after the specified line.",
                    "items": {
                        "type": "object",
                        "required": ["path", "start_line", "new_text"],
                        "properties": {
                            "path": {"type": "string", "description": "File path to edit (relative to project root) "},
                            "start_line": {"type": "integer", "description": "Line number to insert after (1-based)."},
                            "new_text": {"type": "string", "description": "Text to insert"}
                        }
                    }
                },
                "delete_lines": {
                    "type": "array",
                    "description": "Delete a range of lines.",
                    "items": {
                        "type": "object",
                        "required": ["path", "start_line", "end_line"],
                        "properties": {
                            "path": {"type": "string", "description": "File path to edit (relative to project root) "},
                            "start_line": {"type": "integer", "description": "Starting line number (1-based, inclusive) "},
                            "end_line": {"type": "integer", "description": "Ending line number (1-based, inclusive) "}
                        }
                    }
                }
            }
        })").dump();
        return schema;
    }

    void show_arguments(const std::string& json_args) override {
        Tool::show_arguments(json_args);
        try {
            auto args = json::parse(json_args);
            if (args.is_discarded()) return;

            // Collect all operations grouped by path
            std::map<std::string, int> file_op_count;

            auto show_ops = [&](const char* type_name, const json& arr) {
                for (const auto& op : arr) {
                    std::string path = op.value("path", "");
                    if (path.empty()) continue;
                    file_op_count[path]++;

                    if (strcmp(type_name, "replace_line_range") == 0) {
                        int start = op.value("start_line", 0);
                        int end   = op.value("end_line", 0);
                        auto new_text = op.value("new_text", "");
                        std::cout << "# replace_line_range: '" << path << "' lines " << start << "-" << end << '\n';
                        std::cout << TUI::ANSI_BRIGHT_BLACK << new_text << TUI::ANSI_RESET << "\n";
                    } else if (strcmp(type_name, "insert_before_line") == 0) {
                        int line_num = op.value("start_line", 0);
                        auto new_text = op.value("new_text", "");
                        std::cout << "# insert_before_line: '" << path << "' before line " << line_num << '\n';
                        std::cout << TUI::ANSI_BRIGHT_BLACK << new_text << TUI::ANSI_RESET << "\n";
                    } else if (strcmp(type_name, "insert_after_line") == 0) {
                        int line_num = op.value("start_line", 0);
                        auto new_text = op.value("new_text", "");
                        std::cout << "# insert_after_line: '" << path << "' after line " << line_num << '\n';
                        std::cout << TUI::ANSI_BRIGHT_BLACK << new_text << TUI::ANSI_RESET << "\n";
                    } else if (strcmp(type_name, "delete_lines") == 0) {
                        int start = op.value("start_line", 0);
                        int end   = op.value("end_line", 0);
                        std::cout << "# delete_lines: '" << path << "' lines " << start << "-" << end << '\n';
                    }
                }
            };

            if (is_json_array(args, "replace_line_range"))
                show_ops("replace_line_range", args["replace_line_range"]);
            if (is_json_array(args, "insert_before_line"))
                show_ops("insert_before_line", args["insert_before_line"]);
            if (is_json_array(args, "insert_after_line"))
                show_ops("insert_after_line", args["insert_after_line"]);
            if (is_json_array(args, "delete_lines"))
                show_ops("delete_lines", args["delete_lines"]);

            std::cout << "  files affected: " << file_op_count.size() << '\n';
        } catch (const std::exception& e) {
            std::cerr << "show_arguments error: " << e.what() << '\n';
        } catch (...) {
            std::cerr << "unknown error in show_arguments\n";
        }
    }

    void show_preview(const std::string& json_args) override {
        LOG_INFO(u8"🛠️Tool", name() + " [preview]");
        try {
            auto args = json::parse(json_args);
            if (args.is_discarded()) return;

            std::map<std::string, agent::EditFile> files;
            parse_operations(args, files);

            for (auto& [path, ef] : files) {
                std::string old_content = ef.to_string();

                std::string error;
                if (!ef.validate_blocks(error)) {
                    std::cerr << "show_preview validation failed: " << path << " - " << error << '\n';
                    continue;
                }

                EditLines result;
                ef.apply_blocks(result);

                std::string new_content = result.to_string();
                std::cout << "\n" << path << "\n" << DiffEdit(old_content, new_content, 1) << "\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "show_preview error: " << e.what() << '\n';
        } catch (...) {
            std::cerr << "unknown error in show_preview\n";
        }
    }

    bool needs_user_reply(UserReplyMode mode) const override {
        return mode == UserReplyMode::Edit || mode == UserReplyMode::Always;
    }

private:
    /// Parse flat JSON args and populate files map with EditFile objects.
    /// Each EditFile is read from disk and populated with blocks.
    static void parse_operations(const json& args,
                                 std::map<std::string, agent::EditFile>& files) {
        auto get_ef = [&](const std::string& path) -> agent::EditFile* {
            if (files.find(path) == files.end()) {
                files[path].read_file(path);
            }
            return &files[path];
        };

        // Line-based operations: directly add blocks
        auto process_replace_line_range = [&](const json& op) {
            std::string path = op.value("path", "");
            if (path.empty()) return;
            int start = op.value("start_line", 0);
            int end   = op.value("end_line", 0);
            std::string new_text = op.value("new_text", "");
            get_ef(path)->replace_line_range(start, end, new_text);
        };

        auto process_insert_before_line = [&](const json& op) {
            std::string path = op.value("path", "");
            if (path.empty()) return;
            int start_line = op.value("start_line", 0);
            std::string new_text = op.value("new_text", "");
            get_ef(path)->insert_before_line(start_line, new_text);
        };

        auto process_insert_after_line = [&](const json& op) {
            std::string path = op.value("path", "");
            if (path.empty()) return;
            int start_line = op.value("start_line", 0);
            std::string new_text = op.value("new_text", "");
            get_ef(path)->insert_after_line(start_line, new_text);
        };

        auto process_delete_lines = [&](const json& op) {
            std::string path = op.value("path", "");
            if (path.empty()) return;
            int start = op.value("start_line", 0);
            int end   = op.value("end_line", 0);
            get_ef(path)->delete_lines(start, end);
        };

        if (is_json_array(args, "replace_line_range"))
            for (const auto& op : args["replace_line_range"]) process_replace_line_range(op);
        if (is_json_array(args, "insert_before_line"))
            for (const auto& op : args["insert_before_line"]) process_insert_before_line(op);
        if (is_json_array(args, "insert_after_line"))
            for (const auto& op : args["insert_after_line"]) process_insert_after_line(op);
        if (is_json_array(args, "delete_lines"))
            for (const auto& op : args["delete_lines"]) process_delete_lines(op);
    }

public:
    std::string execute(const std::string& json_args) override {
        try {
            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                return "Error: Invalid JSON arguments - not json";
            }

            std::map<std::string, agent::EditFile> files;
            parse_operations(args, files);

            std::string output;

            for (auto& [path, ef] : files) {
                auto check_result = SafetyGuard::get_instance().is_path_ok(path);
                if (check_result == PathCheckResult::Denied) {
                    output += "failed: " + path + " - Path '" + path + "' is outside allowed directories. Operation denied.\n";
                    continue;
                }

                if (!ef.error_message.empty()) {
                    output += "failed: " + path + " - " + ef.error_message + "\n";
                    continue;
                }

                int original_lines = static_cast<int>(ef.lines.size());
                std::string error;

                if (!ef.validate_blocks(error)) {
                    output += "failed: " + path + " - " + error + "\n";
                    continue;
                }

                // Capture old content before apply_blocks modifies ef.lines
                std::string old_content = ef.to_string();

                EditLines result;
                ef.apply_blocks(result);

                if (!result.write_file(path)) {
                    output += "failed: " + path + " - Cannot write to file '" + path + "'.\n";
                    continue;
                }

                std::string new_content = result.to_string();
                int new_lines = static_cast<int>(result.lines.size());
                output += "edited: " + path + " (" + std::to_string(original_lines) + " -> " + std::to_string(new_lines) + " lines)\n";

                std::string edited = EditedLines(old_content, new_content, 1);
                if (!edited.empty()) {
                    output += edited;
                }
            }

            return output;
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }
};

// -----------------------------------------------------------------------
// ListDirectoryTool - Browse directory contents
// -----------------------------------------------------------------------
class ListDirectoryTool : public Tool {
public:
    std::string name() const override { return "list_directory"; }
    std::string description() const override {
        return "List files and directories in the given path. Returns a structured view showing folders (with trailing /) and files.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["path"]["type"] = "string";
        schema["properties"]["path"]["description"] = "The directory path to list";
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
            if (path.empty()) {
                return "Error: No directory path provided.";
            }

            if (!path.empty() && path.back() != '/' && path.back() != '\\')
                path += '/';

            if (!fs::exists(path)) {
                return "Error: Directory '" + path + "' does not exist.";
            }

            std::string folders, files;
            int folder_count = 0, file_count = 0;

            for (const auto& entry : fs::directory_iterator(path)) {
                std::string name = entry.path().filename().string();
                if (name == "." || name == "..") continue;

                if (entry.is_directory()) {
                    folders += "  " + name + "/\n";
                    folder_count++;
                } else {
                    files += "  " + name + "\n";
                    file_count++;
                }
            }

            std::stringstream result;
            result << "# Folders:\n";
            if (folder_count > 0) result << folders;
            result << "\n# Files:\n";
            if (file_count > 0) result << files;
            result << "\n(" << folder_count << " directories, " << file_count << " files)";

            return result.str();
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }
};

// -----------------------------------------------------------------------
// AppendFileTool - Append content to a file without overwriting
// -----------------------------------------------------------------------
class AppendFileTool : public Tool {
public:
    std::string name() const override { return "append_file"; }
    std::string description() const override {
        return "Append content to the end of a file. Creates the file if it doesn't exist.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["path"]["type"] = "string";
        schema["properties"]["path"]["description"] = "The file path to append to";
        schema["properties"]["content"]["type"] = "string";
        schema["properties"]["content"]["description"] = "The content to append";
        schema["required"] = {"path", "content"};
        return schema.dump();
    }
    void show_arguments(const std::string& json_args) override {
        try {
            auto args = json::parse(json_args);
            if (args.is_discarded()) return;
            std::cout << "  path: '" << args.value("path", "") << "'\n";
            std::string content = args.value("content", "");
            int line_num = 1;
            for (const auto& ch : content) {
                if (ch == '\n') ++line_num;
            }
            std::cout << "  content: " << line_num << " lines\n";
        } catch (...) {}
    }

    void show_preview(const std::string& json_args) override {
        try {
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                Tool::show_preview(json_args);
                return;
            }
            std::string path = args.value("path", "");
            std::string content = args.value("content", "");
            if (!path.empty()) {
                std::ifstream existing(path);
                if (existing.is_open()) {
                    std::stringstream ss;
                    ss << existing.rdbuf();
                    std::string old_content = ss.str();
                    existing.close();
                    std::string new_content = old_content + content;
                    std::string diff = DiffEdit(old_content, new_content, 1);
                    std::cout << std::endl << path << std::endl << diff << std::endl;
                }
            }
        } catch (...) {}
    }

    bool needs_user_reply(UserReplyMode mode) const override {
        return mode == UserReplyMode::Edit || mode == UserReplyMode::Always;
    }

    std::string execute(const std::string& json_args) override {
        try {
            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                return "Error: Invalid JSON arguments - not json";
            }
            std::string path = args.value("path", "");
            std::string content = args.value("content", "");

            if (path.empty()) return "Error: No file path provided.";
            if (content.empty()) return "Error: No content provided.";

            // Safety: integrated path check (working dir + whitelist + strict mode).
            auto check_result = SafetyGuard::get_instance().is_path_ok(path);
            if (check_result == PathCheckResult::Denied) {
                return "Error: Path '" + path + "' is outside allowed directories. Operation denied.";
            }

            std::ofstream file(path, std::ios::app);
            if (!file.is_open()) {
                return "Error: Cannot open file '" + path + "' for appending";
            }
            file << content;
            file.close();

            return "Successfully appended " + std::to_string(content.size()) +
                   " bytes to '" + path + "'";
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }
};

// -----------------------------------------------------------------------
// InsertFileContentTool - Insert content at a specific line in a file
// -----------------------------------------------------------------------
class InsertFileContentTool : public Tool {
public:
    std::string name() const override { return "insert_file_content"; }
    std::string description() const override {
        return "Insert content before the specified line number in a file. The new content will be placed on the given line, pushing existing lines down.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["path"]["type"] = "string";
        schema["properties"]["path"]["description"] = "The file path to insert into";
        schema["properties"]["line_number"]["type"] = "integer";
        schema["properties"]["line_number"]["description"] = "Line number to insert before (1-based). Existing content at this line will be pushed down.";
        schema["properties"]["content"]["type"] = "string";
        schema["properties"]["content"]["description"] = "The content to insert";
        schema["required"] = {"path", "line_number", "content"};
        return schema.dump();
    }
    void show_arguments(const std::string& json_args) override {
        try {
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                Tool::show_arguments(json_args);
                return;
            }
            std::cout << "  path: '" << args.value("path", "") << "'\n";
            int line_number = args.value("line_number", 0);
            std::cout << "  line_number: " << line_number << '\n';
            std::string content = args.value("content", "");
            int line_count = 1;
            for (const auto& ch : content) {
                if (ch == '\n') ++line_count;
            }
            std::cout << "  content: " << line_count << " lines\n";
        } catch (...) {}
    }

    void show_preview(const std::string& json_args) override {
        try {
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                Tool::show_preview(json_args);
                return;
            }
            std::string path = args.value("path", "");
            int line_number = args.value("line_number", 0);
            std::string content = args.value("content", "");
            LOG_DEBUG("InsertFileContentTool", "preview path:" + path + " line:" + std::to_string(line_number) + " content:" + std::to_string(content.length()) + "bytes");
            if (!path.empty()) {
                std::ifstream existing(path);
                if (existing.is_open()) {
                    std::stringstream ss;
                    ss << existing.rdbuf();
                    std::string old_content = ss.str();
                    existing.close();

                    // Simulate the insertion to show diff
                    std::vector<std::string> lines;
                    std::string line;
                    std::istringstream iss(old_content);
                    while (std::getline(iss, line)) {
                        lines.push_back(line);
                    }

                    if (line_number <= static_cast<int>(lines.size()) + 1) {
                        std::vector<std::string> insert_lines;
                        std::istringstream ciss(content);
                        while (std::getline(ciss, line)) {
                            insert_lines.push_back(line);
                        }
                        if (!content.empty() && content.back() == '\n' && !insert_lines.empty()) {
                            insert_lines.pop_back();
                        }

                        int insert_pos = line_number - 1;
                        for (int i = static_cast<int>(insert_lines.size()) - 1; i >= 0; --i) {
                            lines.insert(lines.begin() + insert_pos, insert_lines[i]);
                        }

                        std::string new_content;
                        for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
                            new_content += lines[i];
                            if (i < static_cast<int>(lines.size()) - 1)
                                new_content += '\n';
                        }

                        std::string diff = DiffEdit(old_content, new_content);
                        std::cout << std::endl << path << " (insert at line " << line_number << ")" << std::endl << diff << std::endl;
                    }
                }
            }
        } catch (...) {}
    }

    bool needs_user_reply(UserReplyMode mode) const override {
        return mode == UserReplyMode::Edit || mode == UserReplyMode::Always;
    }

    std::string execute(const std::string& json_args) override {
        try {
            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                return "Error: Invalid JSON arguments - not json";
            }
            std::string path = args.value("path", "");
            int line_number = args.value("line_number", 0);
            std::string content = args.value("content", "");

            if (path.empty()) return "Error: No file path provided.";
            if (line_number < 1) return "Error: line_number must be >= 1.";
            if (content.empty()) return "Error: No content provided.";

            // Safety: integrated path check (working dir + whitelist + strict mode).
            auto check_result = SafetyGuard::get_instance().is_path_ok(path);
            if (check_result == PathCheckResult::Denied) {
                return "Error: Path '" + path + "' is outside allowed directories. Operation denied.";
            }

            // Read the file
            std::vector<std::string> lines;
            if (!agent::read_file_lines(path, lines)) {
                return "Error: Cannot open file '" + path + "'";
            }

            // Check if line_number is within bounds or at the end
            if (line_number > static_cast<int>(lines.size()) + 1) {
                return "Error: line_number " + std::to_string(line_number) +
                       " exceeds file length (" + std::to_string(lines.size()) + " lines).";
            }

            // Split content into lines for insertion
            agent::EditLines el;
            el.parse(content);
            std::vector<std::string> insert_lines = std::move(el.lines);

            // Insert before the given line (0-based index = line_number - 1)
            int insert_pos = line_number - 1;
            for (int i = static_cast<int>(insert_lines.size()) - 1; i >= 0; --i) {
                lines.insert(lines.begin() + insert_pos, insert_lines[i]);
            }

            LOG_DEBUG("InsertFileContentTool", "execute path:" + path + " line:" + std::to_string(line_number) + " content:" + std::to_string(content.length()) + "bytes");
            // Write back
            if (!agent::write_file_lines(path, lines)) {
                return "Error: Cannot write to file '" + path + "'";
            }

            return "Successfully inserted content before line " + std::to_string(line_number) +
                   " in '" + path + "'";
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }
};

ToolPtr create_read_file_tool() {
    return std::make_shared<ReadFileTool>();
}

ToolPtr create_write_file_tool() {
    return std::make_shared<WriteFileTool>();
}

ToolPtr create_append_file_tool() {
    return std::make_shared<AppendFileTool>();
}

ToolPtr create_insert_file_content_tool() {
    return std::make_shared<InsertFileContentTool>();
}

ToolPtr create_edit_file_tool() {
    return std::make_shared<EditFileTool>();
}

ToolPtr create_edit_files_tool() {
    return std::make_shared<EditFilesTool>();
}

// -----------------------------------------------------------------------
// ReadFileLinesTool - Read a specific line range from a file
// -----------------------------------------------------------------------
//class ReadFileLinesTool : public Tool {
//public:
//    std::string name() const override { return "read_file_lines"; }
//    std::string description() const override {
//        return "Read a specific line range from a file. More efficient than read_file for large files when you only need certain lines.";
//    }
//    std::string parameters_schema() const override {
//        json schema;
//        schema["type"] = "object";
//        schema["properties"]["path"]["type"] = "string";
//        schema["properties"]["path"]["description"] = "The file path to read";
//        schema["properties"]["start_line"]["type"] = "integer";
//        schema["properties"]["start_line"]["description"] = "Starting line number (1-based)";
//        schema["properties"]["end_line"]["type"] = "integer";
//        schema["properties"]["end_line"]["description"] = "Ending line number (inclusive, 1-based)";
//        schema["required"] = {"path", "start_line", "end_line"};
//        return schema.dump();
//    }
//
//    std::string execute(const std::string& json_args) override {
//        try {
//            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
//            auto args = json::parse(json_args);
//            std::string path = args.value("path", "");
//            int start_line = args.value("start_line", 0);
//            int end_line   = args.value("end_line", 0);
//
//            if (path.empty()) return "Error: No file path provided.";
//            if (start_line <= 0) return "Error: start_line must be >= 1.";
//            if (end_line < start_line) return "Error: end_line must be >= start_line.";
//
//            // Safety: integrated path check (working dir + whitelist + strict mode).
//            auto check_result = SafetyGuard::get_instance().is_path_ok(path);
//            if (check_result == PathCheckResult::Denied) {
//                return "Error: Path '" + path + "' is outside allowed directories. Operation denied.";
//            }
//
//            std::string result = agent::ReadFileLinesAsString(path, start_line, end_line);
//            if (result.empty()) {
//                return "Error: Cannot read file '" + path + "' or line range is out of bounds.";
//            }
//            return result;
//        } catch (const json::parse_error& e) {
//            return "Error: Invalid JSON arguments - " + std::string(e.what());
//        }
//    }
//};

ToolPtr create_list_directory_tool() {
    return std::make_shared<ListDirectoryTool>();
}

//ToolPtr create_read_file_lines_tool() {
//    return std::make_shared<ReadFileLinesTool>();
//}

// -----------------------------------------------------------------------
// WriteFilesTool - batch write multiple files
// -----------------------------------------------------------------------
class WriteFilesTool : public Tool {
public:
    std::string name() const override { return "write_files"; }
    std::string description() const override {
        return "Write content to one or more files. Creates the files or overwrites them. Supports text and base64-encoded binary content.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["files"]["type"] = "array";
        schema["properties"]["files"]["items"]["type"] = "object";
        schema["properties"]["files"]["items"]["properties"]["path"]["type"] = "string";
        schema["properties"]["files"]["items"]["properties"]["path"]["description"] = "The file path to write (relative to project root)";
        schema["properties"]["files"]["items"]["properties"]["content"]["type"] = "string";
        schema["properties"]["files"]["items"]["properties"]["content"]["description"] = "The content to write. If encoding is 'base64', this should be a base64-encoded string; otherwise it's raw text.";
        schema["properties"]["files"]["items"]["properties"]["encoding"]["type"] = "string";
        schema["properties"]["files"]["items"]["properties"]["encoding"]["description"] = "Encoding of the content. Default is 'text'. Options: 'text', 'base64'. When 'base64', the tool will decode and write in binary mode.";
        schema["required"] = {"files"};
        return schema.dump();
    }

    void show_preview(const std::string& json_args) override {
        try {
            auto args = json::parse(json_args);
            if (!args.contains("files") || !args["files"].is_array()) return;
            for (const auto& file_entry : args["files"]) {
                std::string path = file_entry.value("path", "");
                std::string new_content = file_entry.value("content", "");
                if (!path.empty()) {
                    // Safety: integrated path check
                    auto check_result = SafetyGuard::get_instance().is_path_ok(path);
                    if (check_result == PathCheckResult::Denied) continue;

                    std::ifstream existing(path);
                    if (existing.is_open()) {
                        std::stringstream ss;
                        ss << existing.rdbuf();
                        std::string old_content = ss.str();
                        existing.close();
                        std::string diff = DiffEdit(old_content, new_content);
                        std::cout << std::endl << path << std::endl << diff << std::endl;
                    }
                }
            }
        } catch (...) {}
    }

    bool needs_user_reply(UserReplyMode mode) const override {
        return mode == UserReplyMode::Edit || mode == UserReplyMode::Always;
    }

private:
    // Check if content already has UTF-8 BOM (EF BB BF)
    static bool has_utf8_bom(const std::string& content) {
        return content.size() >= 3 &&
               static_cast<unsigned char>(content[0]) == bom[0] &&
               static_cast<unsigned char>(content[1]) == bom[1] &&
               static_cast<unsigned char>(content[2]) == bom[2];
    }

    // Check if the file extension requires UTF-8 BOM
    static bool needs_bom(const std::string& path) {
        auto ext = fs::path(path).extension().string();
        return ext == ".c" || ext == ".cpp" || ext == ".h" || ext == ".hpp";
    }

public:
    std::string execute(const std::string& json_args) override {
        try {
            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                return "Error: Invalid JSON arguments - not json";
            }
            if (!args.contains("files") || !args["files"].is_array()) {
                return "Error: 'files' array is required.";
            }

            std::string output;

            for (const auto& file_entry : args["files"]) {
                std::string path      = file_entry.value("path", "");
                std::string content   = file_entry.value("content", "");
                std::string encoding  = file_entry.value("encoding", "text");

                if (path.empty()) {
                    output += "FAIL: (no path) - No file path provided.\n";
                    continue;
                }

                // Safety: integrated path check (working dir + whitelist + strict mode).
                auto check_result = SafetyGuard::get_instance().is_path_ok(path);
                if (check_result == PathCheckResult::Denied) {
                    output += "FAIL: " + path + " - Path is outside allowed directories. Operation denied.\n";
                    continue;
                }

                // Decode base64 if needed
                bool binary_mode = false;
                if (encoding == "base64") {
                    content = Base64Decode(content);
                    binary_mode = true;
                }

                // Auto-add UTF-8 BOM for C/C++ source files (text mode only)
                if (!binary_mode && needs_bom(path) && !has_utf8_bom(content)) {
                    content.insert(0, (const char*)bom, 3);
                }

                // Ensure parent directory exists
                auto parent_dir = fs::path(path).parent_path();
                if (!parent_dir.empty()) {
                    std::error_code ec;
                    fs::create_directories(parent_dir, ec);
                    if (ec) {
                        output += "FAIL: " + path + " - Failed to create directory: " + ec.message() + "\n";
                        continue;
                    }
                }

                // Write the file
                std::ofstream file(path, binary_mode ? std::ios::binary : std::ios::trunc);
                if (!file.is_open()) {
                    output += "FAIL: " + path + " - Cannot create/open file: " + strerror(errno) + "\n";
                    continue;
                }

                if (binary_mode) {
                    file.write(content.data(), content.size());
                } else {
                    file << content;
                }
                file.close();

                output += "OK: " + path + "\n";
            }

            return output;
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }
};

// -----------------------------------------------------------------------
// Batch file tools - ReadFilesTool, DeleteFilesTool, WriteFilesTool
// -----------------------------------------------------------------------
ToolPtr create_read_files_tool() {
    return std::make_shared<ReadFilesTool>();
}

ToolPtr create_delete_files_tool() {
    return std::make_shared<DeleteFilesTool>();
}

ToolPtr create_write_files_tool() {
    return std::make_shared<WriteFilesTool>();
}

} // namespace agent
