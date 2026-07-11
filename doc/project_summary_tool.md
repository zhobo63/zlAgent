# 程式專案摘要工具 (Project Summary Tool)

## 概述

自動分析 C++ 專案並生成結構化的 Markdown 摘要報告。能掃描目錄、解析類別/函式宣告、建立依賴關係圖，並偵測常見設計模式。

## 功能特性

### 📊 基本統計
- **檔案數量**：掃描所有 `.cpp` / `.h` / `.hpp` 檔案
- **總行數**：計算程式碼總量
- **類別/結構體計數**：識別 class、struct、enum 宣告
- **函式計數**：解析方法與建構函式

### 📁 模組分組
根據目錄結構和命名慣例自動將檔案分組為邏輯模組（如 Core、RAG、TUI）。

### 🔍 設計模式偵測
自動識別常見設計模式：
- **Singleton**：靜態 factory method
- **Factory**：物件建立工廠方法
- **Strategy**：可插拔演算法選擇
- **Observer**：事件驅動回撥機制
- **Repository**：資料存取抽象層

### 🔗 依賴關係圖
分析檔案間的 `#include` 關係，建立簡化的依賴邊。

## API 使用

```cpp
#include "project_summary/summary_tool.h"

// 快速預覽（僅列印到控制台）
quick_summary("src");

// 生成完整 Markdown 報告
bool success = generate_report("src", "output/project_summary.md");

// 或手動使用引擎
agent::ProjectSummaryEngine engine;
engine.scan_directory("src");
engine.print_preview();
engine.generate_summary("report.md");
```

## 實作細節

### 檔案掃描
- 遞迴遍歷目錄，支援 `.cpp`、`.cc`、`.cxx`、`.h`、`.hpp`、`.hh`
- 計算行數和註解比例（簡化估算）

### 類別解析
- 識別 `class ClassName : public BaseClass {` 宣告
- 提取繼承列表（public/protected/private）
- 解析成員變數和建構函式

### 函式解析
- 匹配 `ClassName::method_name() const {` 模式
- 識別 static、inline、virtual、const 修飾子
- 提取回傳類型和方法名稱

### 模組分組
基於目錄結構的簡單啟發式演算法：
```
src/agent.cpp → Core_agent
src/rag_manager.cpp → RAG_rag_manager
include/tui.h → TUI_tui
```

## 限制與未來改進

### 當前限制
1. **解析深度有限**：僅處理單行宣告，不支援多行 class body
2. **註解計數簡化**：使用粗略估算而非精確計算
3. **依賴分析基礎**：僅基於 `#include`，未考慮模板實例化

### 未來改進方向
- [ ] 支援更複雜的 class 宣告（繼承、模板參數）
- [ ] 精確註解統計（區分 doc comment vs code comment）
- [ ] 圖形化依賴關係展示
- [ ] VCS
- [ ] 支援其他語言（Python、JavaScript、TypeScript、Rust、Go）

## 檔案結構

```
src/project_summary/
├── summary_tool.h      # 主頭檔：資料結構與 API 宣告
└── summary_tool.cpp    # 實作：掃描、解析、分析邏輯
```

## 整合到現有系統

可透過以下方式整合到 zlagent 專案：

1. **CLI 指令**：在 `command_dispatcher` 中加入 `/summary` 命令
2. **事件觸發**：當新檔案加入時自動重新生成摘要
3. **RAG 整合**：將摘要報告作為知識庫的一部分

## 編譯與測試

```bash
cmake --build build
# 或
make -C build
```

工具已整合到主執行檔中，無需額外編譯目標。

# AI 查看專案狀況過程

我會按照以下步驟來了解專案狀況：

## 1. **目錄結構概覽**
先看專案的整體架構，了解有哪些目錄、主要檔案分布在哪裡。

## 2. **閱讀專案說明文件**
了解專案的用途、架構和開發指南。

README.md
CMakeLists.txt

## 3. **查看主要程式碼結構**

## 4. **查看入口點與核心邏輯**

## 5. **查看核心類別**
