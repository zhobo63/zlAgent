# 使用者回復功能 — User Reply Feature

## 1. 問題定義

目前的推理循環是「黑盒」式的——Agent 自動執行工具調用直到完成或達到最大迭代次數。使用者在過程中無法介入，只能等全部結束後才能回應。這導致以下痛點：

| 場景 | 現況 | 期望 |
|------|------|------|
| Agent 執行錯誤的工具參數 | 繼續跑下去，浪費 token | 暫停並詢問使用者是否修正 |
| 危險操作（rm -rf、del /f） | SafetyGuard 確認，但只在 execute_command | 所有工具都可選擇性確認 |
| Agent 卡住/無限循環 | 只能按 ESC 中斷 | 可以輸入指令引導方向 |
| 多步驟任務中途需要決策 | 無法介入 | 暫停並等待使用者指示 |

## 2. 功能設計

### 核心概念：`UserReplyMode` — 推理循環中的使用者介入機制

```
┌─────────────────────────────────────────────┐
│              現有流程 (黑盒)                   │
│                                             │
│  User → Agent ─[tool1]→ [tool2]→ ... → Done │
│         └── 推理循環（不可中斷） ────────────┘ │
└─────────────────────────────────────────────┘

┌─────────────────────────────────────────────┐
│              新流程 (可介入)                   │
│                                             │
│  User → Agent ─[tool1]→ ⏸ Ask? ─[tool2]→   │
│         └── 推理循環（可暫停等待回復） ────────┘ │
│                    ↑                          │
│              使用者輸入修正/確認/跳過            │
└─────────────────────────────────────────────┘
```

### 三種介入模式

| 模式 | 觸發條件 | 行為 | INI Key |
|------|---------|------|---------|
| `off` | — | 不暫停，完全自動（預設） | `user_reply_mode = off` |
| `on_error` | 工具執行失敗時 | 暫停並詢問使用者如何處理 | `user_reply_mode = on_error` |
| `always` | 每次工具調用前 | 每次都等待使用者確認 | `user_reply_mode = always` |

### 使用者回復選項

當 Agent 暫停等待回復時，提供以下快捷指令：

| 輸入 | 動作 |
|------|------|
| `y` / `yes` | 繼續執行（接受當前參數） |
| `n` / `no` | 跳過此工具調用 |
| `skip` | 跳過此步驟，繼續後續 |
| `abort` | 終止整個推理循環 |
| `edit:<json>` | 修改工具參數後執行（如 `edit:{"path":"src/other.cpp"}`） |
| 自由文字 | 作為新的使用者訊息注入對話記憶 |

## 3. 架構設計

### 新增模組：`user_reply.h/cpp`

```cpp
#pragma once
#include <string>
#include "llm_client.h"

namespace agent {

/**
 * User Reply Mode — controls when the Agent pauses for user input.
 */
enum class UserReplyMode {
    Off,        // No pause, fully automatic (default)
    OnError,    // Pause only when a tool execution fails
    Always      // Pause before every tool call
};

/**
 * Action taken by the user during an intervention prompt.
 */
enum class ReplyAction {
    Continue,   // Proceed with current parameters
    Skip,       // Skip this tool call
    Abort,      // Terminate the reasoning loop
    Edit,       // Modify arguments and execute
    Custom      // Inject custom message into conversation
};

/**
 * Result of a user reply prompt.
 */
struct UserReplyResult {
    ReplyAction action;
    std::string modified_args;   // Only valid when action == Edit
    std::string custom_message;  // Only valid when action == Custom
};

/**
 * Prompt the user for input during Agent reasoning loop.
 * Returns the result of the user's choice.
 */
UserReplyResult prompt_user_reply(
    const std::string& tool_name,
    const std::string& json_args,
    const std::string& error_message = "");

/**
 * Parse a reply mode string from config.
 */
UserReplyMode parse_reply_mode(const std::string& value);

} // namespace agent
```

### 修改 `Agent` 類別

在 `agent.h` 中新增：

```cpp
// User Reply: allow user intervention during reasoning loop
void set_user_reply_mode(UserReplyMode mode) { user_reply_mode_ = mode; }
UserReplyMode get_user_reply_mode() const { return user_reply_mode_; }
```

### 修改推理循環流程

在 `reasoning_loop()` 和 `reasoning_loop_stream()` 中，工具執行前/後加入介入點：

```cpp
// In reasoning loop, before executing each tool call:
if (user_reply_mode_ == UserReplyMode::Always) {
    auto reply = prompt_user_reply(tc.name, tc.arguments);
    switch (reply.action) {
        case ReplyAction::Abort:   return ChatResponse{"[User aborted]"};
        case ReplyAction::Skip:   continue;  // skip this tool call
        case ReplyAction::Edit:   tc.arguments = reply.modified_args; break;
        case ReplyAction::Custom: 
            memory_.add(ChatMessage{"user", reply.custom_message});
            return reasoning_loop();  // restart with new context
        default: break;  // Continue
    }
}

// Execute tool...
std::string result = registry_.execute(tc.name, tc.arguments);

// After execution, check for errors if mode is OnError:
if (user_reply_mode_ == UserReplyMode::OnError && starts_with(result, "ERROR")) {
    auto reply = prompt_user_reply(tc.name, tc.arguments, result);
    // same switch logic...
}
```

## 4. INI 配置擴充

在 `zlagent.ini` 的 `[agent]` section 中新增：

| Key | 類型 | 預設值 | 說明 |
|-----|------|--------|------|
| `user_reply_mode` | string | `off` | 使用者介入模式：`off` / `on_error` / `always` |

## 5. CLI 命令擴充

新增 `/reply-mode` 命令，允許在運行時切換模式：

```
/reply-mode          # 顯示當前模式
/reply-mode on_error # 切換為錯誤介入模式
/reply-mode always   # 切換為全程介入模式
/reply-mode off      # 關閉介入
```

## 6. UI/UX 設計

當 Agent 暫停等待使用者回復時，顯示清晰的提示：

```
┌─────────────────────────────────────────────┐
│ ⏸  [User Reply] Tool: read_file             │
│    Args: {"path": "src/main.cpp"}           │
│                                             │
│    What would you like to do?                │
│    y/yes   - Continue (default)              │
│    n/no    - Skip this tool call             │
│    skip    - Skip and continue               │
│    abort   - Stop the reasoning loop         │
│    edit:{json}  - Modify arguments           │
│    <free text> - Inject custom message       │
│                                             │
│ Reply: [cursor]                              │
└─────────────────────────────────────────────┘
```

錯誤模式下的提示：

```
┌─────────────────────────────────────────────┐
│ ⏸  [User Reply] Tool failed: execute_command │
│    Args: {"command": "make build"}           │
│    Error: make: *** No rule to make target   │
│                                             │
│    What would you like to do?                │
│    y/yes   - Retry with same arguments       │
│    n/no    - Skip this tool call             │
│    abort   - Stop the reasoning loop         │
│    edit:{json}  - Modify and retry           │
│    <free text> - Inject custom message       │
│                                             │
│ Reply: [cursor]                              │
└─────────────────────────────────────────────┘
```

## 7. 實作路線圖

| 階段 | 工作項目 | 影響範圍 |
|------|---------|---------|
| **P0** | `user_reply.h/cpp` — 核心模組 | 新增檔案 |
| **P1** | `Config` 擴充 — 讀取 `user_reply_mode` | `config.h/cpp`, `zlagent.ini` |
| **P2** | `Agent` 整合 — 推理循環介入點 | `agent.h/cpp` |
| **P3** | CLI 命令 `/reply-mode` | `command_handlers.cpp` |
| **P4** | ESC 中斷後自動進入回復模式 | `main.cpp` |
| **P5** | 測試 — 單元測試 + 整合測試 | `tests/` |

## 8. 與現有功能的關聯

| 現有功能 | 互動方式 |
|---------|---------|
| **SafetyGuard** | SafetyGuard 處理危險操作確認；UserReply 是更通用的介入機制，兩者互補 |
| **ESC 中斷** | ESC 中斷後，若 `user_reply_mode != off`，自動進入回復提示而非直接跳過 |
| **Self-Reflection** | 反思失敗時可觸發使用者回復，讓使用者提供修正方向 |
| **Task Planning** | 規劃階段完成後、執行前可暫停等待使用者確認計畫 |
