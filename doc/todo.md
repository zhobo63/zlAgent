# ZL Agent 優化清單


## [ ] llm供應介面

doc/llm_provider.md

## [ ] telegram User confirm

## [ ] KeyWatcher readline

- refresh speed
- 每次按键都重新计算候选项
- 即使前缀没有变化（比如移动光标），也会重新扫描目录和排序
- 完整重绘而非增量更新
- ANSI 字符串频繁分配

[ ] 增量渲染（Incremental Redraw）
[ ] build_candidates() 重複掃描目錄
[ ] read_key_thread() 推送按鍵時，通知 condition_variable

## [ ] file_utils.cpp

| # | 問題 | 影響 |
|---|------|------|
| 7 | `match_glob()` 缺少 `**` 遞迴萬用字元 | pattern `src/**/*.cpp` 無法正確匹配 |

| # | 問題 | 影響 |
|---|------|------|
| 9 | `Base64Decode()` 無效輸入靜默錯誤 | 非 Base64 字元被忽略，無警告 |
| 10 | `apply_blocks()` 排序不穩定 | 同位置 block 順序不確定 |
