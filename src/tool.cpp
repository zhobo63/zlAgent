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
            {
                TUI::cout << indent << it.key() << ": ";
                show_json(val, depth + 1);
                TUI::cout << "\n";
            }
        }
    }
    else if (args.is_string()) {
        auto s = args.get<std::string>();
        // Truncate long single-line strings
        //if (s.size() > 80) s = s.substr(0, 77) + "...";
        TUI::cout << '"' << s << '"';
    }
    else if (args.is_array()) {
        int size = static_cast<int>(args.size());
        // Large or complex arrays: show count, then items indented
        TUI::cout << '[' << size << " items]\n";
        for (const auto& item : args) {
            if (item.is_object()) {
                show_json(item, depth + 1);
            }
            else {
                TUI::cout << indent << "- ";
                show_json(item, depth + 2);
                TUI::cout << "\n";
            }
            TUI::cout << "\n";
        }
    }
    else if (args.is_number()) {
        TUI::cout << args.dump();
    }
    else if (args.is_boolean()) {
        TUI::cout << (args.get<bool>() ? "true" : "false");
    }
    else if (args.is_null()) {
        TUI::cout << "null";
    }
    return true;
}

void Tool::show_text(const std::string& args) {
    TUI::cout << args << "\n";    
}

void Tool::show_json_text(const std::string& json_args) {
    TUI::cout << TUI::ANSI_BRIGHT_BLACK;
    try {
        auto args = nlohmann::json::parse(json_args);
        if (!show_json(args, 0)) {
            show_text(json_args);
        }
    } catch (...) {
        show_text(json_args);
    }
    TUI::cout << TUI::ANSI_RESET;
}

void Tool::show_arguments(const std::string& json_args) {
    LOG_INFO(u8"🛠️Tool", name() + " [arguments]");
    show_json_text(json_args);
}
void Tool::show_preview(const std::string& json_args) {
    LOG_INFO(u8"🛠️Tool", name() + " [preview]");
    show_json_text(json_args);
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

bool ToolRegistry::unregister_tool(const std::string& tool_name) {
    auto it = tools_.find(tool_name);
    if (it != tools_.end()) {
        tools_.erase(it);
        return true;
    }
    return false;
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
