#pragma once

#include <set>
#include <string>
#include <vector>
#include <map>
#include "tool.h"

namespace agent {

/**
 * Represents a locally-installed executable that the Agent can invoke.
 */
struct LocalToolInfo {
    std::string name;          // e.g. "g++", "cmake", "git"
    std::string display_name;  // e.g. "G++ Compiler (GNU)"
    std::string path;          // full resolved path, e.g. "/usr/bin/g++"
    std::string description;   // what this tool does (sent to LLM)
    std::string version;       // detected version string
};

/**
 * Discovers locally-installed tools by scanning PATH and common install locations.
 */
class LocalToolDiscovery {
public:
    // Scan for all known tools
    std::vector<LocalToolInfo> discover();

    // Scan only the specified tool names (subset of known_tools)
    std::vector<LocalToolInfo> discover(const std::set<std::string>& tool_names);

    // Suggest relevant tool names based on project files in a directory.
    // E.g. .cpp -> g++, cmake; package.json -> node, npm; Cargo.toml -> cargo
    static std::set<std::string> suggest_tools_for_context(const std::string& project_dir);

    // Scan for a specific tool by name
    LocalToolInfo find_tool(const std::string& name);

private:
    // Resolve executable path from PATH environment variable
    std::string resolve_path(const std::string& exe_name);

    // Get version string by running "--version" or "-version"
    std::string get_version(const std::string& full_path);

    // Known tools registry - name -> description mapping
    static const std::map<std::string, std::string>& known_tools();
};

/**
 * Wraps a local executable as an Agent Tool.
 * The LLM calls this tool with arguments, and it executes the command.
 */
class LocalExecutableTool : public Tool {
public:
    explicit LocalExecutableTool(const LocalToolInfo& info);

    std::string name() const override;
    std::string description() const override;
    std::string parameters_schema() const override;
    std::string execute(const std::string& json_args) override;

private:
    LocalToolInfo info_;

    // Execute command and capture output (cross-platform)
    std::string run_command(const std::string& full_cmd, const std::string& cwd);
};

/**
 * Creates a ToolPtr for each discovered local tool.
 */
std::vector<ToolPtr> create_local_tools();

} // namespace agent
