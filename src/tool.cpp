#include "pch.h"

#include "tool.h"

namespace agent {

void ToolRegistry::register_tool(ToolPtr tool) {
    tools_[tool->name()] = std::move(tool);
}

std::vector<ToolPtr> ToolRegistry::get_tools() const {
    std::vector<ToolPtr> result;
    result.reserve(tools_.size());
    for (const auto& pair : tools_) {
        result.push_back(pair.second);
    }
    return result;
}

std::vector<ToolDefinition> ToolRegistry::get_definitions() const {
    std::vector<ToolDefinition> defs;
    defs.reserve(tools_.size());
    for (const auto& pair : tools_) {
        defs.push_back(pair.second->to_definition());
    }
    return defs;
}

std::string ToolRegistry::execute(const std::string& tool_name, const std::string& json_args) {
    auto it = tools_.find(tool_name);
    if (it != tools_.end()) {
        try {
            return it->second->execute(json_args);
        } catch (const std::exception& e) {
            return "Error executing tool '" + tool_name + "': " + e.what();
        }
    }
    return "Unknown tool: " + tool_name;
}

} // namespace agent
