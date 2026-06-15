# ZL Agent - 需求規格說明書

## 1. 專案概述

ZL Agent 是一個多語言本地 AI Coding Agent，採用純 C++17 實現，透過 LM Studio 作為推理引擎。支援 C++/JS/TS/Python/Rust/Go/Java/HTML/CSS 等多種語言。Agent 具備工具調用能力，可自主讀取檔案、編寫程式碼、編譯執行、搜尋程式碼等，完成端到端的開發任務。

**核心設計目標：**
- 本地 AI 代理
- 可擴展的工具系統（內建 + 動態外掛）
- 輕量級、僅使用 httplib.h single-header HTTP 函式庫 + C++ STL

---

## 2. 架構總覽

```
┌──────────────────────────────────────────────────────┐
│                      main.cpp                         │
│                 (互動式 CLI 入口)                       │
│                                                      │
│  ┌─────────┐   ┌──────────┐   ┌───────────┐         │
│  │ Agent   │──▶│ LLM      │◀──│ Memory    │         │
│  │ (推理循環)│   │ Client   │   │ (對話記憶) │         │
│  └────┬────┘   └──────────┘   └───────────┘         │
│       │                                              │
│  ┌────▼──────────────────────────────────────┐       │
│  │            ToolRegistry                    │       │
│  │  ┌────────┐ ┌────────┐ ┌──────────┐      │       │
│  │  │內建工具 │ │外掛工具 │ │本地工具  │      │       │
│  │  └────────┘ └────────┘ └──────────┘      │       │
│  └───────────────────────────────────────────┘       │
│                                                      │
│  ┌──────────────────────────────────────────────┐    │
│  │           Advanced Pipeline                   │    │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────────┐ │    │
│  │  │TaskPlanner│ │SelfReflect│ │MultiAgent   │ │    │
│  │  │(任務規劃) │ │(自我反思) │ │(多Agent協作) │ │    │
│  │  └──────────┘ └──────────┘ └──────────────┘ │    │
│  └──────────────────────────────────────────────┘    │
│                                                      │
│  ┌──────────────────────────────────────────────┐    │
│  │           Configuration                       │    │
│  │  ┌──────────┐ ┌───────────┐ ┌────────────┐  │    │
│  │  │INI Parser│ │SystemPrompt│ │LangDetector│  │    │
│  │  │(zlagent.ini)│(多語言提示詞)│ (自動偵測) │  │    │
│  │  └──────────┘ └───────────┘ └────────────┘  │    │
│  └──────────────────────────────────────────────┘    │
│                                                      │
│  ┌──────────────────────────────────────────────┐    │
│  │           SafetyGuard                         │    │
│  │  ┌───────────┐ ┌──────────┐ ┌────────────┐  │    │
│  │  │DangerousOp│ │PathWhitelist│ │InputFilter│  │    │
│  │  │(危險操作確認)│ (路徑白名單) │ (輸入過濾) │  │    │
│  │  └───────────┘ └──────────┘ └────────────┘  │    │
│  └──────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────┘
```

---

## 3. 模組需求

### 3.1 LLM Client (`llm_client.h/cpp`)

| 項目 | 要求 |
|------|------|
| 協定 | OpenAI-compatible HTTP API（相容 LM Studio） |
| 傳輸層 | httplib.h (single-header, cross-platform) |
| 預設位址 | `http://127.0.0.1:1234` |
| 訊息格式 | ChatMessage (role, content, name) |
| 工具定義 | ToolDefinition (name, description, parameters_schema) |
| 回應解析 | 支援 tool_calls 提取（id, name, arguments JSON） |
| 參數 | temperature (預設 0.2), max_tokens (預設 4096) |

### 3.2 Memory (`memory.h/cpp`)

| 項目 | 要求 |
|------|------|
| 策略 | 滑動視窗（Sliding Window），保留最近 N 條訊息 |
| 預設容量 | 50 條訊息 |
| Token-aware | 同時根據 token 預算截斷，不只看訊息數量 |
| 功能 | add / get_messages / clear / set_system_prompt / summarize / get_token_count / needs_compression |

### 3.2.1 TokenCounter (`token_counter.h/cpp`)

**核心概念：** 輕量 token 估算器。非精確 tokenizer（如 tiktoken），但提供合理的 heuristics 近似值，用於 Memory 的壓縮觸發、預算管理等。

| 項目 | 要求 |
|------|------|
| `estimate(text)` | 估算單一字串的 token 數 |
| `estimate_message(msg)` | 估算 ChatMessage 的 token 數（含 role overhead） |
| `estimate_conversation(messages)` | 估算整段對話的總 token 數 |
| UTF-8 處理 | **先解碼 codepoint，再對完整字元分類** — 不可逐 byte 分類 |

**字元權重規則：**

| 字元類型 | 權重（tokens/char） | 說明 |
|----------|-------------------|------|
| ASCII whitespace / punctuation | 0.25 | 空白、逗號、句點等，通常與周圍文字共享 token |
| ASCII letters (a-z, A-Z) | 0.25 | GPT-style BPE 平均 ~4 bytes/token |
| ASCII digits (0-9) | 0.30 | 數字常與周圍文字共享 token |
| ASCII symbols / operators | 0.60 | 程式碼中的運算子，每個可能獨立成 token |
| 2-byte UTF-8 (Latin ext, Greek, Cyrillic) | 0.50 | 擴展拉丁、希臘文、西里爾文等 |
| 3-byte UTF-8 (CJK) | 1.50 | 中日韓字元，BPE tokenizer 通常 1-2 tokens/char |
| 4-byte UTF-8 (emoji, rare chars) | 2.00 | Emoji、罕見字元 |
| Fallback | 1.00 | 無法分類的字元 |

**TokenUsage 追蹤：**

```cpp
struct TokenUsage {
    size_t prompt_tokens = 0;       // 本次請求的 prompt token 數
    size_t completion_tokens = 0;   // LLM 回傳的 completion token 數
    size_t total_tokens = 0;        // 累計總使用量
};
```

- `TokenCounter::from_api_usage(json)` — 從 API 回應的 `usage` 欄位提取真實 token 數據
- Agent 每次 LLM 呼叫後更新 TokenUsage，供 CLI `/stats` 命令查詢

**增量計算：**

- Memory 在 `add()` 時累加 token 計數，不重新遍歷整個 history
- `summarize()` 後更新緩存值
- 提供 `get_cached_token_count()` 避免 O(n) 重算

### 3.3 Agent (`agent.h/cpp`)

| 項目 | 要求 |
|------|------|
| 推理循環 | 發送對話 → LLM 回傳 → 如有 tool_calls 則執行並回傳結果 → 重複直到無調用或達到最大迭代次數 |
| 安全限制 | 最大迭代次數 = 10（防止無限循環） |
| 工具註冊 | `add_tool(ToolPtr)` 動態新增 |
| 系統提示 | 可配置 — INI `language`（multi/cpp/js/ts/python/rust/go/java）+ `prompt_file` 外部覆蓋；內建多語言提示詞 |

### 3.4 Tool System (`tool.h/cpp`)

**Tool 基底類別要求：**

| 虛函式 | 說明 |
|--------|------|
| `name()` | 工具名稱（唯一標識） |
| `description()` | 人類可讀描述，發送給 LLM |
| `parameters_schema()` | JSON Schema 字串，定義參數結構 |
| `execute(json_args)` | 執行工具，回傳結果字串 |

**ToolRegistry：**
- 註冊、查詢、按名稱執行工具
- 將 ToolPtr 轉換為 ToolDefinition（供 LLM 使用）

### 3.5 內建工具 (`tools/`)

#### read_file
| 項目 | 要求 |
|------|------|
| 參數 | `path` (string, required) |
| 功能 | 讀取檔案完整內容並回傳文字 |
| 錯誤處理 | 檔案不存在時回傳錯誤訊息 |

#### write_file
| 項目 | 要求 |
|------|------|
| 參數 | `path` (string), `content` (string)，均為 required |
| 功能 | 建立或覆蓋寫入檔案內容 |
| 回傳值 | 成功時報告寫入位元組數 |

#### edit_file
| 項目 | 要求 |
|------|------|
| 參數 | `path` (string, required), `old_text` (string, required), `new_text` (string, required) |
| 功能 | 在檔案中精確查找 old_text 並替換為 new_text，支援多行比對 |
| 唯一性檢查 | old_text 必須在檔案中只出現一次，否則報錯要求提供更多上下文 |

#### list_directory
| 項目 | 要求 |
|------|------|
| 參數 | `path` (string, required) |
| 功能 | 列出目錄中的檔案和資料夾，回傳結構化檢視（資料夾帶 `/` 後綴） |
| Windows 實現 | FindFirstFileA / FindNextFileA |

#### execute_command
| 項目 | 要求 |
|------|------|
| 參數 | `command` (string, required), `cwd` (string, optional) |
| 功能 | 執行 shell 命令，捕獲 stdout + stderr |
| Windows 實現 | CreateProcess + pipe，30 秒逾時 |
| Linux/macOS | ✅ popen / pclose，支援 cwd 切換目錄 |

#### search_code
| 項目 | 要求 |
|------|------|
| 參數 | `pattern` (regex, required), `directory` (optional), `file_pattern` (glob, optional) |
| 功能 | 遞迴搜尋目錄中比對正規表示式的檔案行，回傳 `檔案:行號:內容` |
| 限制 | 最多回傳 50 條結果 |
| Windows 實現 | FindFirstFileA / FindNextFileA |
| Linux/macOS | ✅ dirent.h + sys/stat.h 遞迴遍歷，glob 比對 (*.cpp, *.h) |

#### create_directory
| 項目 | 要求 |
|------|------|
| 參數 | `path` (string, required) |
| 功能 | 建立目錄及所有必要的父目錄（類似 mkdir -p） |
| 回傳值 | 成功時報告 "Successfully created directory 'path'" |
| 錯誤處理 | 路徑已存在且非空目錄時報錯 |
| 跨平台 | std::filesystem::create_directories (C++17) |

#### delete_path
| 項目 | 要求 |
|------|------|
| 參數 | `path` (string, required) |
| 功能 | 刪除檔案或目錄（目錄遞迴刪除，類似 rm -rf） |
| 回傳值 | 成功時報告 "Successfully deleted 'path'" |
| 錯誤處理 | 路徑不存在時報錯 |
| 跨平台 | std::filesystem::remove / remove_all (C++17) |

#### copy_path
| 項目 | 要求 |
|------|------|
| 參數 | `source_path` (string, required), `destination_path` (string, required) |
| 功能 | 複製檔案或目錄（目錄遞迴複製） |
| 回傳值 | 成功時報告 "Successfully copied 'source' to 'dest'" |
| 錯誤處理 | 來源路徑不存在時報錯；目標已存在時報錯 |
| 跨平台 | std::filesystem::copy / copy_recursive (C++17) |

#### move_path
| 項目 | 要求 |
|------|------|
| 參數 | `source_path` (string, required), `destination_path` (string, required) |
| 功能 | 移動或重新命名檔案/目錄（同目錄=改名） |
| 回傳值 | 成功時報告 "Successfully moved 'source' to 'dest'" |
| 錯誤處理 | 來源路徑不存在時報錯；目標已存在時報錯 |
| 跨平台 | std::filesystem::rename (C++17) |

#### find_files
| 項目 | 要求 |
|------|------|
| 參數 | `glob` (string, required), `directory` (string, optional) |
| 功能 | 遞迴搜尋目錄中比對 glob 模式的檔案路徑（如 `**/*.cpp`、`*.h`） |
| 回傳值 | 回傳比對的檔案路徑列表，每行一條 |
| 限制 | 最多回傳 50 條結果 |
| 跨平台 | std::filesystem::recursive_directory_iterator + glob matching (C++17) |

#### get_file_outline
| 項目 | 要求 |
|------|------|
| 參數 | `path` (string, required), `start_line` (int, optional), `end_line` (int, optional) |
| 功能 | 提取大檔案的符號摘要，回傳函式/類別/結構體/命名空間的列表及行號。適合快速了解檔案結構而不讀取全文 |
| 回傳值 | 符號列表：`行號: 型別 名稱`（如 `15: class MyClass`、`42: void foo()`） |
| 支援語言 | C/C++ (class, struct, namespace, function)、Python (def, class)、JavaScript/TypeScript (function, class) |
| 錯誤處理 | 檔案不存在時回傳錯誤訊息；start_line/end_line 用於指定範圍，省略則掃描全文 |

#### grep_with_context
| 項目 | 要求 |
|------|------|
| 參數 | `regex` (string, required), `path` (string, optional), `before` (int, optional, default 0), `after` (int, optional, default 0) |
| 功能 | 在檔案中搜尋比對正規表示式的行，並回傳前後 N 行上下文。類似 grep -B/-A |
| 回傳值 | `檔案:行號:內容` 格式，比對行前加 `>` 標記，上下文行前加 `-` 標記 |
| 限制 | 最多回傳 50 條結果 |
| 跨平台 | std::regex + ifstream（純 C++17） |

#### run_build
| 項目 | 要求 |
|------|------|
| 參數 | `command` (string, required), `cwd` (string, optional) |
| 功能 | 執行編譯命令，解析編譯器輸出中的錯誤/警告訊息，提取 `file:line:column: message` 格式的結構化結果 |
| 回傳值 | 編譯成功時回傳 "Build succeeded"；失敗時列出所有錯誤和警告（檔案、行號、訊息） |
| 支援編譯器 | g++, clang++, MSVC (cl.exe) — 自動識別輸出格式 |
| 跨平台 | 複用 execute_command 的底層實現 + 正規表示式解析 |

#### git_status
| 項目 | 要求 |
|------|------|
| 參數 | `path` (string, optional, default ".") |
| 功能 | 執行 `git status --porcelain`，回傳結構化的修改狀態列表 |
| 回傳值 | 每行一條：`狀態 檔案路徑`（如 `M src/main.cpp`、`?? new_file.txt`） |
| 錯誤處理 | 非 git 儲存庫時回傳錯誤訊息 |

#### git_diff
| 項目 | 要求 |
|------|------|
| 參數 | `path` (string, optional), `staged` (bool, optional, default false) |
| 功能 | 執行 `git diff`（或 `--cached`），回傳結構化 diff 輸出 |
| 回傳值 | 標準 unified diff 格式，包含檔案頭、行號範圍、增刪標記 |
| 錯誤處理 | 非 git 儲存庫時回傳錯誤訊息 |

#### fetch_url
| 項目 | 要求 |
|------|------|
| 參數 | `url` (string, required) |
| 功能 | 抓取網頁內容，自動轉換為 Markdown 格式。去除 HTML 標籤，保留文字結構和連結 |
| 回傳值 | 轉換後的 Markdown 文字 |
| 限制 | 最大回應體 100KB；逾時 15 秒 |
| 跨平台 | httplib.h HTTP GET + 簡易 HTML-to-Markdown 轉換器 |

### 3.6 外掛系統 (`plugin_loader.h/cpp`, `tool_sdk.h`)

**PluginLoader：**
- 掃描指定目錄（預設 `plugins/`），載入比對 `tool_*.dll` 的動態函式庫
- Windows: LoadLibrary / GetProcAddress
- Linux/macOS: ✅ dlopen(RTLD_NOW) / dlsym，目錄掃描使用 `<filesystem>`（tool_*.so / tool_*.dylib）
- 將 DLL 包裝為 PluginTool，實現 Tool 介面

**Tool SDK（外部外掛開發）：**

外掛只需匯出 4 個 C 函式：

```cpp
extern "C" {
    const char* get_tool_name();              // 工具名稱
    const char* get_tool_description();       // 描述
    const char* get_tool_parameters_schema(); // JSON Schema
    int execute_tool(const char* json_args,   // 執行
                     char* result_buffer,
                     int buffer_size);
}
```

**範例外掛：** `examples/custom_tool/` — clang-format 程式碼格式化工具

### 3.6.1 INI 配置系統 (`config.h/cpp`, `zlagent.ini`)

| 項目 | 要求 |
|------|------|
| IniParser | 輕量 INI 解析器，支援 `[section] key = value`、註解（`;` / `#`）、空白行 |
| Config | 結構化配置：LLM URL/參數、Memory 容量、Agent 迭代次數、功能開關、外掛目錄、本地工具開關 |
| zlagent.ini | 全域設定檔，找不到時使用預設值並輸出提示 |

**INI 配置項：**

| Section | Key | 類型 | 預設值 | 說明 |
|---------|-----|------|--------|------|
| `[llm]` | `url` | string | `http://127.0.0.1:1234` | LLM 伺服器位址 |
| | `temperature` | float | `0.2` | 生成溫度 |
| | `max_tokens` | int | `4096` | 最大輸出 token 數 |
| `[memory]` | `max_messages` | int | `50` | 對話記憶容量 |
| `[agent]` | `max_iterations` | int | `10` | 推理循環最大迭代次數 |
| | `language` | string | `multi` | 語言模式：multi/cpp/js/ts/python/rust/go/java |
| | `auto_detect_language` | bool | `true` | 自動偵測工作目錄的程式語言 |
| | `prompt_file` | string | *(空)* | 外部系統提示詞檔案路徑（覆蓋內建） |
| `[features]` | `task_planning` | bool | `true` | 任務規劃開關 |
| | `self_reflection` | bool | `true` | 自我反思/糾錯開關 |
| | `multi_agent` | bool | `false` | 多 Agent 協作開關 |
| | `max_reflection_retries` | int | `2` | 反思重試最大次數 |
| `[plugins]` | `directory` | string | `plugins` | 外掛目錄路徑 |
| `[local_tools]` | `enabled` | bool | `true` | 本地工具自動發現開關 |
| `[safety]` | `dangerous_tool_confirmation` | bool | `true` | 危險工具確認開關 |
| | `path_whitelist` | string | *(空)* | 允許的目錄列表（逗號分隔，空=無限制） |
| | `skill_content_check` | bool | `true` | SKILL.md 內容檢查開關 |
| | `input_filter` | bool | `true` | 輸入過濾開關 |

### 3.6.2 多語言系統提示詞 (`system_prompt.h/cpp`)

| 項目 | 要求 |
|------|------|
| SystemPromptProvider::get(language) | 根據語言標識返回對應的內建系統提示詞 |
| 支援語言 | multi（預設）/ cpp / js / ts / python / rust / go / java |
| 優先級鏈 | `prompt_file` 外部檔案 > 內建語言提示詞 > multi 回退 |

### 3.6.3 自動語言偵測 (`language_detector.h/cpp`)

| 項目 | 要求 |
|------|------|
| detect_directory(path) | 遞迴掃描目錄，根據副檔名統計各語言文件數量 |
| extension_to_language(ext) | 將副檔名映射到語言標識（.cpp→cpp, .js→js, .ts→ts, .py→python, .rs→rust, .go→go, .java→java） |
| 單一語言 | 只有一種語言 → 返回該語言 |
| 多語言混合 | 主導語言 >60% → 返回主導語言；否則返回 `multi` |
| 無源碼文件 | 返回空字串，由 INI 預設值接管 |

### 3.7 本地工具發現 (`local_tools.h/cpp`)

| 項目 | 要求 |
|------|------|
| 功能 | 掃描系統 PATH，自動偵測已安裝的多語言開發工具（C++/JS/TS/Python/Rust/Go/Java/Web） |
| LocalToolInfo | name, display_name, path, description, version |
| resolve_path() | Windows: SearchPathW；Linux: which / PATH 遍歷 |
| get_version() | 執行 `--version` / `-version` 取得版本字串 |
| LocalExecutableTool | 將發現的本地工具包裝為 Agent Tool，LLM 可直接調用 |

**已知工具清單（多語言）：**

| 語言/類別 | 工具 |
|-----------|------|
| C++ | g++, clang++, cl (MSVC), cmake, make, ninja, clang-format, clang-tidy, cppcheck, gdb, lldb, vcpkg, conan |
| JavaScript/TypeScript | node, npm, npx, tsc, webpack, vite |
| Python | python3, pip3, pytest |
| Rust | cargo, rustc |
| Go | go |
| Java | javac, java, mvn, gradle |
| 通用 | git |

### 3.7 SafetyGuard（安全防護）(`safety_guard.h/cpp`)

| 項目 | 要求 |
|------|------|
| **危險工具確認** | `execute_command` 偵測破壞性命令模式（rm -rf、del /f、fork bomb 等），要求用戶輸入 `y` 確認 |
| **路徑白名單** | INI `[safety] path_whitelist` 設定允許操作的目錄，超出範圍時工具直接拒絕執行 |
| **SKILL.md 內容檢查** | 掃描技能指令中的可疑模式（rm -rf /、curl\|bash、eval()、fork bomb、chmod 777 等），返回警告列表 |
| **輸入過濾** | 偵測用戶輸入中的提示詞注入關鍵字（[SYSTEM]、ignore previous instructions、jailbreak 等），拒絕處理 |

**INI 配置項：**

```ini
[safety]
dangerous_tool_confirmation = true   ; 危險工具確認開關
path_whitelist =                     ; 允許的目錄列表（逗號分隔，空=無限制）
skill_content_check = true           ; SKILL.md 內容檢查開關
input_filter = true                  ; 輸入過濾開關
```

**串接點：**
- `execute_command` → `is_command_dangerous()` + `confirm_dangerous_operation()`
- `delete_path` / `write_file` / `edit_file` → `is_path_allowed()`
- CLI 互動循環 → `is_prompt_injection()`

---

### 3.8 技能系統 (`skill_system.h/cpp`, `zlagent/skills/`)

**核心概念：**

技能（Skill）是比工具更高層次的可複用能力單元。一個技能 = **指令模板 + 工具依賴 + 配置參數**。LLM 根據用戶意圖自動選擇合適的技能，技能再調用底層工具完成任務。

**與工具的區別：**
- **工具（Tool）**：原子操作 — `read_file`、`execute_command`
- **技能（Skill）**：複合工作流 — 「代碼審查」、「專案初始化」、「除錯編譯錯誤」

---

#### 跨 Agent 相容格式

ZL Agent 的技能系統設計為與主流 AI Coding Assistant 的 Skill 格式相容，讓技能可以在不同 Agent 之間共享。

**標準目錄結構（與其他 Agent 一致）：**

```
zlagent/skills/
├── code_review/
│   └── SKILL.md          # 技能的指令模板（Markdown 格式）
├── project_setup/
│   └── SKILL.md
├── debug_build/
│   └── SKILL.md
└── refactor_helper/
    └── SKILL.md
```

**SKILL.md 標準格式：**

每個技能目錄下必須包含一個 `SKILL.md`，使用 Markdown 格式描述技能的行為。

```markdown
# Code Review Skill

## Description
Review C/C++ code for bugs, style issues, and best practices.

## When to Use
When the user asks you to review, audit, or inspect existing source code.

## Instructions
1. Read the target file(s) using `read_file`
2. Search for common patterns using `grep_with_context`:
   - Memory safety issues (use-after-free, buffer overflow)
   - Style violations (naming conventions, formatting)
   - Performance anti-patterns
3. Report findings in a structured format: severity, location, description, suggestion

## Tools Required
- read_file
- grep_with_context

## Configuration
max_files: 5
check_style: true
```

**SKILL.md 結構規範：**

| Section | 必填 | 說明 |
|---------|------|------|
| `# <Skill Name>` | ✅ | 技能標題（作為技能名稱） |
| `## Description` | ✅ | 技能的簡短描述，發送給 LLM 用於選擇 |
| `## When to Use` | ✅ | 觸發條件 — 什麼情況下應該使用此技能 |
| `## Instructions` | ✅ | 步驟化指令模板，LLM 按此執行工作流 |
| `## Tools Required` | ❌ | 依賴的工具列表（用於驗證） |
| `## Configuration` | ❌ | 配置參數（key: value 格式） |

**相容性映射表：**

| Agent | 專案路徑 | 全域路徑 | ZL Agent 對應 |
|-------|----------|----------|---------------|
| **ZL Agent** | `zlagent/skills/` | N/A | ✅ 原生支援 |
| Claude Code | `.claude/skills/` | `~/.claude/skills/` | 🔄 可導入 |
| Cursor | `.cursor/skills/` | `~/.cursor/skills/` | 🔄 可導入 |
| Gemini CLI | `.gemini/skills/` | `~/.gemini/skills/` | 🔄 可導入 |
| GitHub Copilot | `.github/skills/` | `~/.copilot/skills/` | 🔄 可導入 |
| Windsurf | `.windsurf/skills/` | `~/.codeium/windsurf/skills/` | 🔄 可導入 |
| Codex | `.agents/skills/` | `~/.agents/skills/` | 🔄 可導入 |

**自動偵測與自動導入：**

ZL Agent 啟動時會自動掃描當前工作目錄和全域路徑，偵測其他 AI Coding Assistant 的技能目錄並自動導入。

**自動偵測順序：**

1. **專案層級** — 掃描當前工作目錄下的 `.claude/skills/`、`.cursor/skills/`、`.gemini/skills/`、`.github/skills/`、`.windsurf/skills/`、`.agents/skills/`
2. **全域層級** — 掃描 `~/.claude/skills/`、`~/.cursor/skills/`、`~/.copilot/skills/`、`~/.codeium/windsurf/skills/`、`~/.agents/skills/`
3. **ZL Agent 原生** — 最後載入 `zlagent/skills/`

**自動導入流程：**

```
啟動 → 掃描相容技能目錄 → 發現 SKILL.md → 解析並驗證依賴
→ 無衝突則自動註冊 → 有衝突則提示用戶選擇（覆蓋 / 跳過）
```

**手動控制選項：**

```bash
# 禁用自動導入，僅使用原生技能
zlagent --no-auto-import-skills

# 指定額外的技能來源目錄
zlagent --extra-skill-path /path/to/more/skills/

# 強制重新掃描（忽略快取）
zlagent --rescan-skills
```

**衝突處理策略：**

| 情況 | 行為 |
|------|------|
| 同名稱技能，內容相同 | 跳過（不重複註冊） |
| 同名稱技能，內容不同 | 優先使用原生 `zlagent/skills/`，其他來源標記為 `[imported]` |
| 工具依賴缺失 | 記錄警告但不阻止啟動，該技能不可用但可被發現 |

**日誌輸出範例：**

```
Loading skills...
  ✓ code_review          (zlagent/skills/code_review/)
  ✓ project_setup        (zlagent/skills/project_setup/)
  ⚠ debug_build          [imported from .claude/skills/] — tool 'run_build' not found, disabled
  ✓ refactor_helper      (zlagent/skills/refactor_helper/)
  + code_review          [imported from .cursor/skills/] — skipped (duplicate)
4 skills loaded, 1 imported (disabled), 1 duplicate skipped
```

---

#### SkillRegistry API

- `register_skill(SkillPtr)` — 註冊技能
- `get_skills()` → 返回所有技能的 SkillDefinition（供 LLM 選擇）
- `find_skill(name)` → 按名稱查找技能
- `invoke_skill(name, context)` → 注入技能的 Instructions，執行工作流
- `import_skill(source_path)` → 從其他 Agent 的技能目錄導入
- `auto_detect_and_import()` → 自動偵測並導入相容格式的技能
- `create_skill(name, description, when_to_use, instructions, tools_required, config)` → **動態創建技能**（寫入 SKILL.md + 立即註冊）
- `delete_skill(name)` → 刪除技能（移除目錄 + 從 Registry 中卸載）

#### SkillLoader

- **原生掃描**：掃描 `zlagent/skills/` 目錄，載入每個子目錄下的 `SKILL.md`
- **跨 Agent 掃描**：自動偵測 `.claude/skills/`、`.cursor/skills/`、`.gemini/skills/`、`.github/skills/`、`.windsurf/skills/`、`.agents/skills/`（專案層級 + 全域層級）
- **解析**：從 Markdown 結構提取 Description、Instructions、Tools Required、Configuration
- **驗證**：檢查工具依賴是否滿足（缺失時記錄警告，技能標記為 disabled）
- **去重**：同名稱技能只註冊一次，優先使用原生來源
- **版本管理**：支援技能版本管理（從 SKILL.md 的 frontmatter 或註解中提取）

#### Agent 集成流程

```
用戶請求 → LLM 判斷是否需要技能 → SkillRegistry 選擇技能
→ 注入 Instructions → Agent 使用技能的 Tools Required 執行工作流
→ 返回結果
```

#### 內建技能（預設）

| 技能 | 說明 | 依賴工具 |
|------|------|----------|
| `code_review` | C/C++ 代碼審查（記憶體安全、風格、效能） | read_file, grep_with_context |
| `project_setup` | 初始化新專案（建立目錄結構、CMakeLists.txt、.gitignore） | create_directory, write_file, list_directory |
| `debug_build` | 除錯編譯錯誤（執行 build → 解析錯誤 → 定位問題 → 建議修復） | run_build, read_file, grep_with_context |
| `refactor_helper` | 重構輔助（提取函數、重命名變量、拆分檔案） | read_file, edit_file, find_files, get_file_outline |

#### 技能擴展方式

1. **內建技能**：編譯進主程序，在 `main.cpp` 中註冊
2. **外部技能目錄**：放入 `zlagent/skills/` 的子目錄（含 SKILL.md），啟動時自動載入
3. **從其他 Agent 導入**：使用 `--import-skill` 命令導入 `.claude/skills/`、`.cursor/skills/` 等格式的技能
4. **程式化註冊**：通過 API 動態新增（類似工具外掛）
5. **Agent 動態創建** — LLM 可在對話中自動生成新技能並立即生效

#### Agent 動態創建技能

LLM 可以通過 `create_skill` 工具在運行時動態創建新技能，無需重新啟動。

**create_skill 工具：**

| 參數 | 類型 | 必填 | 說明 |
|------|------|------|------|
| `name` | string | ✅ | 技能名稱（小寫英文 + 底線） |
| `description` | string | ✅ | 技能的簡短描述 |
| `when_to_use` | string | ✅ | 觸發條件說明 |
| `instructions` | string | ✅ | 步驟化指令模板 |
| `tools_required` | array[string] | ❌ | 依賴的工具名稱列表 |
| `config` | object | ❌ | 配置參數（key-value） |

**創建流程：**

```
LLM 調用 create_skill(name, description, when_to_use, instructions, ...)
→ SkillRegistry 驗證名稱唯一性 + 工具依賴
→ 寫入 zlagent/skills/<name>/SKILL.md
→ 自動註冊到 SkillRegistry（立即可用）
→ 返回創建結果
```

**使用場景：**

1. **用戶要求新能力**：「幫我創建一個技能，專門用來檢查 C++ 記憶體洩漏」
2. **LLM 自我優化**：發現重複性工作流時，主動提議將其封裝為技能
3. **專案特定技能**：根據當前專案特性動態生成專屬技能

**對話範例：**

```
You: 幫我創建一個檢查 C++ 記憶體洩漏的技能

Agent: [調用 create_skill]
  name: memory_leak_check
  description: "Check C++ code for potential memory leaks"
  when_to_use: "When the user asks to check for memory leaks or resource management issues"
  instructions: |
    1. Read the target file using read_file
    2. Search for new/delete patterns with grep_with_context
    3. Check for RAII violations (raw pointers without smart pointer wrappers)
    4. Report potential leak locations with severity levels
  tools_required: [read_file, grep_with_context]

Skill 'memory_leak_check' created successfully at zlagent/skills/memory_leak_check/SKILL.md
The skill is now available for use.
```

**安全限制：**

- `tools_required` 中的工具必須已註冊，否則技能標記為 disabled
- 創建的 SKILL.md 會經過基本的安全檢查（不允許包含惡意 shell 命令）
- 用戶可通過 `--no-dynamic-skills` 禁用動態創建功能

#### ⚠️ 安全警告

> Agent skills can include prompt injections, tool poisoning, hidden malware payloads, or unsafe data handling patterns. Always review the code and use skills at your own discretion.
>
> - ZL Agent 在載入技能時會檢查 `Tools Required` 是否合法
> - 建議用戶手動審查 SKILL.md 內容後再啟用
> - 未來可加入技能簽名驗證機制

#### 🗺️ 技能系統實作路線圖

| Phase | 範圍 | 交付物 |
|-------|------|--------|
| **Phase 1** | SkillRegistry + SkillLoader（基礎框架） | `skill_system.h/cpp`、SKILL.md 解析器、工具依賴驗證 |
| **Phase 2** | 內建技能 | code_review / project_setup / debug_build / refactor_helper 的 SKILL.md + 註冊邏輯 |
| **Phase 3** | 跨 Agent 相容導入 | 自動偵測 `.claude/skills/`、`.cursor/skills/` 等目錄，衝突處理 |
| **Phase 4** | Agent 動態創建技能 | `create_skill` / `delete_skill` 工具，LLM 運行時生成技能 |

---

## 4. CLI 互動 (`main.cpp`)

| 項目 | 要求 |
|------|------|
| 啟動流程 | 載入 INI 配置 → **自動偵測語言**（如開啟）→ 設定系統提示詞 → 註冊內建工具 → 載入外掛 → 發現本地工具 → 進入互動循環 |
| 輸入方式 | stdin 逐行讀取（getline） |
| 退出命令 | `quit` / `exit` / EOF |
| 輸出格式 | `Agent: <回應內容>` |
| CLI 選項 | `--no-auto-import-skills`、`--extra-skill-path`、`--rescan-skills`、`--no-dynamic-skills` |

---

## 5. 建構系統 (`CMakeLists.txt`)

| 項目 | 要求 |
|------|------|
| C++ 標準 | C++17 |
| 最低 CMake | 3.16 |
| Windows | httplib.h (single-header, no link required) |
| Linux/macOS | pthread + dl |
| 目標 | `zlagent` 可執行檔 |

---

## 6. 非功能性需求

| 項目 | 要求 |
|------|------|
| 第三方依賴 | **httplib.h** (single-header HTTP client) |
| JSON 解析 | ✅ **nlohmann/json** (single-header, header-only，零執行時依賴) |
| 跨平台 | ✅ Windows / Linux / macOS（全部實現） |
| 編碼 | UTF-8-BOM |

### 6.1 安全機制需求

| 能力 | 說明 | 優先級 |
|------|------|:-----:|
| **最大迭代限制** | 防止無限循環 | ✅ (10次) |
| **危險工具確認** | `delete_path`、`execute_command`（含 rm/del）等破壞性操作前，要求用戶輸入 `y` 確認 | ✅ `SafetyGuard::confirm_dangerous_operation()` |
| **路徑白名單** | 可配置允許操作的目錄範圍，超出範圍時拒絕執行 | ✅ `SafetyGuard::is_path_allowed()` + INI `path_whitelist` |
| **SKILL.md 內容檢查** | 解析技能指令時，偵測可疑 shell 命令模式（如 `rm -rf /`、`curl | bash`）並警告 | ✅ `SafetyGuard::check_skill_content()` |
| **輸入過濾** | 對用戶輸入進行基本清洗，防止提示詞注入（如檢測 `[SYSTEM]`、`IGNORE PREVIOUS INSTRUCTIONS` 等關鍵字） | ✅ `SafetyGuard::is_prompt_injection()` |

### 6.2 測試需求

| 類型 | 範圍 | 工具/框架 |
|------|------|----------|
| **單元測試** | LLM Client 回應解析、Memory 滑動視窗、Tool execute 邏輯、SkillRegistry CRUD | Google Test (gtest) |
| **整合測試** | Agent 推理循環（LLM → Tool → Result）、外掛載入與執行、技能導入流程 | gtest + mock LLM server |
| **跨平台驗證** | Windows / Linux / macOS 的檔案操作、命令執行、外掛載入 | CI (GitHub Actions) |

---

## 🤖 AI Agent 核心功能全景圖

### 1️⃣ 推理引擎（大腦）
| 能力 | 說明 | 你的專案狀態 |
|------|------|:---:|
| **LLM 調用** | 與語言模型互動，取得推理結果 | ✅ `llm_client` (httplib.h) |
| **串流輸出 (SSE)** | 逐字回傳，降低首字延遲 | ✅ `chat_stream` / `run_stream` |
| **多模型支援** | 動態切換模型，持久化到 INI | ✅ `LLMClient::set_model()` / `list_models()` (GET /v1/models), `/model` 互動式選號 + IniParser::update_key() 寫回 zlagent.ini, `[llm] model = local` config

### 2️⃣ 記憶系統（上下文）
| 能力 | 說明 | 你的專案狀態 |
|------|------|:---:|
| **短期記憶** | 滑動視窗保留最近對話 | ✅ `memory` (50條) |
| **長期記憶** | 持久化儲存，跨會話回憶 | ✅ `LongTermMemory` — Episodic (session summaries via LLM), Semantic (structured facts auto-extracted), JSON persistence (.zlagent_memory/), RAG integration, search_memories + recall_facts tools |
| **上下文壓縮/摘要** | 對超長歷史做智慧摘要 | ✅ `Memory::summarize()` |
| **向量檢索 (RAG)** | 從知識庫中語意搜尋相關片段 | ✅ |

### 3️⃣ 工具系統（手腳）
| 能力 | 說明 | 你的專案狀態 |
|------|------|:---:|
| **檔案讀取** | `read_file` | ✅ |
| **檔案寫入** | `write_file` (全量覆蓋) | ✅ |
| **精準編輯** | `edit_file` (行級修改) | ✅ |
| **目錄瀏覽** | `list_directory` | ✅ |
| **命令執行** | `execute_command` | ✅ (跨平台: Windows CreateProcess / Linux/macOS popen) |
| **程式碼搜尋** | `search_code` (正規表示式) | ✅ (跨平台: Windows FindFirstFileA / Linux/macOS dirent.h) |
| **目錄建立** | `create_directory` (mkdir -p) | ✅ |
| **路徑刪除** | `delete_path` (rm -rf) | ✅ |
| **路徑複製** | `copy_path` (遞迴複製) | ✅ |
| **路徑移動/重新命名** | `move_path` (mv/rename) | ✅ |
| **外掛系統** | 動態載入外部工具 (.dll/.so/.dylib) | ✅ (跨平台: LoadLibrary/dlopen + GetProcAddress/dlsym) |
| **本地工具發現** | 自動偵測 PATH 中的多語言開發工具（C++/JS/TS/Python/Rust/Go/Java/Web） | ✅ |
| **自動語言偵測** | 根據工作目錄副檔名自動判斷主要程式語言 | ✅ `language_detector` (遞迴掃描 + >60% 主導規則) |
| **多語言系統提示詞** | 內建 multi/cpp/js/ts/python/rust/go/java 提示詞，支援外部檔案覆蓋 | ✅ `system_prompt` |
| **INI 全域配置** | zlagent.ini 控制所有功能開關和參數 | ✅ `config.h/cpp` |
| **按檔名 glob 搜尋檔案路徑** | `find_files` | ✅ (std::filesystem recursive_directory_iterator) |
| **取得大檔案的符號摘要（函式/類別列表）** | `get_file_outline` | ✅ (C/C++/Python/JS) |
| **帶上下文的 regex 搜尋（`-B/-A` 行數）** | `grep_with_context` | ✅ (std::regex + context lines) |
| **編譯專案並解析錯誤訊息，直接指出檔案:行號** | `run_build` | ✅ (g++/clang++/MSVC) |
| **查看修改狀態、產生 diff** | `git_status` / `git_diff` | ✅ (porcelain + unified diff) |
| **抓取網頁內容轉 Markdown，查文件/API 參考** | `fetch_url` | ✅ (httplib.h + HTML-to-MD) |

### 4️⃣ 技能系統（複合能力）
| 能力 | 說明 | 你的專案狀態 |
|------|------|:---:|
| **技能註冊** | SkillRegistry 管理可複用的工作流單元 | ✅ 已完成 |
| **技能自動載入** | 掃描 `zlagent/skills/` + 相容目錄，自動解析 SKILL.md | ✅ 已完成 |
| **跨 Agent 技能導入** | 自動偵測 `.claude/skills/`、`.cursor/skills/` 等格式並導入 | ✅ 已完成 |
| **技能依賴驗證** | 檢查 Tools Required 是否滿足，缺失時標記 disabled | ✅ 已完成 |
| **內建技能** | code_review / project_setup / debug_build / refactor_helper | ✅ 已完成 |
| **Agent 動態創建技能** | LLM 通過 `create_skill` 工具在運行時創建新技能 | ✅ 已完成 |
| **技能刪除** | 移除已註冊的技能及其 SKILL.md | ✅ 已完成 |

### 4.5️⃣ 向量檢索（RAG）
| 能力 | 說明 | 你的專案狀態 |
|------|------|:---:|
| **Embedding Provider** | LM Studio API + TF-IDF fallback，雙路徑語意嵌入 | ✅ `LLMEmbeddingProvider` (POST /v1/embeddings) + `TfidfEmbeddingProvider` (pure C++ fit/transform/L2-norm) |
| **Vector Store** | Cosine similarity top-K search + JSON 持久化 | ✅ `VectorStore::search()` partial_sort top-K, `save()/load()` via nlohmann::json |
| **Document Chunker** | 滑動視窗 + 段落感知切分，支援多檔案類型 | ✅ `DocumentChunker` (chunk_size/overlap/respect_paragraphs), 16+ extensions |
| **RAG Manager** | 知識庫導入（文件/目錄）+ 檢索整合層 | ✅ `add_document/add_file/add_directory`, TF-IDF auto-fit on first ingestion |
| **search_knowledge_base Tool** | LLM 可呼叫的工具，從知識庫語意搜尋相關片段 | ✅ `SearchKnowledgeBaseTool` (query + top_k params) |
| **INI RAG Config** | `[rag]` section：backend / model / store_path / top_k / min_score / knowledge_dirs | ✅ full config parsing in `Config::load()` |

### 5️⃣ 推理循環（決策）
| 能力 | 說明 | 你的專案狀態 |
|------|------|:---:|
| **Tool Calling 循環** | LLM → 調用工具 → 回傳結果 → 再推理 | ✅ (最多10輪) |
| **任務規劃** | 將複雜任務拆解為子步驟 | ✅ `task_planner` (LLM-driven JSON plan + replan on failure) |
| **自我反思/糾錯** | 檢查輸出品質，自動修正錯誤 | ✅ `self_reflector` (quality review → retry with feedback, max 2 retries) |
| **多 Agent 協作** | 多個 Agent 分工合作 | ✅ `multi_agent` (Coder / Reviewer / Tester pipeline + keyword routing) |

### 6️⃣ 安全與約束（護欄）
| 能力 | 說明 | 你的專案狀態 |
|------|------|:---:|
| **最大迭代限制** | 防止無限循環 | ✅ (10次) |
| **危險工具確認** | `execute_command` 偵測破壞性命令模式，要求用戶輸入 `y` 確認 | ✅ `SafetyGuard::confirm_dangerous_operation()` |
| **路徑白名單** | INI `[safety] path_whitelist` 設定允許操作的目錄，超出範圍時拒絕執行 | ✅ `SafetyGuard::is_path_allowed()` |
| **SKILL.md 內容檢查** | 掃描技能指令中的可疑模式（rm -rf /、curl\|bash、eval() 等）並警告 | ✅ `SafetyGuard::check_skill_content()` |
| **輸入過濾** | 偵測用戶輸入中的提示詞注入關鍵字，拒絕處理 | ✅ `SafetyGuard::is_prompt_injection()` |

### 7️⃣ 互動介面（溝通）
| 能力 | 說明 | 你的專案狀態 |
|------|------|:---:|
| **CLI 互動** | 命令列對話 | ✅ `main.cpp` |
| **對話中使用者命令** | `/help`, `/status`, `/skills`, `/model`, `/model-info`, `/facts`, `/sessions`, `/clear-memory`, `/save-session`, `/search-kb`, `/add-doc`, `/config` | ✅ `CommandDispatcher` + `register_command_handlers()` (12 commands, /-prefix dispatch) |
| **GUI / Web UI** | 圖形化介面 | ⬜ 未規劃 |
| **Markdown 渲染** | 格式化輸出（程式碼區塊、表格等） | ⬜ 未規劃 |
| **Auto update** | 偵測新版本/自動更新 | ⬜ 未規劃 |
