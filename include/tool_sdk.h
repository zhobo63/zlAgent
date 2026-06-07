#pragma once
// ============================================================================
// ZL Agent - Tool SDK
// ============================================================================
// 外部工具开发只需包含此头文件，实现以下接口即可被动态加载。
//
// 编译方式（Windows MSVC）:
//   cl /LD /I..\include custom_tool.cpp /Fe:tool_mytool.dll
//
// 编译方式（MinGW g++）:
//   g++ -shared -O2 -I../include custom_tool.cpp -o tool_mytool.dll
//
// 将生成的 .dll 放入 zlagent.exe 同级的 plugins/ 目录即可自动加载。
// ============================================================================

#include <string>

#ifdef _WIN32
    #ifdef TOOL_SDK_EXPORTS
        #define TOOL_API __declspec(dllexport)
    #else
        #define TOOL_API __declspec(dllimport)
    #endif
#else
    #define TOOL_API __attribute__((visibility("default")))
#endif

// ---------------------------------------------------------------------------
// 工具信息结构（插件必须导出）
// ---------------------------------------------------------------------------
extern "C" {

    // 返回工具名称，如 "format_code"
    TOOL_API const char* get_tool_name();

    // 返回工具描述，发送给 LLM 的说明文字
    TOOL_API const char* get_tool_description();

    // 返回 JSON Schema 字符串，定义工具的参数结构
    TOOL_API const char* get_tool_parameters_schema();

    // 执行工具。json_args 是 LLM 传入的参数（JSON 格式）
    // 返回值通过 result_buffer 传出，buffer_size 指定缓冲区大小
    // 返回实际写入的字节数；出错返回 -1
    TOOL_API int execute_tool(const char* json_args, char* result_buffer, int buffer_size);

} // extern "C"
