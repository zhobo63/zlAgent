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

## [ ] tools/ 工具優化

tools/code_search_tool.cpp | tools/file_tool.cpp | tools/fs_tool.cpp | tools/memory_tool.cpp | tools/overview_tool.cpp | tools/rag_tool.cpp | tools/skill_tool.cpp | tools/terminal_tool.cpp

### 🟡 重複程式碼（可抽取共用）

| # | 問題 | 檔案 | 影響 |
|---|------|------|------|
| 4 | `trim()` 函式在多個檔案重複定義（fs_tool.cpp 內就有 3 個！） | fs_tool.cpp L589/704/982、code_search_tool.cpp L26-30 | 維護成本 |
| 5 | JSON 參數驗證 boilerplate 每個工具都重複 | 所有 tools/*.cpp | 可抽取到 Tool 基底類別 |
| 6 | `execute_shell_command()` 在 fs_tool.cpp 和 overview_tool.cpp 各定義一次 | fs_tool.cpp L23、overview_tool.cpp L452 | 維護成本 |

### 🔵 程式碼品質 / 設計建議

| # | 問題 | 檔案 | 影響 |
|---|------|------|------|
| 10 | description 寫了 timeout 30秒但實際未實作（popen 無 timeout） | terminal_tool.cpp L24 | 描述不正確，長時間命令會卡住 |
| 11 | 全域變數 `g_skill_registry` 應改為建構子注入 | skill_tool.cpp L13 | 設計問題 |
| 12 | `is_ignored_dir()` 線性搜尋可改用 C++20 `starts_with` | code_search_tool.cpp L53-60 | 效能微調 |
| 14 | `is_build_path()` 和 `is_project_source()` 重複維護 excluded_paths | overview_tool.cpp L110-117、L136-142 | 維護成本 |

