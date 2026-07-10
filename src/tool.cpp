#include "pch.h"

#include "tool.h"

namespace agent {


bool Tool::show_json(const nlohmann::json& args, int depth) {
    std::string indent(depth * 2, ' ');

    if(args.is_discarded()) {
        return false;
    }
    if (args.is_object()) {
        for (auto it = args.begin(); it != args.end(); ++it) {
            auto val = it.value();
            // If value is a string with newlines, show line count instead of raw content
            if (val.is_string() && val.get<std::string>().find('\n') != std::string::npos) {
                auto s = val.get<std::string>();
                int lines = 1;
                for (char c : s) if (c == '\n') ++lines;
                std::cout << indent << it.key() << ": " << lines << " lines\n";
            }
            else {
                std::cout << indent << it.key() << ": ";
                show_json(val, depth + 1);
                std::cout << '\n';
            }
        }
    }
    else if (args.is_string()) {
        auto s = args.get<std::string>();
        // Truncate long single-line strings
        if (s.size() > 80) s = s.substr(0, 77) + "...";
        std::cout << '"' << s << '"';
    }
    else if (args.is_array()) {
        int size = static_cast<int>(args.size());
        // Large or complex arrays: show count, then items indented
        std::cout << '[' << size << " items]\n";
        for (const auto& item : args) {
            if (item.is_object()) {
                show_json(item, depth + 1);
            }
            else {
                std::cout << indent << "- ";
                show_json(item, depth + 2);
                std::cout << '\n';
            }
        }
    }
    else if (args.is_number()) {
        std::cout << args.dump();
    }
    else if (args.is_boolean()) {
        std::cout << (args.get<bool>() ? "true" : "false");
    }
    else if (args.is_null()) {
        std::cout << "null";
    }
    return true;
}

void Tool::show_text(const std::string& args) {
    // Not valid JSON: print raw content and check for newlines
    if (args.find('\n') != std::string::npos) {
        int lines = 1;
        for (char c : args) if (c == '\n') ++lines;
        std::cout << "<" << lines << " lines>\n";
    }
    else {
        std::cout << args << '\n';
    }
}

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

ToolPtr ToolRegistry::find_tool(const std::string& tool_name) {
    auto it = tools_.find(tool_name);
    if (it != tools_.end()) {
        return it->second;
    }
    return nullptr;
}

std::string ToolRegistry::execute(const std::string& tool_name, const std::string& json_args) {
    auto ptr = find_tool(tool_name);
    if (ptr) {
        try {
            return ptr->execute(json_args);
        } catch (const std::exception& e) {
            return "Error executing tool '" + tool_name + "': " + e.what();
        }
    }
    return "Unknown tool: " + tool_name;
}

} // namespace agent
