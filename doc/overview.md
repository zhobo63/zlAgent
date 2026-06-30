# Project Inspector — 專案狀況總覽工具

## 核心問題

現在 LLM 要理解專案狀況，必須**手動拼湊**多個工具的結果：

- `list_directory` → 看目錄結構
- `read_file(README.md)` → 看說明
- `read_file(CMakeLists.txt)` → 看建構配置
- `get_file_outline(main.cpp)` → 看入口點
- `git_status()` → 看當前狀態

這太慢了，而且 LLM 可能不知道該調用哪些工具、順序為何。

---

## 設計理念

**一次給出全貌，而非碎片資訊。** 類似人類看地圖的「鳥瞰視角」。

LLM 拿到 Overview 後會自己決定下一步：
- 需要看某個工具的實作？→ `get_file_outline` + `read_file`
- 需要知道改了什麼？→ `git_status` + `git_diff`
- 需要搜尋特定內容？→ `search_code`

---

## 輸出範例

```
📂 PROJECT OVERVIEW — zlagent v0.1.0
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

🔧 BUILD SYSTEM: CMake (C++17)
   - MSVC / MinGW supported
   - OpenSSL dependency (NuGet package)
   
📊 CODE METRICS:
   - src/     : 32 .cpp files (~45K lines)
   - include/ : 41 .h files (~8K lines)  
   - tools/   : 7 tool implementations
   
🧠 CORE ARCHITECTURE:
   Agent → LLMClient → ToolRegistry → Tools (7 built-in + plugin DLLs)
   
🔁 REASONING LOOP:
   Send to LLM → Decide tool call? → Execute → Repeat → Final answer
   
⚡ ADVANCED FEATURES:
   ✅ Task Planning    ✅ Self-Reflection    ✅ Multi-Agent
   ✅ RAG              ✅ Long-term Memory   ✅ Telegram Bot
   
📦 CURRENT STATUS:
   - Git: 3 modified files, 2 untracked
   - Build dir exists (build/)
```

---

## 為什麼需要這個工具？

### 現有工具的痛點

| 場景 | 沒有此工具 | 有此工具 |
|------|-----------|---------|
| "幫我理解這個專案" | LLM 要調用 5+ 次工具，每次來回 | **一次回傳全貌** |
| "這個 bug 可能在哪？" | 需要猜測、多次探索 | 直接看到架構和依賴關係 |
| "新增功能該改哪裡？" | 不知道從何開始 | 看到模組邊界和入口點 |

### 與現有工具的差異

- `list_directory` → **只給目錄樹**，沒有意義層級
- `get_file_outline` → **只給單一檔案符號**，沒有全局視角
- `git_status` → **只有版本控制狀態**，沒有專案結構
- `search_code` → **需要知道搜尋什麼**，不能自動探索

---

## 使用策略

```
收到任務 → 是否需要了解專案？
    │
    ├── 是 → project_inspector（5秒內拿到全貌）
    │         ↓
    │      LLM 根據 Overview 決定下一步：
    │         ├── 資訊已足夠 → ✅ 不再調用其他工具
    │         └── 需要深入某個領域 → 🔁 調用特定工具
    │
    └── 否 → 直接調用所需工具
```

---

## 侷限性

1. **可能過時** — 如果專案結構剛被修改，Overview 可能還沒更新
2. **不處理版本控制** — git_status、git_diff 是它的職責範圍之外
