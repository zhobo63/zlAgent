#include "tool.h"
#include <iostream>

namespace agent {

void ToolRegistry::register_tool(ToolPtr tool) {
    tools_.push_back(std::move(tool));
}

std::vector<ToolPtr> ToolRegistry::get_tools() const {
    return tools_;
}

std::vector<ToolDefinition> ToolRegistry::get_definitions() const {
    std::vector<ToolDefinition> defs;
    for (const auto& tool : tools_) {
        defs.push_back(tool->to_definition());
    }
    return defs;
}

std::string ToolRegistry::execute(const std::string& tool_name, const std::string& json_args) {
    for (const auto& tool : tools_) {
        if (tool->name() == tool_name) {
            try {
                return tool->execute(json_args);
            } catch (const std::exception& e) {
                return "Error executing tool '" + tool_name + "': " + e.what();
            }
        }
    }
    return "Unknown tool: " + tool_name;
}

} // namespace agent
