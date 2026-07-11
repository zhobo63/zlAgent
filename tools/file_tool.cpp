#include "pch.h"

#include <cstring>

#include "tool.h"
#include "file_utils.h"
#include "tui.h"
#include "key_watcher.h"
#include "safety_guard.h"

const uint8_t bom[] = { 0xEF, 0xBB, 0xBF };

namespace agent {
using json = nlohmann::json;

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

            // Full file read
            std::ifstream file(path);
            if (!file.is_open()) {
                return "Error: Cannot open file '" + path + "'";
            }

            // Count lines to decide whether to show outline
            int line_count = 0;
            std::string fline;
            while (std::getline(file, fline)) line_count++;
            file.close();
            if (end_line < 0) { end_line = line_count; }
            if (start_line >= 0 && end_line >= 0 && end_line >= start_line) {
                std::string content = agent::ReadFileLinesAsString(path, start_line, end_line, line_count);
                if (content.empty()) {
                    return "Error: Cannot read file '" + path + "' or line range is out of bounds.";
                }
                std::ostringstream oss;
                oss << "# File for " << path << " (" << line_count << ")\n";
                oss << content;
                return oss.str();
            }

            const int OUTLINE_THRESHOLD = 200; // lines
            if (line_count > OUTLINE_THRESHOLD) {
                std::string outline = agent::GenerateFileOutline(path);
                if (!outline.empty()) {
                    return outline;
                }
            }

            // Re-read full content for small files
            file.open(path);
            if (!file.is_open()) {
                return "Error: Cannot open file '" + path + "'";
            }

            int width = static_cast<int>(std::to_string(line_count).size());
            std::ostringstream oss;
            oss << "# File for " << path << " (" << line_count << ")\n";

            int current = 1;
            while (std::getline(file, fline)) {
                oss << std::setw(width) << current << " " << fline << "\n";
                current++;
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
            "  (1) paths — string array, e.g. {\"paths\":[\"src/a.cpp\",\"inc/b.h\"], \"outline\":false}\n"
            "      outline is required at top-level: true for file outlines (symbol names and line numbers), false for full content.\n"
            "  (2) files — object array with per-file options, e.g. {\"files\":[{\"path\":\"a.cpp\",\"outline\":true,\"start_line\":1,\"end_line\":100}]}\n"
            "      Each file can have its own outline (bool), start_line/end_line for range. In outline mode, range limits the symbols returned.\n"
            "  (3) directory + glob — e.g. {\"directory\":\"src\", \"glob\":\"*.cpp\", \"outline\":true}\n"
            "      Scans a directory with a file pattern; outline is required at top-level.\n"
            "*outline mode support C/C++, Python, JavaScript/TypeScript, Go, Rust, Java, Markdown\n";
    }
    std::string parameters_schema() const override {
        static std::string schema = json::parse(R"({
            "type": "object",
            "properties": {
                "paths": {
                    "type": "array",
                    "items": {"type": "string"},
                    "description": "List of file paths to read as a string array. E.g. [\"file1.cpp\", \"file2.h\"]. Requires top-level 'outline'. Can be combined with files/directory+glob."
                },
                "files": {
                    "type": "array",
                    "items": {"type": "object"},
                    "description": "List of file objects with per-file options. Each object: {\"path\": \"file.cpp\", \"outline\": false, \"start_line\": 1, \"end_line\": 100}. Can be combined with paths/directory+glob."
                },
                "directory": {
                    "type": "string",
                    "description": "Directory to search in (default: current directory) "
                },
                "glob": {
                    "type": "string",
                    "description": "File pattern to match (mutually exclusive with paths, default: '*') "
                },
                "outline": {
                    "type": "boolean",
                    "description": "Required when using 'paths' or 'directory'+glob. Read file outlines (symbol names and line numbers) instead of full content. Set to true for outline only, false to read the complete file content."
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

        // Count total lines for proper width
        std::ifstream file(path);
        if (!file.is_open()) {
            oss << "# Error: " << path << " - Cannot open file";
            return;
        }
        int line_count = 0;
        std::string fline;
        while (std::getline(file, fline)) line_count++;
        file.close();

        // Line range mode (object mode only)
        if (start_line > 0 && end_line >= start_line) {
            std::string content = agent::ReadFileLinesAsString(path, start_line, end_line, line_count);
            if (!content.empty()) {
                oss << "# File for " << path << " (" << line_count << ")\n";
                oss << content;
            } else {
                oss << "# Error: " << path << " - Line range out of bounds or cannot read file";
            }
            return;
        }

        // Full content mode
        int width = static_cast<int>(std::to_string(line_count).size());
        oss << "# File for " << path << " (" << line_count << ")\n";

        file.open(path);
        if (!file.is_open()) {
            oss << "# Error: " << path << " - Cannot open file";
            return;
        }

        int current = 1;
        while (std::getline(file, fline)) {
            oss << std::setw(width) << current << " " << fline << "\n";
            current++;
        }
        file.close();
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

                namespace fs = std::filesystem;
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
            namespace fs = std::filesystem;
            auto parent_dir = fs::path(path).parent_path();
            if (!parent_dir.empty()) {
                std::error_code ec;
                fs::create_directories(parent_dir, ec);
                if (ec) {
                    return "Error: Failed to create directory '" + parent_dir.string() + "': " + ec.message();
                }
            }

            std::ofstream file(path, std::ios::trunc);
            if (!file.is_open()) {
                return "Error: Cannot create/open file '" + path + "'";
            }
            file << content;
            file.close();

            return "Successfully wrote " + std::to_string(content.size()) +
                   " bytes to '" + path + "'";
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what()) +
                   ". The response may have been truncated due to token limits. For large files, use edit_file for targeted changes instead.";
        }
    }
};


// -----------------------------------------------------------------------
// DeleteFilesTool - Batch delete multiple files (paths or directory+glob)
// -----------------------------------------------------------------------

namespace fs = std::filesystem;

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

            namespace fs = std::filesystem;

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
            bool dry_run = args.value("dry_run", false);
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

                namespace fs = std::filesystem;

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

            // Build JSON response
            json result;
            result["success"] = true;
            result["dry_run"] = dry_run;
            result["deleted_files"] = json::array();
            int total_size = 0;
            int success_count = 0;
            int error_count = 0;

            for (const auto& path : file_paths) {
                // Get file info before deletion
                int size_bytes = 0;
                std::string modified_time;
                try {
                    namespace fs = std::filesystem;
                    if (fs::exists(path)) {
                        size_bytes = static_cast<int>(fs::file_size(path));
                        auto time_point = fs::last_write_time(path);
                        // Convert to string format
                        std::time_t t = to_time_t(time_point);
                        char buf[64];
                        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
                        modified_time = buf;
                    }
                } catch (...) {}

                if (dry_run) {
                    // Only record without deleting
                    json file_entry;
                    file_entry["path"] = path;
                    file_entry["size_bytes"] = size_bytes;
                    file_entry["modified_time"] = modified_time.empty() ? "" : modified_time;
                    result["deleted_files"].push_back(file_entry);
                } else {
                    // Actually delete the file
                    namespace fs = std::filesystem;
                    if (fs::exists(path)) {
                        auto ec = fs::remove(path);
                        if (!ec) {
                            total_size += size_bytes;
                            success_count++;

                            json file_entry;
                            file_entry["path"] = path;
                            file_entry["status"] = "success";
                            file_entry["size_bytes"] = size_bytes;
                            file_entry["modified_time"] = modified_time.empty() ? "" : modified_time;
                            result["deleted_files"].push_back(file_entry);
                        } else {
                            error_count++;

                            json err_entry;
                            err_entry["path"] = path;
                            err_entry["status"] = "error";
                            err_entry["error"] = std::to_string(ec);
                            result["deleted_files"].push_back(err_entry);
                        }
                    } else {
                        error_count++;

                        json err_entry;
                        err_entry["path"] = path;
                        err_entry["status"] = "error";
                        err_entry["error"] = "File not found";
                        result["deleted_files"].push_back(err_entry);
                    }
                }
            }

            result["total_files"] = static_cast<int>(file_paths.size());
            if (!dry_run) {
                result["deleted_count"] = success_count;
                result["summary"] = {
                    {"success_count", success_count},
                    {"error_count", error_count}
                };
            } else {
                result["message"] = "Dry run mode: No files were actually deleted.";
            }

            return result.dump();
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
        return "Apply precise edits to an existing file. Two modes: (1) text-based: provide old_text and new_text to find-and-replace; (2) line-based: provide start_line, end_line and new_text to replace a line range. Only one mode should be used at a time.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["path"]["type"] = "string";
        schema["properties"]["path"]["description"] = "The file path to edit";
        schema["properties"]["old_text"]["type"] = "string";
        schema["properties"]["old_text"]["description"] = "Exact text to find and replace (text-based mode). Must match uniquely.";
        schema["properties"]["new_text"]["type"] = "string";
        schema["properties"]["new_text"]["description"] = "The replacement text";
        schema["properties"]["start_line"]["type"] = "integer";
        schema["properties"]["start_line"]["description"] = "Starting line number for range-based edit (1-based). Use with end_line instead of old_text.";
        schema["properties"]["end_line"]["type"] = "integer";
        schema["properties"]["end_line"]["description"] = "Ending line number for range-based edit (inclusive, 1-based). Use with start_line instead of old_text.";
        schema["required"] = {"path", "new_text"};
        return schema.dump();
    }

    void show_preview(const std::string& json_args) override {
        try {
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                Tool::show_preview(json_args);
                return;
            }
            std::string path = args.value("path", "");
            std::string old_text = args.value("old_text", "");
            std::string new_text = args.value("new_text", "");

            if (!old_text.empty()) {
                // Text-based mode
                std::string diff = DiffEdit(old_text, new_text);
                std::cout << std::endl << path << std::endl << diff << std::endl;
            } else {
                // Line-based mode
                int start_line = args.value("start_line", 0);
                int end_line   = args.value("end_line", 0);
                if (!path.empty() && start_line > 0 && end_line >= start_line) {
                    std::vector<std::pair<int, std::string>> lines;
                    if (ReadFileLines(path, start_line, end_line, lines)) {
                        std::string old_content;
                        for (const auto& [num, content] : lines) {
                            old_content += content + "\n";
                        }
                        std::string diff = DiffEdit(old_content, new_text, start_line);
                        std::cout << std::endl << path << " (lines " << start_line << "-" << end_line << ")" << std::endl << diff << std::endl;
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
            std::string old_text = args.value("old_text", "");
            std::string new_text = args.value("new_text", "");
            int start_line = args.value("start_line", 0);
            int end_line   = args.value("end_line", 0);

            if (path.empty()) return "Error: No file path provided.";
            if (new_text.empty() && old_text.empty()) return "Error: Neither old_text nor new_text provided.";

            // Determine mode: text-based or line-based
            bool text_mode = !old_text.empty();
            bool line_mode = start_line > 0 && end_line >= start_line;

            if (!text_mode && !line_mode) {
                return "Error: Must provide either old_text (text-based mode) or start_line/end_line (line-based mode).";
            }
            if (text_mode && line_mode) {
                return "Error: Cannot use both old_text and start_line/end_line at the same time. Choose one mode.";
            }

            // Safety: integrated path check (working dir + whitelist + strict mode).
            auto check_result = SafetyGuard::get_instance().is_path_ok(path);
            if (check_result == PathCheckResult::Denied) {
                return "Error: Path '" + path + "' is outside allowed directories. Operation denied.";
            }
            if (text_mode) {
                std::ifstream infile(path);
                if (!infile.is_open()) {
                    return "Error: Cannot open file '" + path + "'";
                }
                std::stringstream ss;
                ss << infile.rdbuf();
                std::string content = ss.str();
                infile.close();

                auto pos = content.find(old_text);
                if (pos == std::string::npos) {
                    return "Error: old_text not found in file '" + path + "'. "
                           "The text to replace must match exactly.\n"
                           "Suggested: read the file first with read_file, then copy the exact text.";
                }

                auto pos2 = content.find(old_text, pos + 1);
                if (pos2 != std::string::npos) {
                    return "Error: old_text matches multiple locations in '" + path + "'. "
                           "Provide more surrounding context to make the match unique.";
                }

                content.replace(pos, old_text.size(), new_text);
                std::ofstream outfile(path, std::ios::trunc);
                if (!outfile.is_open()) {
                    return "Error: Cannot write to file '" + path + "'";
                }
                outfile << content;
                outfile.close();

                std::string diff = DiffEdit(old_text, new_text);
                return "Successfully edited '" + path + "'.\n" + diff;
            } else {
                // --- Line-based mode: replace lines start_line..end_line with new_text ---
                if (new_text.empty()) return "Error: new_text is required in line-based mode.";

                agent::EditFile ef;
                if (!ef.read_file(path)) {
                    return "Error: Cannot open file '" + path + "'";
                }

                if (end_line > static_cast<int>(ef.lines.size())) {
                    return "Error: end_line " + std::to_string(end_line) +
                           " exceeds file length (" + std::to_string(ef.lines.size()) + " lines).";
                }

                // Capture the old content for diff
                std::string old_content;
                for (int i = start_line - 1; i <= end_line - 1; ++i) {
                    old_content += ef.lines[i] + "\n";
                }

                ef.replace_line_range(start_line, end_line, new_text);

                agent::EditLines result;
                ef.apply_blocks(result);

                if (!result.write_file(path)) {
                    return "Error: Cannot write to file '" + path + "'";
                }

                std::string diff = DiffEdit(old_content, new_text, start_line);
                return "Successfully replaced lines " + std::to_string(start_line) + "-" +
                       std::to_string(end_line) + " in '" + path + "'.\n" + diff;
            }
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
Each operation type is specified as a top-level array property.
Supported operations: replace_line_range, insert_before_line,
insert_after_line, delete_lines, replace_text.
All line numbers are based on the original file before any edits.
Operations within a file are atomic - if any operation fails (e.g., overlapping ranges),
that entire file is rolled back. Files are independent of each other.)";
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
                },
                "replace_text": {
                    "type": "array",
                    "description": "Replace text content in the file. If old_text appears multiple times, user will be prompted to choose.",
                    "items": {
                        "type": "object",
                        "required": ["path", "old_text", "new_text"],
                        "properties": {
                            "path": {"type": "string", "description": "File path to edit (relative to project root) "},
                            "old_text": {"type": "string", "description": "Exact text to find and replace. Must match exactly."},
                            "new_text": {"type": "string", "description": "Replacement text"}
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
                        std::cout << "    replace_line_range: '" << path << "' lines " << start << "-" << end << '\n';
                    } else if (strcmp(type_name, "insert_before_line") == 0) {
                        int line_num = op.value("start_line", 0);
                        std::cout << "    insert_before_line: '" << path << "' before line " << line_num << '\n';
                    } else if (strcmp(type_name, "insert_after_line") == 0) {
                        int line_num = op.value("start_line", 0);
                        std::cout << "    insert_after_line: '" << path << "' after line " << line_num << '\n';
                    } else if (strcmp(type_name, "delete_lines") == 0) {
                        int start = op.value("start_line", 0);
                        int end   = op.value("end_line", 0);
                        std::cout << "    delete_lines: '" << path << "' lines " << start << "-" << end << '\n';
                    } else if (strcmp(type_name, "replace_text") == 0) {
                        std::string old_text = op.value("old_text", "");
                        size_t nl = old_text.find('\n');
                        std::string preview = (nl != std::string::npos) ? old_text.substr(0, nl) : old_text;
                        if (preview.size() > 60) preview = preview.substr(0, 57) + "...";
                        std::cout << "    replace_text: '" << path << "' '" << preview << "'\n";
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
            if (is_json_array(args, "replace_text"))
                show_ops("replace_text", args["replace_text"]);

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

            // Reuse parse_operations to avoid duplicating JSON parsing logic
            auto file_ops = parse_operations(args);

            for (auto& [path, ops] : file_ops) {
                agent::EditFile ef;
                if (!ef.read_file(path)) continue;
                for (const auto& block : ops.blocks)
                    ef.blocks.push_back(block);

                std::string old_content = ef.to_string();

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
    static bool is_json_array(const json& obj, const char* key) {
        return obj.contains(key) && obj[key].is_array();
    }

    struct FileOps {
        std::vector<agent::EditFile::ModifiedBlock> blocks;
        std::vector<std::pair<std::string, std::string>> text_replacements; // (old_text, new_text) — resolved to blocks after interaction
    };

    /// Parse flat JSON args and collect operations grouped by file path.
    static std::map<std::string, FileOps> parse_operations(const json& args) {
        std::map<std::string, FileOps> file_ops;

        auto add_block = [&](const json& op) {
            std::string path = op.value("path", "");
            if (path.empty()) return;
            int start = op.value("start_line", 0);
            int end   = op.value("end_line", 0);
            std::string new_text = op.value("new_text", "");
            file_ops[path].blocks.push_back({start, end, new_text, false});
        };

        auto add_insertion = [&](const json& op) {
            std::string path = op.value("path", "");
            if (path.empty()) return;
            int start_line = op.value("start_line", 0);
            std::string new_text = op.value("new_text", "");
            file_ops[path].blocks.push_back({start_line, -1, new_text, true});
        };

        auto add_insert_after = [&](const json& op) {
            std::string path = op.value("path", "");
            if (path.empty()) return;
            int start_line = op.value("start_line", 0);
            std::string new_text = op.value("new_text", "");
            file_ops[path].blocks.push_back({start_line + 1, -1, new_text, true});
        };

        auto add_delete = [&](const json& op) {
            std::string path = op.value("path", "");
            if (path.empty()) return;
            int start = op.value("start_line", 0);
            int end   = op.value("end_line", 0);
            file_ops[path].blocks.push_back({start, end, "", false});
        };

        auto add_replace_text = [&](const json& op) {
            std::string path = op.value("path", "");
            if (path.empty()) return;
            std::string old_text = op.value("old_text", "");
            std::string new_text = op.value("new_text", "");
            file_ops[path].text_replacements.push_back({old_text, new_text});
        };

        if (is_json_array(args, "replace_line_range"))
            for (const auto& op : args["replace_line_range"]) add_block(op);
        if (is_json_array(args, "insert_before_line"))
            for (const auto& op : args["insert_before_line"]) add_insertion(op);
        if (is_json_array(args, "insert_after_line"))
            for (const auto& op : args["insert_after_line"]) add_insert_after(op);
        if (is_json_array(args, "delete_lines"))
            for (const auto& op : args["delete_lines"]) add_delete(op);
        if (is_json_array(args, "replace_text"))
            for (const auto& op : args["replace_text"]) add_replace_text(op);

        return file_ops;
    }

    /// Process a single file: validate blocks, apply replace_text interactively (converted to blocks), write back.
    bool process_file(const std::string& path,
                      const FileOps& ops,
                      std::string& error_out,
                      int& original_lines_out,
                      int& new_lines_out) {
        if (path.empty()) {
            error_out = "No file path provided.";
            return false;
        }

        auto check_result = SafetyGuard::get_instance().is_path_ok(path);
        if (check_result == PathCheckResult::Denied) {
            error_out = "Path '" + path + "' is outside allowed directories. Operation denied.";
            return false;
        }

        agent::EditFile ef;
        if (!ef.read_file(path)) {
            error_out = "Cannot open file '" + path + "'.";
            return false;
        }
        original_lines_out = static_cast<int>(ef.lines.size());

        // Add line-based blocks from ops
        for (const auto& block : ops.blocks)
            ef.blocks.push_back(block);

        // Resolve replace_text into blocks via interaction
        std::string content = ef.to_string();

        for (const auto& [old_text, new_text] : ops.text_replacements) {
            if (old_text.empty()) {
                error_out = "old_text cannot be empty string.";
                return false;
            }

            size_t first_pos = content.find(old_text);
            if (first_pos == std::string::npos) {
                error_out = "old_text not found in file: \"" + old_text + "\".";
                return false;
            }

            auto occurrences = agent::EditFile::find_occurrences(ef.lines, old_text);

            if (occurrences.size() == 1) {
                auto [sl, el] = agent::EditFile::text_range_to_lines(ef.lines, occurrences[0].second, old_text.size());
                ef.replace_line_range(sl, el, new_text);
            } else {
                std::ostringstream prompt;
                prompt << "\n" << path << ": \"" << old_text << "\" found in "
                       << occurrences.size() << " locations:\n";
                for (size_t i = 0; i < occurrences.size(); ++i) {
                    int line_num = occurrences[i].first;
                    prompt << "  [" << (i + 1) << "] Line " << line_num
                           << ": \"" << ef.lines[line_num - 1] << "\"\n";
                }
                prompt << "\nChoose: [N] skip / [num] replace that one / [A] all: ";
                std::cout << prompt.str();

                std::string choice = KeyWatcher::readline("", nullptr);

                if (choice == "N" || choice == "n") {
                    // Skip this operation
                } else if (choice == "A" || choice == "a") {
                    for (const auto& [ln, cpos] : occurrences) {
                        auto [sl, el] = agent::EditFile::text_range_to_lines(ef.lines, cpos, old_text.size());
                        ef.replace_line_range(sl, el, new_text);
                    }
                } else {
                    int idx = 0;
                    try { idx = std::stoi(choice); } catch (...) {}
                    if (idx >= 1 && idx <= static_cast<int>(occurrences.size())) {
                        auto [ln, cpos] = occurrences[idx - 1];
                        auto [sl, el] = agent::EditFile::text_range_to_lines(ef.lines, cpos, old_text.size());
                        ef.replace_line_range(sl, el, new_text);
                    }
                }
            }
        }

        // Validate all blocks (including those from replace_text)
        if (!ef.validate_blocks(error_out))
            return false;

        // Apply all blocks and write back
        EditLines result;
        ef.apply_blocks(result);

        if (!result.write_file(path)) {
            error_out = "Cannot write to file '" + path + "'.";
            return false;
        }

        new_lines_out = static_cast<int>(result.lines.size());
        return true;
    }

public:
    std::string execute(const std::string& json_args) override {
        try {
            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
            auto args = json::parse(json_args);
            if (args.is_discarded()) {
                return "Error: Invalid JSON arguments - not json";
            }

            auto file_ops = parse_operations(args);

            json result_edited  = json::array();
            json result_failed  = json::array();

            for (auto& [path, ops] : file_ops) {
                int original_lines = 0, new_lines = 0;
                std::string error;

                if (process_file(path, ops, error, original_lines, new_lines)) {
                    json file_result;
                    file_result["path"] = path;
                    file_result["original_lines"] = original_lines;
                    file_result["new_lines"] = new_lines;
                    result_edited.push_back(file_result);
                } else {
                    json file_error;
                    file_error["path"] = path.empty() ? "(unknown)" : path;
                    file_error["error"] = error;
                    result_failed.push_back(file_error);
                }
            }

            std::string output;
            for (auto& file : result_edited) {
                auto path = file.value("path", "");
                auto orig = file.value("original_lines", 0);
                auto newl = file.value("new_lines", 0);
                output += "edited: " + path + " (" + std::to_string(orig) + " -> " + std::to_string(newl) + " lines)\n";
            }
            for (auto& file : result_failed) {
                auto path = file.value("path", "");
                auto error = file.value("error", "");
                output += "failed: " + path + " - " + error + "\n";
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

namespace fs = std::filesystem;

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
            bool has_trailing_newline = false;
            if (!agent::read_file_lines(path, lines, &has_trailing_newline)) {
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
            if (!agent::write_file_lines(path, lines, has_trailing_newline)) {
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
        namespace fs = std::filesystem;
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

            json result_written = json::array();
            json result_failed  = json::array();

            for (const auto& file_entry : args["files"]) {
                std::string path      = file_entry.value("path", "");
                std::string content   = file_entry.value("content", "");
                std::string encoding  = file_entry.value("encoding", "text");

                if (path.empty()) {
                    result_failed.push_back({{"path", ""}, {"status", "error"}, {"error", "No file path provided."}});
                    continue;
                }

                // Safety: integrated path check (working dir + whitelist + strict mode).
                auto check_result = SafetyGuard::get_instance().is_path_ok(path);
                if (check_result == PathCheckResult::Denied) {
                    result_failed.push_back({{"path", path}, {"status", "error"}, {"error", "Path is outside allowed directories. Operation denied."}});
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
                namespace fs = std::filesystem;
                auto parent_dir = fs::path(path).parent_path();
                if (!parent_dir.empty()) {
                    std::error_code ec;
                    fs::create_directories(parent_dir, ec);
                    if (ec) {
                        result_failed.push_back({{"path", path}, {"status", "error"}, {"error", "Failed to create directory: " + ec.message()}});
                        continue;
                    }
                }

                // Write the file
                std::ofstream file(path, binary_mode ? std::ios::binary : std::ios::trunc);
                if (!file.is_open()) {
                    result_failed.push_back({{"path", path}, {"status", "error"}, {"error", std::string("Cannot create/open file: ") + strerror(errno)}});
                    continue;
                }

                if (binary_mode) {
                    file.write(content.data(), content.size());
                } else {
                    file << content;
                }
                file.close();

                result_written.push_back({{"path", path}, {"status", "ok"}});
            }

            // Build response JSON
            json response;
            response["written_files"] = result_written;
            response["failed_files"]  = result_failed;

            return response.dump(2);
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
