# TUI 統一輸出改造

## 背景

目前專案中 `std::cout` 散落在多個原始檔中，缺乏統一的輸出控制點。這使得：

- 難以集中管理輸出格式（如 ANSI 色彩、緩衝行為）
- 無法方便地切換/重導向輸出目標（如寫入檔案、關閉除錯訊息）
- 多執行緒環境下缺少互斥保護，可能產生交錯輸出

## 目標

建立 `TUI::out(const char* fmt, ...)` 作為所有終端輸出的統一入口，取代各處的 `std::cout`。

## 現有狀況

### 已有基礎

- `include/tui.h` — TUI class，包含 ANSI escape code 常數、色彩工具函式（`color()`、`bold()`、`dim()`）、游標控制（`cls()`、`flush()`）等
- `include/logger.h` — 結構化日誌系統（LOG_DEBUG/INFO/WARN/ERROR），走 `std::cout` / `std::cerr`

### std::cout 散佈位置

#### 已遷移至 TUI::out()（Phase 2 完成）

| 檔案 | 用途 |
|------|------|
| `src/main.cpp` | 歡迎訊息、狀態列、提示字元、Token 統計、Spinner |
| `src/agent.cpp` | 計畫執行結果輸出 |
| `src/command_dispatcher.cpp` | 命令分派換行 |
| `src/llm_client.cpp` | Stream 完成後換行 |
| `src/reply_mode_command.cpp` | Reply mode 狀態顯示 |
| `src/safety_guard.cpp` | 危險操作確認提示 |

#### 仍使用 std::cout（Phase 2 未完成）

##### src/main.cpp
    
    | 行號 | 用途 | 說明 |
|------|------|------|
| 92 | Status bar renderer | `std::cout << bar.str();` 狀態列渲染器 |
| 298 | Ctrl-C newline | `std::cout << std::endl;` Ctrl-C 後確保換行 |
| 304 | Input newline | `std::cout << std::endl;` 輸入後確保換行 |

##### src/key_watcher.cpp（~5 處）

| 行號 | 用途 | 說明 |
|------|------|------|
| 446 | Cursor positioning | `TUI::cursor_pos()` 游標定位 + prompt 輸出 |
| 459 | Draw area cursor | 繪製區域游標定位 |
| 467 | Prompt display | 提示字串顯示 |
| 476 | Draw text | 繪製文字輸出 |
| 645 | Command echo | 命令回顯 |

##### src/command_handlers.cpp（1 處）

| 行號 | 用途 | 說明 |
|------|------|------|
| 660 | Output display | `std::cout << out.str();` 輸出顯示 |

##### src/project_summary/summary_tool.cpp（3 處）

| 行號 | 用途 | 說明 |
|------|------|------|
| 594 | Summary header | `=== 專案摘要 ===` 標題 |
| 599 | Folder listing | 📁 資料夾列表輸出 |
| 606 | Total stats | 📊 統計資訊輸出 |

##### src/tool.cpp（~12 處）

| 行號 | 用途 | 說明 |
|------|------|------|
| 26-28 | JSON object printing | `print_json_value()` 物件鍵值輸出 |
| 36 | String value | 字串值輸出（含引號） |
| 41 | Array size | 陣列大小顯示 |
| 47-51 | Array items | 陣列項目逐行輸出 |
| 55 | JSON dump | `args.dump()` 完整 JSON 輸出 |
| 58 | Boolean value | bool 值輸出 |
| 61 | Null value | null 值輸出 |
| 67 | Scalar value | 純量值輸出 |
| 71, 80 | ANSI color | `TUI::ANSI_BRIGHT_BLACK` / `TUI::ANSI_RESET` 色彩控制 |

##### src/terminal_command_detector.cpp（2 處）

| 行號 | 用途 | 說明 |
|------|------|------|
| 239 | Detection output | `std::cout << buffer;` 偵測結果輸出 |
| 262 | Warning message | ⚠ 終端命令警告訊息 |

##### src/user_reply.cpp（~7 處）

| 行號 | 用途 | 說明 |
|------|------|------|
| 68-74 | Status header | `[User Reply]` 狀態列 + Tool 名稱 |
| 81 | Error preview | 錯誤預覽輸出 |
| 85-90 | Command display | 命令顯示 + Reply 提示 |

##### tools/file_tool.cpp（~30+ 處）

| 行號範圍 | 用途 | 說明 |
|----------|------|------|
| 163-206 | read_files echo | 回顯讀取參數（paths/files/directory/glob/outline） |
| 381-410 | write_files info | 寫入檔案資訊 + diff 輸出 |
| 692 | edit_file diff | Diff 輸出 |
| 1020-1049 | edit_files echo | 回顯編輯操作（replace/insert/delete）+ ANSI 色彩 |
| 1079 | edit_files diff | Diff 輸出 |
| 1300-1328 | create_file info | 建立檔案資訊 + diff 輸出 |
| 1400-1462 | insert_content_at_line info | 插入內容資訊 + diff 輸出 |
| 1657 | delete_files diff | Diff 輸出 |

##### src/tui.cpp（2 處 — TUI::out() 內部實作）

| 行號 | 用途 | 說明 |
|------|------|------|
| 38 | `TUI::out()` impl | `std::cout << buf << std::flush;` out() 的實際寫入 |
| 45 | `TUI::err()` impl | `std::cout << text << std::flush;` err() 的實際寫入 |

> **注意**：`src/tui.cpp` 中的 `std::cout` 是 `TUI::out()` / `TUI::err()` 的底層實作，屬於統一出口本身，不需遷移。

## 設計

### TUI::out() API

```cpp
// include/tui.h 新增
static void out(const char* fmt, ...);           // printf-style，寫入 stdout
static void err(const char* fmt, ...);           // printf-style，寫入 stderr（錯誤/警告）
static void set_output_enabled(bool enabled);    // 開關輸出
```

### 設計決策

| 項目 | 決定 | 理由 |
|------|------|------|
| 緩衝策略 | 每次 `out()` 自動 flush | 終端互動需要即時可見，避免訊息延遲 |
| 執行緒安全 | 內部加 mutex | 防止多執行緒交錯輸出 |
| 開關控制 | `set_output_enabled()` | 方便測試/批次模式時靜音 |
| 與 logger 關係 | **不取代** logger | logger 走結構化日誌（含時間戳、等級），TUI::out 走直接輸出。logger 內部仍用 std::cout/std::cerr |

### 執行緒安全架構

```
TUI::out(fmt, ...)
    ├── mutex lock
    ├── va_list 展開 → vsnprintf → buffer
    ├── std::cout << buffer << std::flush
    └── mutex unlock
```

## 實作步驟

### Phase 1: 核心實作

- [x] Step 1.1 — `include/tui.h`：宣告 `TUI::out()`、`TUI::err()`、`set_output_enabled()`，新增必要 member（mutex、enabled flag）
- [x] Step 1.2 — `src/tui.cpp`：實作 `TUI::out()`、`TUI::err()`、`set_output_enabled()`

### Phase 2: 遷移 std::cout → TUI::out()

- [x] Step 2.1 — `src/main.cpp`：大部分 `std::cout` 已替換為 `TUI::out()`（歡迎訊息、狀態列、提示字元、Token 統計、Spinner）
- [x] Step 2.2 — `src/agent.cpp`：替換 `std::cout`
- [x] Step 2.3 — `src/command_dispatcher.cpp`：替換 `std::cout`
- [x] Step 2.4 — `src/llm_client.cpp`：替換 `std::cout`
- [x] Step 2.5 — `src/reply_mode_command.cpp`：替換 `std::cout`
- [x] Step 2.6 — `src/safety_guard.cpp`：替換 `std::cout`

### Phase 2 未完成項目（仍需處理）

以下檔案仍有 `std::cout` 未遷移至 `TUI::out()`，共 **~60+ 處**：

| 檔案 | 數量 | 說明 |
|------|------|------|
| `src/main.cpp` | ~3 | Status bar renderer、Ctrl-C/Input newline |
| `src/key_watcher.cpp` | ~5 | Cursor positioning、prompt display、draw text、command echo |
| `src/command_handlers.cpp` | 1 | Output display |
| `src/project_summary/summary_tool.cpp` | 3 | Summary header、folder listing、total stats |
| `src/tool.cpp` | ~12 | `print_json_value()` JSON 列印函式（含 ANSI color） |
| `src/terminal_command_detector.cpp` | 2 | Detection output、warning message |
| `src/user_reply.cpp` | ~7 | User reply status header、error preview、command display |
| `tools/file_tool.cpp` | ~30+ | read_files/write_files/edit_file/edit_files/create_file diff echo |

> **注意**：`src/tui.cpp` 中的 `std::cout` 是 `TUI::out()` / `TUI::err()` 的底層實作，屬於統一出口本身，不需遷移。

### Phase 3: TUI 內部方法改用 out()

- [x] Step 3.1 — `tui.h` 中的 inline 方法（`cls()`、`flush()`、`clearScreen()` 等）**維持直接操作 std::cout**，避免遞迴呼叫。這是有意設計。
- [x] Step 3.2 — `include/tui.h` **保留** `#include <iostream>`，因為低層級 inline 方法需要它

## 注意事項

1. **TUI inline 方法避免遞迴**：`cls()`、`flush()` 等 inline 函式若改用 `TUI::out()` 會造成遞迴。這些低層級方法應直接操作 `std::cout`，或讓 `TUI::out()` 內部呼叫它們而非相反。
2. **CMakeLists.txt**：需將 `src/tui.cpp` 加入編譯來源列表
3. **Windows UTF-8**：`main.cpp` 中的 `SetConsoleCP(65001)` 等初始化保持不變，在 `TUI::out()` 呼叫之前完成
4. **logger.h 不改造**：logger 是結構化日誌系統，與 TUI::out 的用途不同，維持現狀
