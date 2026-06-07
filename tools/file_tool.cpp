#include "tool.h"
#include "json.hpp"
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

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
            auto args = json::parse(json_args);
            std::string path = args.value("path", "");
            std::string content = args.value("content", "");

            if (path.empty()) return "Error: No file path provided.";

            std::ofstream file(path, std::ios::trunc);
            if (!file.is_open()) {
                return "Error: Cannot create/open file '" + path + "'";
            }
            file << content;
            file.close();

            return "Successfully wrote " + std::to_string(content.size()) +
                   " bytes to '" + path + "'";
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
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
            auto args = json::parse(json_args);
            std::string path = args.value("path", "");
            std::string old_text = args.value("old_text", "");
            std::string new_text = args.value("new_text", "");

            if (path.empty()) return "Error: No file path provided.";
            if (old_text.empty()) return "Error: No old_text provided.";

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

            // Replace
            content.replace(pos, old_text.size(), new_text);

            // Write back
            std::ofstream outfile(path, std::ios::trunc);
            if (!outfile.is_open()) {
                return "Error: Cannot write to file '" + path + "'";
            }
            outfile << content;
            outfile.close();

            return "Successfully edited '" + path + "'. Replaced text with new_text.";
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
            auto args = json::parse(json_args);
            std::string path = args.value("path", "");
            if (path.empty()) return "Error: No directory path provided.";

#ifdef _WIN32
            const char sep = '\\';
#else
            const char sep = '/';
#endif
            if (!path.empty() && path.back() != sep)
                path += sep;

            std::string folders, files;
            int folder_count = 0, file_count = 0;

#ifdef _WIN32
            WIN32_FIND_DATAA find_data;
            std::string search_path = path + "*";
            HANDLE hFind = FindFirstFileA(search_path.c_str(), &find_data);
            if (hFind == INVALID_HANDLE_VALUE) {
                return "Error: Cannot access directory '" + path + "'";
            }

            do {
                std::string name = find_data.cFileName;
                if (name == "." || name == "..") continue;

                if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    folders += "  " + name + "/\n";
                    folder_count++;
                } else {
                    files += "  " + name + "\n";
                    file_count++;
                }
            } while (FindNextFileA(hFind, &find_data) != 0);

            FindClose(hFind);
#else
            DIR* dir = opendir(path.c_str());
            if (!dir) {
                return "Error: Cannot open directory '" + path + "'";
            }

            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                std::string name = entry->d_name;
                if (name == "." || name == "..") continue;

                std::string full = path + name;
                struct stat st;
                if (stat(full.c_str(), &st) == 0) {
                    if (S_ISDIR(st.st_mode)) {
                        folders += "  " + name + "/\n";
                        folder_count++;
                    } else {
                        files += "  " + name + "\n";
                        file_count++;
                    }
                }
            }
            closedir(dir);
#endif

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

ToolPtr create_list_directory_tool() {
    return std::make_shared<ListDirectoryTool>();
}

} // namespace agent
