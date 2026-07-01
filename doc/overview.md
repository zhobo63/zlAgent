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

**一次給出全貌，而非碎片資訊。** 

---

## Level 1: Overview（鳥瞰圖）— 預設，5 秒內完成

```
📂 PROJECT OVERVIEW
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

🔧 BUILD SYSTEM: 
   - CMakeLists.txt for CMake
   - .sln .vcxproj for MSVC / MinGW supported
   
📊 CODE METRICS:
   - search .cpp .h ... 
     
📦 CURRENT STATUS:
   - Git: git status, git submodule status
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

## 輸出格式設計考量

### 1. 人類可讀 + LLM 可解析

- 使用 markdown 格式

### 2. 可壓縮性 — LLM 不需要每次都讀完整報告

報告中標註關鍵資訊的「摘要行」，LLM 可以決定是否要深入某個部分。

---
