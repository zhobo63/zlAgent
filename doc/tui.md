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

| 檔案 | 用途 | 數量 |
|------|------|------|
| `src/main.cpp` | 歡迎訊息、狀態列、提示字元、Token 統計、Spinner | ~15 |
| `src/agent.cpp` | 計畫執行結果輸出 | 1 |
| `src/command_dispatcher.cpp` | 命令分派換行 | 1 |
| `src/llm_client.cpp` | Stream 完成後換行 | 1 |
| `src/reply_mode_command.cpp` | Reply mode 狀態顯示 | ~4 |
| `src/safety_guard.cpp` | 危險操作確認提示 | ~2 |

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

| 檔案 | 行號 | 用途 | 說明 |
|------|------|------|------|
| `src/main.cpp:92` | `std::cout << bar.str();` | Status bar renderer | 狀態列渲染器，目前仍直接寫入 std::cout |
| `src/terminal_command_detector.cpp:236` | `std::cout << buffer;` | 終端命令偵測器輸出 | 偵測結果輸出尚未遷移 |
| `src/user_reply.cpp:66-95` | 多處 `std::cout` | User reply 模式顯示 | 用戶回覆模式的錯誤/狀態訊息尚未遷移 |

> **注意**：Phase 2 大部分已完成，但上述三個位置仍需將 `std::cout` 改為 `TUI::out()`。

### Phase 3: TUI 內部方法改用 out()

- [x] Step 3.1 — `tui.h` 中的 inline 方法（`cls()`、`flush()`、`clearScreen()` 等）**維持直接操作 std::cout**，避免遞迴呼叫。這是有意設計。
- [x] Step 3.2 — `include/tui.h` **保留** `#include <iostream>`，因為低層級 inline 方法需要它

## 注意事項

1. **TUI inline 方法避免遞迴**：`cls()`、`flush()` 等 inline 函式若改用 `TUI::out()` 會造成遞迴。這些低層級方法應直接操作 `std::cout`，或讓 `TUI::out()` 內部呼叫它們而非相反。
2. **CMakeLists.txt**：需將 `src/tui.cpp` 加入編譯來源列表
3. **Windows UTF-8**：`main.cpp` 中的 `SetConsoleCP(65001)` 等初始化保持不變，在 `TUI::out()` 呼叫之前完成
4. **logger.h 不改造**：logger 是結構化日誌系統，與 TUI::out 的用途不同，維持現狀
