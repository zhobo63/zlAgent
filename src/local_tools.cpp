#include "pch.h"

#include "local_tools.h"
#include "encoding.h"
#include "wide_string.h"

#ifndef _WIN32
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace agent {

// ============================================================================
// Known tools registry
// ============================================================================

const std::map<std::string, std::string>& LocalToolDiscovery::known_tools() {
    static const std::map<std::string, std::string> tools = {
        // ── C++ Compilers ───────────────────────────────────
        {"g++",       "GNU G++ compiler. Compile and link C++ source files."},
        {"clang++",   "Clang C++ compiler (LLVM). Compile and link C++ source files with better diagnostics."},
        {"cl",        "Microsoft Visual C++ compiler (MSVC). Compile C++ on Windows."},

        // ── JavaScript / TypeScript ─────────────────────────
        {"node",      "Node.js runtime. Execute JavaScript/TypeScript code, run npm scripts."},
        {"npm",       "Node Package Manager. Install packages, run scripts for JS/TS projects."},
        {"npx",       "Node package runner. Execute npm packages without global install."},
        {"tsc",       "TypeScript compiler. Compile TypeScript to JavaScript with type checking."},

        // ── Python ──────────────────────────────────────────
        {"python3",   "Python 3 interpreter. Run Python scripts and interactive shell."},
        {"pip3",      "Python package installer (v3). Install Python packages from PyPI."},
        {"pytest",    "Pytest testing framework. Discover and run Python unit tests."},

        // ── Rust ────────────────────────────────────────────
        {"cargo",     "Rust package manager and build tool. Build, test, and publish Rust projects."},
        {"rustc",     "Rust compiler. Compile Rust source files directly."},

        // ── Go ──────────────────────────────────────────────
        {"go",        "Go programming language toolchain. Build, test, and run Go programs."},

        // ── Java ────────────────────────────────────────────
        {"javac",     "Java compiler. Compile Java source files to bytecode (.class)."},
        {"java",      "Java runtime. Execute compiled Java programs."},
        {"mvn",       "Apache Maven build tool. Build and manage Java projects with dependencies."},
        {"gradle",    "Gradle build automation. Build Java/Kotlin projects with flexible DSL."},

        // ── Web / Frontend ──────────────────────────────────
        {"webpack",   "Webpack module bundler. Bundle JavaScript modules for web applications."},
        {"vite",      "Vite build tool. Fast development server and build tool for modern web apps."},

        // ── Build Systems (cross-language) ──────────────────
        {"cmake",     "CMake build system generator. Configure and generate build files for C++ projects."},
        {"make",      "GNU Make build automation tool. Execute Makefile targets to compile projects."},
        {"ninja",     "Ninja build system. Fast build focused on speed, used by CMake as backend."},

        // ── Code Quality (cross-language) ───────────────────
        {"clang-format", "Clang code formatter. Automatically format C/C++ source code."},
        {"clang-tidy",   "Clang static analysis tool. Check for bugs and style issues in C++ code."},
        {"cppcheck",     "Static analysis tool for C/C++ code. Detects bugs and coding errors."},

        // ── Version Control ─────────────────────────────────
        {"git",       "Git version control system. Manage source code history, branches, diffs."},

        // ── Debuggers ───────────────────────────────────────
        {"gdb",       "GNU Debugger. Debug C/C++ programs with breakpoints, stack traces, variable inspection."},
        {"lldb",      "LLVM debugger. Debug programs compiled with Clang/LLVM toolchain."},

        // ── Package Managers / Other ────────────────────────
        {"vcpkg",     "Microsoft C++ package manager. Install and manage C++ libraries."},
        {"conan",     "C/C++ package manager. Manage dependencies for C++ projects."},
    };
    return tools;
}

// ============================================================================
// LocalToolDiscovery - find installed tools
// ============================================================================

std::string LocalToolDiscovery::resolve_path(const std::string& exe_name) {
#ifdef _WIN32
    wchar_t result[1024];
    std::wstring wname = agent::utf8_to_wide(exe_name);

    // SearchPathW needs a mutable buffer for the file name
    wchar_t search_buf[512];

    // Try with .exe extension first
    std::wstring wname_exe = wname + L".exe";
    wcsncpy_s(search_buf, wname_exe.c_str(), _TRUNCATE);
    if (SearchPathW(nullptr, search_buf, nullptr, 1024, result, nullptr)) {
        return agent::wide_to_utf8(result);
    }

    // Also try without .exe
    wcsncpy_s(search_buf, wname.c_str(), _TRUNCATE);
    if (SearchPathW(nullptr, search_buf, nullptr, 1024, result, nullptr)) {
        return agent::wide_to_utf8(result);
    }

    // If it's 'cl' and not found in PATH, return bare name for dev cmd env
    if (exe_name == "cl") {
        return exe_name;
    }

    return "";
#else
    // Linux/macOS: use which or check PATH manually
    std::string cmd = "which " + exe_name + " 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";

    char buffer[1024];
    if (fgets(buffer, sizeof(buffer), pipe)) {
        // Remove trailing newline
        std::string path(buffer);
        size_t nl = path.find('\n');
        if (nl != std::string::npos) path.erase(nl);
        pclose(pipe);
        return path;
    }

    pclose(pipe);
    return "";
#endif
}

std::string LocalToolDiscovery::get_version(const std::string& full_path) {
    if (full_path.empty()) return "unknown";

#ifdef _WIN32
    // Try --version first, then -version
    std::string cmd = "\"" + full_path + "\" --version 2>&1";
    if (cmd.find("cl") != std::string::npos) {
        cmd = "\"" + full_path + "\" 2>&1";  // cl doesn't support --version
		return "unknown";  // For cl, we will just return unknown for version to avoid long hangs. In practice, cl is usually run in a dev cmd environment where the version is known.
    }

    HANDLE hReadPipe, hWritePipe;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return "unknown";

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.dwFlags |= STARTF_USESTDHANDLES;

    std::string full_cmd = "cmd.exe /c " + cmd;
    std::wstring wcmd = agent::utf8_to_wide(full_cmd);

    if (!CreateProcessW(nullptr, const_cast<wchar_t*>(wcmd.c_str()), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return "unknown";
    }

    WaitForSingleObject(pi.hProcess, 5000);

    std::string raw_output;
    char buffer[4096];
    DWORD bytesRead;
    CloseHandle(hWritePipe);

    while (true) {
        if (!ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr)) break;
        if (bytesRead == 0) break;
        buffer[bytesRead] = '\0';
        raw_output += buffer;
    }

    CloseHandle(hReadPipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Convert from BIG5 to UTF-8 (cmd.exe output uses system code page).
    std::string output = agent::big5_to_utf8(raw_output);

    // Extract first line as version
    size_t nl = output.find('\n');
    if (nl != std::string::npos) output.erase(nl);
    return output.empty() ? "unknown" : output;
#else
    std::string cmd = "\"" + full_path + "\" --version 2>&1 | head -1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "unknown";

    char buffer[512];
    if (fgets(buffer, sizeof(buffer), pipe)) {
        std::string ver(buffer);
        size_t nl = ver.find('\n');
        if (nl != std::string::npos) ver.erase(nl);
        pclose(pipe);
        return ver;
    }

    pclose(pipe);
    return "unknown";
#endif
}

std::vector<LocalToolInfo> LocalToolDiscovery::discover() {
    std::vector<LocalToolInfo> found;

    for (const auto& [name, desc] : known_tools()) {
        std::string path = resolve_path(name);
        if (!path.empty()) {
            LocalToolInfo info;
            info.name = name;
            info.display_name = name + " (" + path.substr(path.find_last_of("/\\") + 1) + ")";
            info.path = path;
            info.description = desc;

            // Get version (with timeout protection)
            try {
                info.version = get_version(path);
            } catch (...) {
                info.version = "unknown";
            }

            found.push_back(info);
        }
    }

    return found;
}

LocalToolInfo LocalToolDiscovery::find_tool(const std::string& name) {
    LocalToolInfo info;
    info.name = name;

    std::string path = resolve_path(name);
    if (path.empty()) return info;  // Not found

    info.path = path;
    info.display_name = name + " (" + path.substr(path.find_last_of("/\\") + 1) + ")";

    auto& tools = known_tools();
    auto it = tools.find(name);
    if (it != tools.end()) {
        info.description = it->second;
    } else {
        info.description = "External tool: " + name;
    }

    try {
        info.version = get_version(path);
    } catch (...) {
        info.version = "unknown";
    }

    return info;
}

// ============================================================================
// LocalExecutableTool - wraps a local executable as an Agent Tool
// ============================================================================

LocalExecutableTool::LocalExecutableTool(const LocalToolInfo& info) : info_(info) {}

std::string LocalExecutableTool::name() const {
    // Sanitize tool name: replace special chars, add _tool suffix.
    std::string n = info_.name;
    std::replace(n.begin(), n.end(), '+', 'p');   // g++ -> gpp, clang++ -> clangpp
    if (n == "cl") n = "msvc_cl";                 // avoid conflict with common word
    return n + "_tool";
}

std::string LocalExecutableTool::description() const {
    std::ostringstream desc;
    desc << info_.name << " " << info_.version << ". " << info_.description
         << " Installed at: " << info_.path
         << ". Pass 'args' as command-line arguments and optionally 'cwd' for working directory.";
    return desc.str();
}

std::string LocalExecutableTool::parameters_schema() const {
    std::ostringstream schema;
    schema << "{"
        "\"type\": \"object\","
        "\"properties\": {"
            "\"args\": {\"type\": \"string\", \"description\": \"Command-line arguments for " << info_.name << "\"},"
            "\"cwd\": {\"type\": \"string\", \"description\": \"Working directory (optional)\"}"
        "},"
        "\"required\": [\"args\"]"
    "}";
    return schema.str();
}

std::string LocalExecutableTool::execute(const std::string& json_args) {
    // Extract args and cwd from JSON
    auto extract = [](const std::string& json, const std::string& key) -> std::string {
        std::string search = "\"" + key + "\"";
        auto pos = json.find(search);
        if (pos == std::string::npos) return "";

        auto q1 = json.find('"', pos + search.size() + 1);
        if (q1 == std::string::npos) return "";
        auto q2 = json.find('"', q1 + 1);
        if (q2 == std::string::npos) return "";

        std::string result;
        for (size_t i = q1 + 1; i < q2; i++) {
            if (json[i] == '\\' && i + 1 < q2) {
                char next = json[i + 1];
                switch (next) {
                    case '"': result += '"'; i++; break;
                    case '\\': result += '\\'; i++; break;
                    case 'n': result += '\n'; i++; break;
                    default: result += json[i]; break;
                }
            } else {
                result += json[i];
            }
        }
        return result;
    };

    std::string args = extract(json_args, "args");
    std::string cwd  = extract(json_args, "cwd");

    if (args.empty()) {
        return "Error: No arguments provided for " + info_.name + ".";
    }

    std::string full_cmd = "\"" + info_.path + "\" " + args;
    return run_command(full_cmd, cwd);
}

std::string LocalExecutableTool::run_command(const std::string& full_cmd, const std::string& cwd) {
#ifdef _WIN32
    HANDLE hReadPipe, hWritePipe;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        return "Error: Failed to create pipe.";
    }

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.dwFlags |= STARTF_USESTDHANDLES;

    // Set working directory
    std::wstring cwd_wide = L".";
    if (!cwd.empty()) {
        cwd_wide = agent::utf8_to_wide(cwd);
    }

    std::string full_cmd_win = "cmd.exe /c " + full_cmd;
    std::wstring wcmd = agent::utf8_to_wide(full_cmd_win);

    if (!CreateProcessW(nullptr, const_cast<wchar_t*>(wcmd.c_str()), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                        nullptr, cwd_wide.c_str(), &si, &pi)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return "Error: Failed to execute '" + info_.name + "'.";
    }

    WaitForSingleObject(pi.hProcess, 30000);  // 30s timeout

    std::string raw_output;
    char buffer[4096];
    DWORD bytesRead;
    CloseHandle(hWritePipe);

    while (true) {
        if (!ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr)) break;
        if (bytesRead == 0) break;
        buffer[bytesRead] = '\0';
        raw_output += buffer;
    }

    CloseHandle(hReadPipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Convert from BIG5 to UTF-8 (cmd.exe output uses system code page).
    std::string output = agent::big5_to_utf8(raw_output);
    return output.empty() ? "(no output)" : output;
#else
    // Linux/macOS implementation using popen
    std::string cmd = full_cmd;
    if (!cwd.empty()) {
        cmd = "cd \"" + cwd + "\" && " + cmd;
    }
    cmd += " 2>&1";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return "Error: Failed to execute '" + info_.name + "'.";
    }

    std::string output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        output += buffer;
    }

    int status = pclose(pipe);
    if (status != 0 && output.empty()) {
        return "Error: Command exited with code " + std::to_string(WEXITSTATUS(status));
    }

    return output.empty() ? "(no output)" : output;
#endif
}

// ============================================================================
// Factory function - discover and create tools
// ============================================================================

std::vector<ToolPtr> create_local_tools() {
    LocalToolDiscovery discovery;
    auto found = discovery.discover();

    std::cout << "[LocalTools] Scanning for installed tools..." << std::endl;

    std::vector<ToolPtr> tools;
    for (const auto& info : found) {
        std::cout << "  Found: " << info.name
                  << " v:" << info.version
                  << " (" << info.path << ")" << std::endl;
        tools.push_back(std::make_shared<LocalExecutableTool>(info));
    }

    if (tools.empty()) {
        std::cout << "[LocalTools] No known dev tools found in PATH." << std::endl;
        std::cout << "  Install compilers/build tools (g++, node, python3, cargo, go, etc.) to enable these features." << std::endl;
    } else {
        std::cout << "[LocalTools] Registered " << tools.size() << " local tool(s)." << std::endl;
    }

    return tools;
}

} // namespace agent
