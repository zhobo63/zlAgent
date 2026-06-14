#include "plugin_loader.h"
#include "wide_string.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <filesystem>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#else
#include <dlfcn.h>
#endif

namespace agent {

// ============================================================================
// PluginTool - wraps a DLL as a Tool
// ============================================================================

std::string PluginTool::name() const {
    if (get_name_) return get_name_();
    return "unknown_plugin";
}

std::string PluginTool::description() const {
    if (get_desc_) return get_desc_();
    return "No description available.";
}

std::string PluginTool::parameters_schema() const {
    if (get_schema_) return get_schema_();
    return "{}";
}

std::string PluginTool::execute(const std::string& json_args) {
    if (!execute_fn_) {
        return "Error: Plugin execute function not available.";
    }

    char buffer[65536];  // 64KB result buffer
    int written = execute_fn_(json_args.c_str(), buffer, sizeof(buffer));

    if (written < 0) {
        return "Error: Plugin execution failed.";
    }

    if (written == 0) {
        return "";
    }

    // Handle case where plugin wrote more than buffer (truncate safely)
    int safe_len = static_cast<int>(std::min(static_cast<size_t>(written), sizeof(buffer) - 1));
    buffer[safe_len] = '\0';

    return std::string(buffer, safe_len);
}

// ============================================================================
// PluginLoader - dynamic DLL loading
// ============================================================================

void* PluginLoader::load_library(const std::string& path) {
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen == 0) return nullptr;

    auto wpath = std::make_unique<wchar_t[]>(wlen);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.get(), wlen);

    HMODULE hmod = LoadLibraryW(wpath.get());
    if (!hmod) {
        DWORD err = GetLastError();
        std::ostringstream oss;
        oss << "Failed to load DLL '" << path << "' (error " << err << ")";
        std::cerr << "[Plugin] " << oss.str() << std::endl;
    }
    return reinterpret_cast<void*>(hmod);
#else
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        std::cerr << "[Plugin] Failed to load library '" << path << "': " << dlerror() << std::endl;
    }
    return handle;
#endif
}

void PluginLoader::unload_library(void* handle) {
    if (handle) {
#ifdef _WIN32
        FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
    }
}

void* PluginLoader::get_symbol(void* handle, const std::string& name) {
#ifdef _WIN32
    HMODULE hmod = reinterpret_cast<HMODULE>(handle);
    FARPROC proc = GetProcAddress(hmod, name.c_str());
    if (!proc) {
        std::cerr << "[Plugin] Symbol '" << name << "' not found in DL" << std::endl;
    }
    return reinterpret_cast<void*>(proc);
#else
    void* sym = dlsym(handle, name.c_str());
    if (!sym) {
        std::cerr << "[Plugin] Symbol '" << name << "' not found: " << dlerror() << std::endl;
    }
    return sym;
#endif
}

ToolPtr PluginLoader::load_plugin(const std::string& dll_path) {
    void* handle = load_library(dll_path);
    if (!handle) return nullptr;

    auto plugin = std::make_shared<PluginTool>();
    plugin->handle_ = handle;

    // Load all required symbols
    plugin->get_name_   = reinterpret_cast<PluginTool::GetNameFunc>(get_symbol(handle, "get_tool_name"));
    plugin->get_desc_   = reinterpret_cast<PluginTool::GetDescFunc>(get_symbol(handle, "get_tool_description"));
    plugin->get_schema_ = reinterpret_cast<PluginTool::GetSchemaFunc>(get_symbol(handle, "get_tool_parameters_schema"));
    plugin->execute_fn_ = reinterpret_cast<PluginTool::ExecuteFunc>(get_symbol(handle, "execute_too"));

    // Validate: at minimum we need name and execute
    if (!plugin->get_name_ || !plugin->execute_fn_) {
        std::cerr << "[Plugin] Library '" << dll_path << "' missing required exports. Skipping." << std::endl;
        unload_library(handle);
        return nullptr;
    }

    std::cout << "[Plugin] Loaded: " << plugin->name()
               << " - " << plugin->description() << std::endl;

    return plugin;
}

std::vector<ToolPtr> PluginLoader::load_plugins(const std::string& plugin_dir) {
    std::vector<ToolPtr> plugins;

#ifdef _WIN32
    // Scan directory for tool_*.dll files — use Wide API for Unicode support.
    std::string search_path = plugin_dir + "\\tool_*.dll";
    std::wstring wsearch = agent::utf8_to_wide(search_path);

    WIN32_FIND_DATAW find_data;
    HANDLE hFind = FindFirstFileW(wsearch.c_str(), &find_data);

    if (hFind == INVALID_HANDLE_VALUE) {
        std::cout << "[Plugin] No plugins found in '" << plugin_dir << "/'" << std::endl;
        return plugins;
    }

    do {
        std::string dll_name = agent::wide_to_utf8(find_data.cFileName);
        if (dll_name == "." || dll_name == "..") continue;

        std::string full_path = plugin_dir + "\\" + dll_name;

        auto tool = load_plugin(full_path);
        if (tool) {
            plugins.push_back(std::move(tool));
        }
    } while (FindNextFileW(hFind, &find_data));

    FindClose(hFind);
#else
    // Linux/macOS: scan directory for tool_*.so / tool_*.dylib files
    std::string ext;
#ifdef __APPLE__
    ext = ".dylib";
#else
    ext = ".so";
#endif

    try {
        namespace fs = std::filesystem;
        if (!fs::exists(plugin_dir) || !fs::is_directory(plugin_dir)) {
            std::cout << "[Plugin] No plugins found in '" << plugin_dir << "/'" << std::endl;
            return plugins;
        }

        for (const auto& entry : fs::directory_iterator(plugin_dir)) {
            if (!entry.is_regular_file()) continue;
            std::string filename = entry.filename().string();

            // Match tool_*.so or tool_*.dylib
            if (filename.rfind("tool_", 0) != 0 && filename.find(ext) == std::string::npos) continue;
            if (filename.substr(filename.size() - ext.size()) != ext) continue;

            auto tool = load_plugin(entry.path().string());
            if (tool) {
                plugins.push_back(std::move(tool));
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "[Plugin] Error scanning directory '" << plugin_dir << "': " << e.what() << std::endl;
    }
#endif

    if (!plugins.empty()) {
        std::cout << "[Plugin] " << std::to_string(plugins.size()) << " plugin(s) loaded successfully." << std::endl;
    }

    return plugins;
}

} // namespace agent
