#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <iostream>
#include "json.hpp"
#include "llm_client.h"
#include "user_reply.h"

namespace agent {

/**
 * Base class for all tools the Agent can invoke.
 */
class Tool {
public:
    virtual ~Tool() = default;

    // Human-readable name and description (sent to LLM)
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual std::string parameters_schema() const = 0;  // JSON Schema

    // Execute the tool with JSON arguments string, return result as string
    virtual std::string execute(const std::string& json_args) = 0;

    static bool show_json(const nlohmann::json& args, int depth = 0);

    static void show_text(const std::string& args);
    static void show_json_text(const std::string& json_args);

    // Show user friendly arguments
    // Default: parse json_args and display key-value pairs in a readable format.
    virtual void show_arguments(const std::string& json_args);

    // Show a preview (e.g. diff) before execution. Override to provide custom preview.
    // Default does nothing.
    virtual void show_preview(const std::string& json_args);

    virtual void show_result(const std::string& json_args);

    // Whether this tool requires user confirmation in the given mode.
    // Default: only when Always mode is active.
    virtual bool needs_user_reply(UserReplyMode mode) const {
        return mode == UserReplyMode::Always;
    }

    // Convert to LLM-compatible ToolDefinition
    ToolDefinition to_definition() const {
        return {name(), description(), parameters_schema()};
    }
};

using ToolPtr = std::shared_ptr<Tool>;

/**
 * Registry that holds all available tools.
 */
class ToolRegistry {
public:
    void register_tool(ToolPtr tool);
    std::vector<ToolPtr> get_tools() const;
    std::vector<ToolDefinition> get_definitions() const;

    // Find and execute a tool by name
    std::string execute(const std::string& tool_name, const std::string& json_args);

    // Find a tool by name (returns nullptr if not found)
    ToolPtr find_tool(const std::string& tool_name);

    // Unregister a tool by name (returns true if found and removed)
    bool unregister_tool(const std::string& tool_name);

 private:
    std::unordered_map<std::string, ToolPtr> tools_;
};

} // namespace agent
