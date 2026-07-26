# ZL Agent 優化清單


## [ ] llm_provider 供應介面

doc/llm_provider.md

## [ ] telegram User confirm

## [ ] KeyWatcher readline

- refresh speed
- 每次按键都重新计算候选项
- 即使前缀没有变化（比如移动光标），也会重新扫描目录和排序
- ANSI 字符串频繁分配

[ ] build_candidates() 重複掃描目錄
[ ] read_key_thread() 推送按鍵時，通知 condition_variable
[ ] `History::add()` 改用 deque | 歷史記錄操作從 O(n) → O(1) |

## [ ] file_utils.cpp

| # | 問題 | 影響 |
|---|------|------|
| 7 | `match_glob()` 缺少 `**` 遞迴萬用字元 | pattern `src/**/*.cpp` 無法正確匹配 |

| # | 問題 | 影響 |
|---|------|------|
| 9 | `Base64Decode()` 無效輸入靜默錯誤 | 非 Base64 字元被忽略，無警告 |
| 10 | `apply_blocks()` 排序不穩定 | 同位置 block 順序不確定 |

## [ ] use edit_files

tmp/ 測試生成一個100行左右的代碼 並測試使用 edit_files 工具`同時`修改 5處地方 修改的操作需要符合邏輯 使用工具的不同功能 只能使用一次工具 修改後回報工具使用狀況
測試完後 把你測試的步驟 包含生成的 原本內容 修改指令 寫到 tmp/test_edit_files.md 
tmp/test_multi_edit.cpp 測試使用 edit_files 工具`同時`修改 5處地方

## [ ] use read_files

# File outline for multi_agent.h

 14 namespace agent
 22  class SubAgent
 26  ├ get_name()
 27  ├ description()
 30  ├ execute()
 33  └ run_loop() [virtual]
 45  class SubAgentLLM : SubAgent
 52  ├ set_workdir()
 56  ├ set_system_prompt()
 58  └ run_loop() [override]
 67  class SubAgentNet : SubAgent
 69  ├ struct Config
 76  │ ├ load()
 78  │ └ save()
 84  ├ start()
 87  ├ stop()
 90  ├ is_connected()
 94  ├ ask_confirm()
104  ├ connection_loop() [private]
107  ├ heartbeat_loop() [private]
110  ├ handle_message() [private]
114  └ send_confirm_request() [private]

## [ ] tools/ 工具優化

tools/code_search_tool.cpp | tools/file_tool.cpp | tools/fs_tool.cpp | tools/memory_tool.cpp | tools/overview_tool.cpp | tools/rag_tool.cpp | tools/skill_tool.cpp | tools/terminal_tool.cpp

### 🔴 Bug / 邏輯錯誤

| # | 問題 | 檔案 | 影響 |
|---|------|------|------|
| ~~1~~ | ~~`is_in_comment()` bug — `line[1]` 永遠檢查第二個字元，不是 `c` 的下一個~~ | fs_tool.cpp L644-652 | ✅ 已修正：改用索引 i 跳過空白後再檢查 line[i+1] |
| ~~2~~ | ~~`dname.substr(0, 1)` 空字串越界風險~~ | overview_tool.cpp L174、L268 | ✅ 已修正：改用 dname.empty() \\|\\| dname[0] == '.'

### 🟡 重複程式碼（可抽取共用）

| # | 問題 | 檔案 | 影響 |
|---|------|------|------|
| 3 | `#include "safety_guard.h"` 出現兩次 | terminal_tool.cpp L4-5、fs_tool.cpp L4-5 | 編譯警告風險 |
| 4 | `trim()` 函式在多個檔案重複定義（fs_tool.cpp 內就有 3 個！） | fs_tool.cpp L589/704/982、code_search_tool.cpp L26-30 | 維護成本 |
| 5 | JSON 參數驗證 boilerplate 每個工具都重複 | 所有 tools/*.cpp | 可抽取到 Tool 基底類別 |
| 6 | `execute_shell_command()` 在 fs_tool.cpp 和 overview_tool.cpp 各定義一次 | fs_tool.cpp L23、overview_tool.cpp L452 | 維護成本 |

### 🟠 效能問題

| # | 問題 | 檔案 | 影響 |
|---|------|------|------|
| ~~7~~ | ~~`std::regex` 在迴圈中重複編譯（parse_gcc_style / parse_msvc_style）~~ | fs_tool.cpp L946、L962 | ✅ 已修正：改用 `static const std::regex` |
| ~~8~~ | ~~`ReadFileTool::execute()` 讀取檔案兩次（先計行數再讀內容）~~ | file_tool.cpp L57-66、L88 | ✅ 已修正：一次讀入 vector<string> |
| ~~9~~ | ~~`ReadFilesTool::process_file()` 同樣讀取兩次~~ | file_tool.cpp L244-251、L270 | ✅ 已修正：一次讀入 vector<string> |

### 🔵 程式碼品質 / 設計建議

| # | 問題 | 檔案 | 影響 |
|---|------|------|------|
| 10 | description 寫了 timeout 30秒但實際未實作（popen 無 timeout） | terminal_tool.cpp L24 | 描述不正確，長時間命令會卡住 |
| 11 | 全域變數 `g_skill_registry` 應改為建構子注入 | skill_tool.cpp L13 | 設計問題 |
| 12 | `is_ignored_dir()` 線性搜尋可改用 C++20 `starts_with` | code_search_tool.cpp L53-60 | 效能微調 |
| 13 | LOG_DEBUG 印出空字串（應印出實際的 ignored_dirs） | code_search_tool.cpp L141 | 除錯資訊不完整 |
| 14 | `is_build_path()` 和 `is_project_source()` 重複維護 excluded_paths | overview_tool.cpp L110-117、L136-142 | 維護成本 |
| 15 | `count_files()` 定義了但似乎沒有被呼叫 | overview_tool.cpp L148-182 | 死碼 |
| 16 | `ScoredSession` 結構體定義在 execute() 內部，應移到 class level | memory_tool.cpp L56-59 | 可讀性 |

# File outline for multi_agent.h

 14 namespace agent
 22  class SubAgent
 26  ├ get_name()
 27  ├ description()
 30  ├ execute()
 33  └ run_loop() [virtual]
 45  class SubAgentLLM : SubAgent
 52  ├ set_workdir()
 56  ├ set_system_prompt()
 58  └ run_loop() [override]
 67  class SubAgentNet : SubAgent
 69  ├ struct Config
 76  │ ├ load()
 78  │ └ save()
 84  ├ start()
 87  ├ stop()
 90  ├ is_connected()
 94  ├ ask_confirm()
104  ├ connection_loop() [private]
107  ├ heartbeat_loop() [private]
110  ├ handle_message() [private]
114  └ send_confirm_request() [private]
