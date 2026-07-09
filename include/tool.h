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


    static void show_json(const nlohmann::json &args, int depth = 0) {
        std::string indent(depth * 2, ' ');

        if (args.is_object()) {
            for (auto it = args.begin(); it != args.end(); ++it) {
                auto val = it.value();
                // If value is a string with newlines, show line count instead of raw content
                if (val.is_string() && val.get<std::string>().find('\n') != std::string::npos) {
                    auto s = val.get<std::string>();
                    int lines = 1;
                    for (char c : s) if (c == '\n') ++lines;
                    std::cout << indent << it.key() << ": " << lines << " lines\n";
                } else {
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
            // Small arrays: show inline
            if (size <= 3 && args[0].is_primitive()) {
                std::cout << '[';
                for (int i = 0; i < size; ++i) {
                    if (i > 0) std::cout << ", ";
                    show_json(args[i], depth + 1);
                }
                std::cout << ']';
            } else {
                // Large or complex arrays: show count, then items indented
                std::cout << '[' << size << " items]\n";
                for (const auto &item : args) {
                    if (item.is_object()) {
                        show_json(item, depth + 1);
                    } else {
                        std::cout << indent << "- ";
                        show_json(item, depth + 2);
                        std::cout << '\n';
                    }
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
    }

    // Show user friendly arguments
    // Default: parse json_args and display key-value pairs in a readable format.
    virtual void show_arguments(const std::string& json_args) {
        try {
            auto args = nlohmann::json::parse(json_args);
            show_json(args, 0);
        } catch (...) {}
    }

    // Show a preview (e.g. diff) before execution. Override to provide custom preview.
    // Default does nothing.
    virtual void show_preview(const std::string& /*json_args*/) {}

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

 private:
    std::unordered_map<std::string, ToolPtr> tools_;
};

} // namespace agent
