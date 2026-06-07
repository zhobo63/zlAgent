#pragma once

#include <string>
#include <vector>
#include "tool.h"

namespace agent {

/**
 * Wraps a dynamically-loaded DLL as a Tool instance.
 */
class PluginLoader;  // Forward declaration

class PluginTool : public Tool {
    friend class PluginLoader;
public:
    std::string name() const override;
    std::string description() const override;
    std::string parameters_schema() const override;
    std::string execute(const std::string& json_args) override;

private:
    void* handle_ = nullptr;

    // Function pointers loaded from DLL
    typedef const char* (*GetNameFunc)();
    typedef const char* (*GetDescFunc)();
    typedef const char* (*GetSchemaFunc)();
    typedef int         (*ExecuteFunc)(const char*, char*, int);

    GetNameFunc get_name_ = nullptr;
    GetDescFunc get_desc_ = nullptr;
    GetSchemaFunc get_schema_ = nullptr;
    ExecuteFunc execute_fn_ = nullptr;
};

/**
 * Scans a directory for DLL files matching "tool_*.dll" pattern,
 * loads them and returns PluginTool instances.
 */
class PluginLoader {
public:
    // Load all plugins from the given directory (default: "./plugins")
    std::vector<ToolPtr> load_plugins(const std::string& plugin_dir = "plugins");

    // Load a single DLL file
    ToolPtr load_plugin(const std::string& dll_path);

private:
    void* load_library(const std::string& path);
    void  unload_library(void* handle);
    void* get_symbol(void* handle, const std::string& name);
};

} // namespace agent
