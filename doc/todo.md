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
