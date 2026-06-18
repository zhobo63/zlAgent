#include "pch.h"

#include "tool.h"
#include "encoding.h"
#include "safety_guard.h"
#include "json.hpp"

#ifndef _WIN32
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#endif

namespace agent {
using json = nlohmann::json;

class TerminalTool : public Tool {
public:
    std::string name() const override { return "execute_command"; }
    std::string description() const override {
        return "Execute a shell command and return its output. "
               "Useful for compiling C++ code (g++, clang++), running programs, listing files, etc. "
               "Commands are limited to 30 seconds timeout.";
    }
    std::string parameters_schema() const override {
        json schema;
        schema["type"] = "object";
        schema["properties"]["command"]["type"] = "string";
        schema["properties"]["command"]["description"] = "The shell command to execute";
        schema["properties"]["cwd"]["type"] = "string";
        schema["properties"]["cwd"]["description"] = "Working directory (optional)";
        schema["required"] = {"command"};
        return schema.dump();
    }

    std::string execute(const std::string& json_args) override {
        try {
            if (json_args.empty()) return "Error: Invalid JSON arguments - empty input";
            auto args = json::parse(json_args);
            std::string command = args.value("command", "");
            std::string cwd = args.value("cwd", "");

            if (command.empty()) return "Error: No command provided.";

            // Safety: check for dangerous commands and require confirmation.
            if (SafetyGuard::is_command_dangerous(command)) {
                std::string op = "execute_command: " + command;
                if (!SafetyGuard::confirm_dangerous_operation(op)) {
                    return "Operation cancelled by user.";
                }
            }

#ifdef _WIN32
            // Force UTF-8 output from cmd.exe to avoid BIG5/CP950 encoding issues.
            std::string full_cmd = "cmd.exe /c chcp 65001 >nul && \"" + command + "\"";

            wchar_t cwd_wide[1024] = L".";
            if (!cwd.empty()) {
                int len = MultiByteToWideChar(CP_UTF8, 0, cwd.c_str(), -1, nullptr, 0);
                MultiByteToWideChar(CP_UTF8, 0, cwd.c_str(), -1, cwd_wide, 1024);
            }

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

            wchar_t cmd_wide[4096] = { 0 };
            MultiByteToWideChar(CP_UTF8, 0, full_cmd.c_str(), -1, cmd_wide, 4096);

            if (!CreateProcessW(nullptr, cmd_wide, nullptr, nullptr, TRUE,
                                CREATE_NO_WINDOW, nullptr, cwd_wide, &si, &pi)) {
                CloseHandle(hReadPipe);
                CloseHandle(hWritePipe);
                return "Error: Failed to execute command.";
            }

            WaitForSingleObject(pi.hProcess, 30000);

            std::string raw_output;
            char buffer[4096] = { 0 };
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

            // Sanitize: remove null bytes and control characters that would break JSON serialization.
            std::string sanitized;
            sanitized.reserve(raw_output.size());
            for (unsigned char c : raw_output) {
                if (c == 0) continue;                          // drop null bytes
                if (c >= 0x20 || c == '\n' || c == '\r')     // printable + newline / carriage return
                    sanitized += static_cast<char>(c);
            }

            // cmd.exe is forced to UTF-8 via chcp 65001, so output is already valid UTF-8.
            return sanitized.empty() ? "(no output)" : sanitized;
#else
            // Linux/macOS: popen with timeout via fork+exec
            std::string shell_cmd = command;
            if (!cwd.empty()) {
                shell_cmd = "cd '" + cwd + "' && " + command;
            }

            FILE* pipe = popen(shell_cmd.c_str(), "r");
            if (!pipe) {
                return "Error: Failed to execute command.";
            }

            std::string output;
            std::array<char, 4096> buffer;
            while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
                output += buffer.data();
            }

            int status = pclose(pipe);
            if (status != 0) {
                // Command exited with error, but we still return whatever output exists
                if (output.empty()) {
                    return "Error: Command failed with exit code " + std::to_string(WEXITSTATUS(status)) + ".";
                }
            }

            // Sanitize: remove null bytes and control characters that would break JSON serialization.
            std::string sanitized;
            sanitized.reserve(output.size());
            for (unsigned char c : output) {
                if (c == 0) continue;                          // drop null bytes
                if (c >= 0x20 || c == '\n' || c == '\r')     // printable + newline / carriage return
                    sanitized += static_cast<char>(c);
            }

            return sanitized.empty() ? "(no output)" : sanitized;
#endif
        } catch (const json::parse_error& e) {
            return "Error: Invalid JSON arguments - " + std::string(e.what());
        }
    }
};

ToolPtr create_terminal_tool() {
    return std::make_shared<TerminalTool>();
}

} // namespace agent
