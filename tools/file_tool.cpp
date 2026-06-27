#include "pch.h"

#include "tool.h"
#include "file_utils.h"
#include "tui.h"
#include "encoding.h"
#include "safety_guard.h"

namespace agent {
using json = nlohmann::json;

class FileTool : public Tool {
public:
    std::string name() const override { return "read_file"; }
    std::string description() const override {
        return "Read the contents of a file. Returns the full text content.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["path"]["type"] = "string";
        schema["properties"]["path"]["description"] = "The file path to read";
        schema["required"] = {"path"};
        return schema.dump();
    }

    std::string execute(const std::string& json_args) override {
        try {
            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
            auto args = json::parse(json_args);
            std::string path = args.value("path", "");
            if (path.empty()) return "Error: No file path provided.";

            std::ifstream file(path);
            if (!file.is_open()) {
                return "Error: Cannot open file '" + path + "'";
            }
            std::stringstream ss;
            ss << file.rdbuf();
            return ss.str();
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

    std::string execute(const std::string& json_args) override {
        try {
            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
            auto args = json::parse(json_args);
            std::string path = args.value("path", "");
            std::string content = args.value("content", "");

            if (path.empty()) return "Error: No file path provided.";
            if (content.empty()) return "Error: No content provided. For large files, consider using edit_file to make targeted changes instead of write_file.";

            // Safety: path whitelist check.
            if (!SafetyGuard::get_instance().is_path_allowed(path)) {
                return "Error: Path '" + path + "' is outside allowed directories. Operation denied.";
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

ToolPtr create_read_file_tool() {
    return std::make_shared<FileTool>();
}

// -----------------------------------------------------------------------
// EditFileTool - Line-level precise editing (find old_text, replace with new_text)
// -----------------------------------------------------------------------
class EditFileTool : public Tool {
public:
    std::string name() const override { return "edit_file"; }
    std::string description() const override {
        return "Apply precise edits to an existing file. Finds old_text and replaces it with new_text. Use for targeted modifications instead of overwriting the entire file.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["path"]["type"] = "string";
        schema["properties"]["path"]["description"] = "The file path to edit";
        schema["properties"]["old_text"]["type"] = "string";
        schema["properties"]["old_text"]["description"] = "Exact text to find and replace, must match uniquely";
        schema["properties"]["new_text"]["type"] = "string";
        schema["properties"]["new_text"]["description"] = "The replacement text";
        schema["required"] = {"path", "old_text", "new_text"};
        return schema.dump();
    }

    std::string execute(const std::string& json_args) override {
        try {
            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
            auto args = json::parse(json_args);
            std::string path = args.value("path", "");
            std::string old_text = args.value("old_text", "");
            std::string new_text = args.value("new_text", "");

            if (path.empty()) return "Error: No file path provided.";
            if (old_text.empty()) return "Error: No old_text provided.";

            // Safety: path whitelist check.
            if (!SafetyGuard::get_instance().is_path_allowed(path)) {
                return "Error: Path '" + path + "' is outside allowed directories. Operation denied.";
            }

            // Read the file
            std::ifstream infile(path);
            if (!infile.is_open()) {
                return "Error: Cannot open file '" + path + "'";
            }
            std::stringstream ss;
            ss << infile.rdbuf();
            std::string content = ss.str();
            infile.close();

            // Find old_text (support multi-line)
            auto pos = content.find(old_text);
            if (pos == std::string::npos) {
                return "Error: old_text not found in file '" + path + "'. "
                       "The text to replace must match exactly.\n"
                       "Suggested: read the file first with read_file, then copy the exact text.";
            }

            // Check uniqueness - ensure old_text appears only once
            auto pos2 = content.find(old_text, pos + 1);
            if (pos2 != std::string::npos) {
                return "Error: old_text matches multiple locations in '" + path + "'. "
                       "Provide more surrounding context to make the match unique.";
            }

            // Write back
            content.replace(pos, old_text.size(), new_text);
            std::ofstream outfile(path, std::ios::trunc);
            if (!outfile.is_open()) {
                return "Error: Cannot write to file '" + path + "'";
            }
            outfile << content;
            outfile.close();

            // Show diff of the change using TUI colors (red for removed, green for added)
            std::string diff = DiffEdit(old_text, new_text);
            return "Successfully edited '" + path + "'.\n" + diff;
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
            std::string path = args.value("path", "");
            if (path.empty()) {
                path = "./";
            }

namespace fs = std::filesystem;

            if (!path.empty() && path.back() != '/' && path.back() != '\\')
                path += '/';

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

ToolPtr create_write_file_tool() {
    return std::make_shared<WriteFileTool>();
}

ToolPtr create_edit_file_tool() {
    return std::make_shared<EditFileTool>();
}

// -----------------------------------------------------------------------
// ReadFileLinesTool - Read a specific line range from a file
// -----------------------------------------------------------------------
class ReadFileLinesTool : public Tool {
public:
    std::string name() const override { return "read_file_lines"; }
    std::string description() const override {
        return "Read a specific line range from a file. More efficient than read_file for large files when you only need certain lines.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["path"]["type"] = "string";
        schema["properties"]["path"]["description"] = "The file path to read";
        schema["properties"]["start_line"]["type"] = "integer";
        schema["properties"]["start_line"]["description"] = "Starting line number (1-based)";
        schema["properties"]["end_line"]["type"] = "integer";
        schema["properties"]["end_line"]["description"] = "Ending line number (inclusive, 1-based)";
        schema["required"] = {"path", "start_line", "end_line"};
        return schema.dump();
    }

    std::string execute(const std::string& json_args) override {
        try {
            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
            auto args = json::parse(json_args);
            std::string path = args.value("path", "");
            int start_line = args.value("start_line", 0);
            int end_line   = args.value("end_line", 0);

            if (path.empty()) return "Error: No file path provided.";
            if (start_line <= 0) return "Error: start_line must be >= 1.";
            if (end_line < start_line) return "Error: end_line must be >= start_line.";

            std::string result = agent::ReadFileLinesAsString(path, start_line, end_line);
            if (result.empty()) {
                return "Error: Cannot read file '" + path + "' or line range is out of bounds.";
            }
            return result;
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }
};

ToolPtr create_list_directory_tool() {
    return std::make_shared<ListDirectoryTool>();
}

ToolPtr create_read_file_lines_tool() {
    return std::make_shared<ReadFileLinesTool>();
}

} // namespace agent
