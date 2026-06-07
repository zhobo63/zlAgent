# ZL Agent - Multi-Language AI Coding Agent

基於本地 LLM 的多語言 AI Coding Agent，支援 C++/JS/TS/Python/Rust/Go/Java/HTML/CSS 等語言。使用 LM Studio 作為推理引擎，具備任務規劃、自我反思糾錯、多 Agent 協作等進階能力。

## 架構

```
├── include/              # 頭文件
│   ├── agent.h           # Agent 核心（推理循環 + 高級管道）
│   ├── llm_client.h      # LM Studio HTTP Client (httplib.h)
│   ├── memory.h          # 對話記憶管理（滑動視窗 + 摘要壓縮）
│   ├── tool.h            # 工具基類 + ToolRegistry
│   ├── tools.h           # 內建工具工廠函數
│   ├── plugin_loader.h   # 動態外掛載入器
│   ├── local_tools.h     # 本地工具自動發現（多語言）
│   ├── config.h          # INI 配置解析 + Config 結構
│   ├── safety_guard.h    # 安全防護（危險操作確認/路徑白名單/輸入過濾）
│   ├── system_prompt.h   # 多語言系統提示詞提供者
│   ├── language_detector.h # 自動語言偵測（副檔名掃描）
│   ├── task_planner.h    # 🆕 任務規劃（拆解複雜任務為子步驟）
│   ├── self_reflector.h  # 🆕 自我反思/糾錯（品質審查 + 自動重試）
│   └── multi_agent.h     # 🆕 多 Agent 協作（Coder / Reviewer / Tester）
├── src/                  # 核心實現
│   ├── config.cpp        # INI 配置解析 + Config 載入
│   └── safety_guard.cpp  # 安全防護實現
│   ├── system_prompt.cpp # 多語言系統提示詞實現
│   ├── language_detector.cpp # 自動語言偵測實現
│   ├── main.cpp          # 互動式 CLI 入口
│   ├── agent.cpp         # 推理循環 + Plan→Execute 高級管道
│   ├── llm_client.cpp    # HTTP Client 實現
│   ├── tool.cpp          # ToolRegistry 實現
│   ├── memory.cpp        # 滑動視窗記憶 + summarize
│   ├── plugin_loader.cpp # DLL/SO 動態載入
│   ├── local_tools.cpp   # PATH 掃描 + LocalExecutableTool（多語言）
│   ├── task_planner.cpp  # 🆕 LLM-driven 計劃生成 + replan
│   ├── self_reflector.cpp# 🆕 品質審查 + 反饋重試
│   └── multi_agent.cpp   # 🆕 SubAgent 路由 + Coder→Reviewer pipeline
├── tools/                # 工具實現
│   ├── file_tool.cpp         # read_file / write_file / edit_file
│   ├── terminal_tool.cpp     # execute_command（跨平台）
│   ├── code_search_tool.cpp  # search_code（遞迴 regex 搜尋）
│   └── fs_tool.cpp           # create_directory / delete_path / copy_path / move_path / find_files / get_file_outline / grep_with_context / run_build / git_status / git_diff / fetch_url
├── plugins/              # 外掛目錄（tool_*.dll / tool_*.so）
├── zlagent.ini           # 全域設定檔
└── CMakeLists.txt        # 構建配置 (C++17, CMake 3.16+)
```

## 前置條件

- **C++17** 編譯器 (MSVC / MinGW)
- **CMake 3.16+**
- **LM Studio** 本地運行，端口 `1234`（預設）

## 構建

```bash
cd F:\hg\zlagent
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022"   # MSVC
# 或 cmake .. -G "MinGW Makefiles"    # MinGW
cmake --build . --config Release
```

## 使用

1. 啟動 LM Studio，載入模型，開啟本地伺服器（預設 `http://127.0.0.1:1234`）
2. 運行 `zlagent.exe`
3. 輸入你的需求，Agent 會自動調用工具完成任務

### 示例對話

```
You: 幫我寫一個 C++ 快速排序並編譯測試

Agent: [Planner] Generating task plan...
       [Planner] Plan for: Write quicksort in C++, compile and test
         Step 1: Create a C++ file with quicksort implementation
         Step 2: Compile the code using g++ or clang++
         Step 3: Run the program to verify output

       [Planner] Executing Step 1: Create a C++ file...
       [Tool] write_file ...
       [Reflection] Step passed quality check.

       [Planner] Executing Step 2: Compile the code...
       [Tool] execute_command g++ -o quicksort quicksort.cpp ...
       [Reflection] Step passed quality check.

       ## Task Completed
       **Goal:** Write quicksort in C++, compile and test
       **Steps executed:** (all completed successfully)
```

## 工具列表（17 個內建）

| 工具 | 功能 |
|------|------|
| `read_file` | 讀取文件內容 |
| `write_file` | 寫入/創建文件（全量覆蓋） |
| `edit_file` | 精準編輯（查找 old_text → 替換 new_text） |
| `list_directory` | 列出目錄中的文件和資料夾 |
| `execute_command` | 執行 shell 命令（編譯、運行等，跨平台） |
| `search_code` | 遞迴 regex 搜尋代碼 |
| `create_directory` | 創建目錄及父目錄（mkdir -p） |
| `delete_path` | 刪除文件或目錄（rm -rf） |
| `copy_path` | 複製文件或目錄（遞迴） |
| `move_path` | 移動或重新命名文件/目錄 |
| `find_files` | 按 glob 模式搜尋文件路徑 |
| `get_file_outline` | 提取大文件的符號摘要（C/C++/Python/JS） |
| `grep_with_context` | regex 搜尋 + 前後 N 行上下文 |
| `run_build` | 編譯專案並解析錯誤訊息（g++/clang++/MSVC） |
| `git_status` | 結構化 git status 輸出 |
| `git_diff` | unified diff 輸出 |
| `fetch_url` | 抓取網頁內容轉 Markdown |

## 全域設定（INI）

所有功能開關和全域參數統一由 `zlagent.ini` 控制，無需修改代碼或編譯。

```ini
; zlagent.ini — ZL Agent Configuration File

[llm]
url = http://127.0.0.1:1234
temperature = 0.2
max_tokens = 4096

[memory]
max_messages = 50

[agent]
max_iterations = 10
; Auto-detect language from source file extensions in current directory.
auto_detect_language = true
; Language: multi (default), cpp, js, ts, python, rust, go, java
language = multi
; Optional: path to an external system prompt file (.md / .txt). Overrides built-in.
prompt_file =

; ── Advanced Feature Toggles ───────────────────────────────
[features]
task_planning = true
self_reflection = true
multi_agent = false
max_reflection_retries = 2

[plugins]
directory = plugins

[local_tools]
enabled = true

; ── Safety Settings ────────────────────────────────────────
[safety]
dangerous_tool_confirmation = true   ; 危險工具確認（rm -rf/del /f 等需用戶確認）
path_whitelist =                     ; 允許操作的目錄列表（逗號分隔，空=無限制）
skill_content_check = true           ; SKILL.md 內容檢查
input_filter = true                  ; 輸入過濾（防止提示詞注入攻擊）
```

| Section | Key | 類型 | 預設值 | 說明 |
|---------|-----|------|--------|------|
| `[llm]` | `url` | string | `http://127.0.0.1:1234` | LLM 伺服器位址 |
| | `temperature` | float | `0.2` | 生成溫度 |
| | `max_tokens` | int | `4096` | 最大輸出 token 數 |
| `[memory]` | `max_messages` | int | `50` | 對話記憶容量 |
| `[agent]` | `max_iterations` | int | `10` | 推理循環最大迭代次數 |
| | `auto_detect_language` | bool | `true` | 自動偵測工作目錄的程式語言 |
| | `language` | string | `multi` | 語言模式：multi/cpp/js/ts/python/rust/go/java |
| | `prompt_file` | string | *(空)* | 外部系統提示詞檔案路徑（覆蓋內建） |
| `[features]` | `task_planning` | bool | `true` | 任務規劃開關 |
| | `self_reflection` | bool | `true` | 自我反思/糾錯開關 |
| | `multi_agent` | bool | `false` | 多 Agent 協作開關 |
| | `max_reflection_retries` | int | `2` | 反思重試最大次數 |
| `[plugins]` | `directory` | string | `plugins` | 外掛目錄路徑 |
| `[local_tools]` | `enabled` | bool | `true` | 本地工具自動發現開關 |
| `[safety]` | `dangerous_tool_confirmation` | bool | `true` | 危險工具確認（rm -rf/del /f 等需用戶確認） |
| | `path_whitelist` | string | *(空)* | 允許操作的目錄列表（逗號分隔，空=無限制） |
| | `skill_content_check` | bool | `true` | SKILL.md 內容檢查（偵測可疑 shell 命令） |
| | `input_filter` | bool | `true` | 輸入過濾（防止提示詞注入攻擊） |

> **註：** 若找不到 `zlagent.ini`，Agent 會使用全部預設值並輸出提示訊息。

## 進階能力

### 🆕 任務規劃（Task Planning）

複雜任務自動拆解為有序子步驟，失敗時自動 replan：

```
用戶輸入 → LLM 生成計劃 (JSON) → 依序執行每個步驟
         ↓ (某步驟失敗)
       replan() → 帶已完成上下文重新規劃
```

### 🆕 自我反思/糾錯（Self-Reflection）

每步輸出經品質審查，發現問題自動重試：

```
執行步驟 → SelfReflector.review() → needs_correction?
    ↓ yes                    ↓ no
retry with feedback          ✓ 通過
(max 2 retries)
```

### 🆕 多 Agent 協作（Multi-Agent）

三個專職子 Agent 分工合作，Coder 任務自動走審查 pipeline：

```
MultiAgent (協調器)
├── Coder    → 寫代碼、修改文件
├── Reviewer → 審查代碼質量
└── Tester   → 編譯測試、驗證結果

路由策略: keyword-based (review→Reviewer, build/test→Tester, default→Coder)
Pipeline: Coder → Reviewer → (發現問題) → Coder fix
```

### 功能開關

所有功能開關統一由 `zlagent.ini` 的 `[features]` section 控制：

| 參數 | 預設值 | 說明 |
|------|--------|------|
| `task_planning = true` | ✅ | 任務規劃（拆解複雜任務為子步驟） |
| `self_reflection = true` | ✅ | 自我反思/糾錯（品質審查 + 自動重試） |
| `multi_agent = false` | ❌ | 多 Agent 協作（Coder / Reviewer / Tester pipeline） |
| `max_reflection_retries = 2` | 2 | 反思重試最大次數 |

> **程式化控制：** 也可通過 API 動態調整：
> ```cpp
> ag.set_task_planning(true);
> ag.set_self_reflection(true);
> ag.set_multi_agent(false);
> ag.set_max_reflection_retries(2);
> ```

## 擴展

### 方式一：內建工具（編譯進主程序）

1. 繼承 `Tool` 類，實現純虛函數
2. 在 `tools.h` 聲明工廠函數
3. 在 `main.cpp` 註冊到 Agent

```cpp
class MyNewTool : public Tool { ... };
ToolPtr create_my_new_tool() { return std::make_shared<MyNewTool>(); }
// main.cpp: ag.add_tool(agent::create_my_new_tool());
```

### 方式二：外部外掛（動態 DLL，無需重新編譯主程序）

只需實現 `tool_sdk.h` 定義的 **4 個 C 接口**，編譯為 DLL 放入 `plugins/` 目錄即可：

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

**編譯 & 部署：**

```bash
# MSVC
cl /LD /DTOOL_SDK_EXPORTS /I..\include my_tool.cpp /Fe:tool_mytool.dll

# MinGW
g++ -shared -O2 -DTOOL_SDK_EXPORTS -I../include my_tool.cpp -o tool_mytool.dll

# 複製到 plugins/ 目錄（必須命名為 tool_*.dll）
copy tool_mytool.dll ..\..\plugins\
```

Agent 啟動時會自動掃描 `plugins/tool_*.dll`，無需修改主程序代碼。

**示例外掛：** `examples/custom_tool/` — clang-format 代碼格式化工具

### 方式三：本地工具自動發現

Agent 啟動時自動掃描系統 PATH，偵測已安裝的多語言開發工具，並包裝為可調用 Tool。

**支援的工具：**

| 語言/類別 | 工具 |
|-----------|------|
| C++ | g++, clang++, cl (MSVC), cmake, make, ninja, clang-format, clang-tidy, cppcheck, gdb, lldb, vcpkg, conan |
| JavaScript/TypeScript | node, npm, npx, tsc, webpack, vite |
| Python | python3, pip3, pytest |
| Rust | cargo, rustc |
| Go | go |
| Java | javac, java, mvn, gradle |
| 通用 | git |

## 技能系統（Skill System）

技能是比工具更高層次的可複用能力單元：**指令模板 + 工具依賴 + 配置參數**。

- **內建技能**：code_review / project_setup / debug_build / refactor_helper
- **跨 Agent 相容**：自動偵測 `.claude/skills/`、`.cursor/skills/` 等格式並導入
- **動態創建**：LLM 可在對話中通過 `create_skill` 工具即時創建新技能

## 技術棧

| 組件 | 選擇 |
|------|------|
| HTTP Client | httplib.h (single-header, cross-platform) |
| JSON 解析 | nlohmann/json (header-only) |
| C++ 標準 | C++17 |
| 跨平台 | ✅ Windows / Linux / macOS |
