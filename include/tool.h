#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include "llm_client.h"

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

    // Convert to LLM-compatible ToolDefinition
    ToolDefinition to_definition() const {
        return {name(), description(), parameters_schema()};
    }

    // Compact definition: name + description only, no JSON Schema.
    // Local LLMs can infer parameter names from the tool name/description,
    // so sending full schemas wastes tokens. Override to force a schema.
    virtual ToolDefinition compact_definition() const {
        return {name(), description(), ""};
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

    // Compact definitions: name + description only, no JSON Schema.
    // Local LLMs can infer parameter names from the tool name/description,
    // so sending full schemas wastes tokens. Use this to reduce prompt size.
    std::vector<ToolDefinition> get_compact_definitions() const;

    // Find and execute a tool by name
    std::string execute(const std::string& tool_name, const std::string& json_args);

 private:
    std::unordered_map<std::string, ToolPtr> tools_;
};

} // namespace agent
