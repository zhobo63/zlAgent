# ZL Agent - C++ Code Assistant

基于本地 LLM 的 C++ 代码助手，使用 LM Studio 作为推理引擎。

## 架构

```
├── include/          # 头文件
│   ├── agent.h       # Agent 核心（推理循环）
│   ├── llm_client.h  # LM Studio HTTP Client
│   ├── memory.h      # 对话记忆管理
│   ├── tool.h        # 工具基类 + 注册表
│   └── tools.h       # 工具工厂函数
├── src/              # 核心实现
│   ├── main.cpp      # 交互式 CLI 入口
│   ├── agent.cpp     # 推理循环实现
│   ├── llm_client.cpp# WinHTTP 客户端
│   ├── tool.cpp      # 注册表实现
│   └── memory.cpp    # 滑动窗口记忆
├── tools/            # 工具实现
│   ├── file_tool.cpp       # 文件读写
│   ├── terminal_tool.cpp   # 终端命令执行（编译/运行）
│   └── code_search_tool.cpp# 代码正则搜索
└── CMakeLists.txt    # 构建配置
```

## 前置条件

- **C++17** 编译器 (MSVC / MinGW)
- **CMake 3.16+**
- **LM Studio** 本地运行，端口 `1234`（默认）

## 构建

```bash
cd F:\hg\zlagent
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022"   # MSVC
# 或 cmake .. -G "MinGW Makefiles"    # MinGW
cmake --build . --config Release
```

## 使用

1. 启动 LM Studio，加载模型，开启本地服务器（默认 `http://127.0.0.1:1234`）
2. 运行 `zlagent.exe`
3. 输入你的需求，Agent 会自动调用工具完成任务

### 示例对话

```
You: 帮我写一个 C++ 快速排序并编译测试

Agent: [调用 write_file 创建 quicksort.cpp]
      [调用 execute_command 执行 g++ 编译]
      [调用 execute_command 运行程序]
      已完成。快速排序实现已保存到 quicksort.cpp，编译通过，输出正确。
```

## 工具列表

| 工具 | 功能 |
|------|------|
| `read_file` | 读取文件内容 |
| `write_file` | 写入/创建文件 |
| `execute_command` | 执行 shell 命令（编译、运行等） |
| `search_code` | 正则搜索代码 |

## 扩展

### 方式一：内置工具（编译进主程序）

1. 继承 `Tool` 类，实现纯虚函数
2. 在 `tools.h` 声明工厂函数
3. 在 `main.cpp` 注册到 Agent

```cpp
class MyNewTool : public Tool { ... };
ToolPtr create_my_new_tool() { return std::make_shared<MyNewTool>(); }
// main.cpp: ag.add_tool(agent::create_my_new_tool());
```

### 方式二：外部插件（动态 DLL，无需重新编译主程序）

只需实现 `tool_sdk.h` 定义的 **4 个 C 接口**，编译为 DLL 放入 `plugins/` 目录即可：

```cpp
#define TOOL_SDK_EXPORTS
#include "tool_sdk.h"

extern "C" {
    TOOL_API const char* get_tool_name()               { return "my_tool"; }
    TOOL_API const char* get_tool_description()        { return "My custom tool."; }
    TOOL_API const char* get_tool_parameters_schema()  { return "{...}"; }  // JSON Schema
    TOOL_API int execute_tool(const char* json_args, char* buf, int size) {
        snprintf(buf, size, "Hello from plugin!");
        return strlen(buf);
    }
}
```

**编译 & 部署：**

```bash
# MSVC
cl /LD /DTOOL_SDK_EXPORTS /I..\include my_tool.cpp /Fe:tool_mytool.dll

# MinGW
g++ -shared -O2 -DTOOL_SDK_EXPORTS -I../include my_tool.cpp -o tool_mytool.dll

# 复制到 plugins/ 目录（必须命名为 tool_*.dll）
copy tool_mytool.dll ..\..\plugins\
```

Agent 启动时会自动扫描 `plugins/tool_*.dll`，无需修改主程序代码。

**示例插件：** `examples/custom_tool/` — clang-format 代码格式化工具
