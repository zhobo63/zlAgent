// ============================================================================
// 示例外部工具: clang_format
// ============================================================================
// 功能：调用 clang-format 格式化 C/C++ 代码
//
// 编译 (MSVC):
//   cl /LD /DTOOL_SDK_EXPORTS /I..\..\include tool_clang_format.cpp /Fe:tool_clang_format.dll
//
// 编译 (MinGW):
//   g++ -shared -O2 -DTOOL_SDK_EXPORTS -I../../include tool_clang_format.cpp -o tool_clang_format.dll
//
// 部署：将生成的 tool_clang_format.dll 复制到 plugins/ 目录
// ============================================================================

#define TOOL_SDK_EXPORTS
#include "tool_sdk.h"

#include <string>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

extern "C" {

TOOL_API const char* get_tool_name() {
    return "clang_format";
}

TOOL_API const char* get_tool_description() {
    return "Format C/C++ source code using clang-format. "
           "Accepts either 'code' (inline code string) or 'path' (file path to format). "
           "Returns the formatted code.";
}

TOOL_API const char* get_tool_parameters_schema() {
    return "{"
        "\"type\": \"object\","
        "\"properties\": {"
            "\"code\": {\"type\": \"string\", \"description\": \"C/C++ source code to format (inline)\"},"
            "\"path\": {\"type\": \"string\", \"description\": \"Path to C/C++ file to format\"},"
            "\"style\": {\"type\": \"string\", \"description\": \"Clang-format style: Google, LLVM, Chromium, Mozilla, WebKit (default: LLVM)\"}"
        "},"
        "\"required\": []"
    "}";
}

// Helper: extract a JSON string value by key
static std::string json_extract(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";

    auto q1 = json.find('"', pos + search.size() + 1);
    if (q1 == std::string::npos) return "";
    auto q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos) return "";

    // Unescape
    std::string result;
    for (size_t i = q1 + 1; i < q2; i++) {
        if (json[i] == '\\' && i + 1 < q2) {
            char next = json[i + 1];
            switch (next) {
                case '"': result += '"'; i++; break;
                case '\\': result += '\\'; i++; break;
                case 'n': result += '\n'; i++; break;
                case 'r': result += '\r'; i++; break;
                case 't': result += '\t'; i++; break;
                default: result += json[i]; break;
            }
        } else {
            result += json[i];
        }
    }
    return result;
}

// Helper: run clang-format on a code string via pipe
static std::string format_code(const std::string& code, const std::string& style) {
#ifdef _WIN32
    // Create pipes for stdin/stdout of child process
    HANDLE hReadPipe, hWritePipe;
    HANDLE hReadIn, hWriteIn;

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return "";
    if (!CreatePipe(&hReadIn, &hWriteIn, &sa, 0)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return "";
    }

    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdInput = hReadIn;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.dwFlags |= STARTF_USESTDHANDLES;

    std::string cmd = "clang-format -style=" + style;
    wchar_t wcmd[1024];
    MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, wcmd, 1024);

    if (!CreateProcess(nullptr, wcmd, nullptr, nullptr, TRUE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        CloseHandle(hReadIn);
        CloseHandle(hWriteIn);
        return "";
    }

    // Write code to stdin
    DWORD written;
    WriteFile(hWriteIn, code.c_str(), (DWORD)code.size(), &written, nullptr);
    CloseHandle(hWriteIn);
    CloseHandle(hReadIn);

    // Read formatted output
    std::string result;
    char buffer[4096];
    DWORD bytesRead;
    CloseHandle(hWritePipe);

    while (true) {
        if (!ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr)) break;
        if (bytesRead == 0) break;
        buffer[bytesRead] = '\0';
        result += buffer;
    }

    CloseHandle(hReadPipe);
    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return result;
#else
    // TODO: Implement for Linux/macOS using popen
    return "";
#endif
}

TOOL_API int execute_tool(const char* json_args, char* result_buffer, int buffer_size) {
    std::string args(json_args);

    std::string code = json_extract(args, "code");
    std::string path = json_extract(args, "path");
    std::string style = json_extract(args, "style");
    if (style.empty()) style = "LLVM";

    // If path provided, read file content as code
    if (!path.empty() && code.empty()) {
        std::ifstream f(path);
        if (!f.is_open()) {
            snprintf(result_buffer, buffer_size, "Error: Cannot open file '%s'", path.c_str());
            return (int)strlen(result_buffer);
        }
        std::stringstream ss;
        ss << f.rdbuf();
        code = ss.str();
    }

    if (code.empty()) {
        snprintf(result_buffer, buffer_size, "Error: No code or path provided.");
        return (int)strlen(result_buffer);
    }

    std::string formatted = format_code(code, style);

    if (formatted.empty()) {
        snprintf(result_buffer, buffer_size,
                 "Warning: clang-format not found or failed. Returning original code.\n%s",
                 code.c_str());
        int len = (int)strlen(result_buffer);
        return std::min(len, buffer_size - 1);
    }

    // If a path was given, write formatted code back to the file
    if (!path.empty()) {
        std::ofstream f(path);
        if (f.is_open()) {
            f << formatted;
            f.close();
        }
    }

    int len = (int)formatted.size();
    if (len >= buffer_size) len = buffer_size - 1;
    strncpy(result_buffer, formatted.c_str(), len);
    result_buffer[len] = '\0';

    return len;
}

} // extern "C"
