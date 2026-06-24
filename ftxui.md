# FTXUI 底部狀態列設計方案

對於 ZL Agent，狀態列應該即時反映 Agent 運行狀態。以下是完整設計：

---

## 狀態列佈局

```
┌─────────────────────────────────────────────────────┐
│  ZL Agent — Multi-Language AI Coding Agent           │
├──────────────────┬──────────────────────────────────┤
│                  │                                  │
│   對話 / 輸出區域    │   (主體內容)                      │
│                  │                                  │
├──────────────────┴──────────────────────────────────┤
│ 🔗 Connected │ 🤖 qwen2.5-coder-7b │ 🧠 3,500/8K │ ⚡ 3/10 │
│ ✓ Plan ✓ Reflect ✗ MultiAgent │ 💾 Msg:15 Fact:3 │ ▶ Executing │ /help /model Ctrl+C Quit │
└─────────────────────────────────────────────────────┘
```

## 各區塊說明

| 區塊 | 內容 | 顏色邏輯 |
|------|------|----------|
| **連線狀態** | 🔗 Connected / ❌ Disconnected | 綠=連線，紅=斷線 |
| **模型名稱** | 🤖 qwen2.5-coder-7b | 藍色粗體 |
| **Token 用量** | 🧠 3,500/8K | <50% 綠 → <80% 黃 → ≥80% 紅 |
| **迭代次數** | ⚡ 3/10 | 接近上限時變紅 |
| **功能開關** | ✓ Plan ✓ Reflect ✗ MultiAgent | 啟用=綠勾，關閉=灰叉 |
| **記憶統計** | 💾 Msg:15 Fact:3 | 紫色 |
| **當前階段** | ▶ Thinking / Executing / Reviewing / Idle | 動態變色+粗體 |
| **快捷鍵提示** | /help /model Ctrl+C Quit | 灰色，右對齊 |

## FTXUI 核心實現

```cpp
// ─── 狀態數據模型 ──────────────────────────────
struct AgentState {
    bool connected = true;
    std::string model_name = "qwen2.5-coder-7b";
    int tokens_used = 0, max_tokens = 8192;
    int current_iteration = 0, max_iterations = 10;
    bool task_planning = true, self_reflection = true, multi_agent = false;
    int memory_count = 0, facts_count = 0;
    std::string current_phase = "Idle"; // Idle / Thinking / Executing / Reviewing
};

// ─── 狀態列渲染 ──────────────────────────────
Element RenderStatusBar(const AgentState& state) {
    // Token 用量 — 比例變色
    double ratio = (double)state.tokens_used / state.max_tokens;
    Color token_color = ratio < 0.5 ? Color::Green
                       : ratio < 0.8 ? Color::Yellow
                                    : Color::Red;

    // 當前階段 — 動態變色
    Color phase_color;
    if (state.current_phase == "Thinking")  phase_color = Color::Yellow | Color::Bold;
    else if (state.current_phase == "Executing") phase_color = Color::Cyan | Color::Bold;
    else if (state.current_phase == "Reviewing") phase_color = Color::Magenta | Color::Bold;
    else                                         phase_color = Color::Gray;

    return vbox({
        separator(),  // 分隔線
        hflow({
            colored(Color::Green, text("🔗 Connected")),   separatorH(2),
            colored(Color::Blue | Color::Bold, text("🤖 " + state.model_name)),  separatorH(2),
            colored(token_color, text("🧠 " + std::to_string(state.tokens_used) + "/8K")),  separatorH(2),
            colored(Color::Cyan, text("⚡ " + std::to_string(state.current_iteration) + "/10")),  separatorH(2),

            // 功能開關
            colored(Color::Green, text("✓ Plan")),         separatorH(1),
            colored(Color::Green, text("✓ Reflect")),      separatorH(1),
            colored(Color::Gray, text("✗ MultiAgent")),    separatorH(2),

            colored(Color::Magenta, text("💾 Msg:" + std::to_string(state.memory_count))),  separatorH(2),
            colored(phase_color, text("▶ " + state.current_phase)),  separatorH(2),

            // 右側快捷鍵提示
            |filler(),
            right_aligned(colored(Color::Gray,
                hflow({ text("/help"), separatorH(1), text("/model"), separatorH(1), text("Ctrl+C Quit") })
            )),
        }),
    });
}

// ─── 主佈局：標題 + 內容 + 輸入 + 狀態列 ─────────
auto root = vbox({
    colored(Color::Bold | Color::Blue, text(" ZL Agent ")),
    separator(),
    |conversation_history,     // 佔滿剩餘空間
    hflow({ text("> "), input_box }),
    RenderStatusBar(state),    // 固定在底部
});
```

## 與現有 INI 配置整合

狀態列的顯示內容可以直接從 `zlagent.ini` 讀取：

| INI Section | Key | 映射到狀態列 |
|-------------|-----|------------|
| `[llm]` | `model` | 🤖 模型名稱 |
| `[memory]` | `max_messages` | 💾 Msg 計數上限 |
| `[agent]` | `max_iterations` | ⚡ 迭代次數上限 |
| `[features]` | `task_planning` | ✓ Plan 開關 |
| `[features]` | `self_reflection` | ✓ Reflect 開關 |
| `[features]` | `multi_agent` | ✗ MultiAgent 開關 |

## 動態更新機制

FTXUI 的渲染循環每幀重繪，只需在 Agent 狀態改變時更新 `AgentState` 結構體：

```cpp
// Agent 運行中...
state.current_phase = "Thinking";   // 狀態列即時變黃
state.tokens_used += 500;           // Token 計數器增加

// FTXUI 渲染循環自動重繪，無需手動刷新
while (running) {
    render(root);
    std::this_thread::sleep_for(16ms);
}
```

---

**總結：** FTXUI 的 `hflow` + `colored` + `separatorH` 組合可以輕鬆實現資訊豐富的狀態列，顏色動態變化讓用戶一眼掌握 Agent 運行狀況。
