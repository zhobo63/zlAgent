# edit_files - 編輯檔案工具規劃

## 概述

本文件規劃 `edit_files` 工具，用於一次性修改一個或多個檔案的多個位置，取代現有的 `edit_file`。

---

## 目的

一次性修改一個或多個檔案的多個位置，解決以下問題：
- **行號偏移問題**：不需要手動計算插入/刪除後的行號變化
- **衝突錯誤**：避免「改完上面，下面卻對不上」的問題
- **效率低落**：減少工具呼叫次數

---

## API 格式

```json
{
  "tool": "edit_files",
  "edits": [
    {
      "path": "src/main.cpp",
      "operations": [
        {
          "type": "replace_line_range",
          "start_line": 10,
          "end_line": 15,
          "new_text": "// Replace lines 10-15"
        },
        {
          "type": "insert_before_line",
          "line_number": 42,
          "content": "// Insert before line 42"
        }
      ]
    },
    {
      "path": "include/game.h",
      "operations": [
        {
          "type": "replace_text",
          "old_text": "#include <stdio.h>",
          "new_text": "#include <cstdio>"
        }
      ]
    }
  ]
}
```

---

## 參數說明

| 參數 | 類型 | 必填 | 說明 |
|------|------|------|------|
| `edits` | array | ✅ | 編輯列表，每個元素包含檔案路徑和該檔案的編輯操作 |
| `edits[].path` | string | ✅ | 要編輯的檔案路徑（相對於專案根目錄） |
| `edits[].operations` | array | ✅ | 該檔案的編輯操作列表 |

---

## 支援的編輯類型

### 1. replace_line_range - 替換行範圍

| 參數 | 類型 | 必填 | 說明 |
|------|------|------|------|
| `type` | string | ✅ | 固定值 `"replace_line_range"` |
| `start_line` | integer | ✅ | 起始行號（1-based） |
| `end_line` | integer | ✅ | 結束行號（1-based，包含） |
| `new_text` | string | ✅ | 替換後的內容 |

**行為：** 將 `start_line` 到 `end_line` 的行替換為 `new_text`。如果 `new_text` 有多行，則會改變檔案的總行數。

### 2. insert_before_line - 在指定行之前插入

| 參數 | 類型 | 必填 | 說明 |
|------|------|------|------|
| `type` | string | ✅ | 固定值 `"insert_before_line"` |
| `line_number` | integer | ✅ | 要插入的行號（1-based） |
| `content` | string | ✅ | 要插入的內容 |

**行為：** 在指定行之前插入內容。如果 `content` 有多行，則會改變檔案的總行數。

### 3. insert_after_line - 在指定行之後插入

| 參數 | 類型 | 必填 | 說明 |
|------|------|------|------|
| `type` | string | ✅ | 固定值 `"insert_after_line"` |
| `line_number` | integer | ✅ | 要插入的行號（1-based） |
| `content` | string | ✅ | 要插入的內容 |

**行為：** 在指定行之後插入內容。如果 `content` 有多行，則會改變檔案的總行數。

### 4. delete_lines - 刪除行範圍

| 參數 | 類型 | 必填 | 說明 |
|------|------|------|------|
| `type` | string | ✅ | 固定值 `"delete_lines"` |
| `start_line` | integer | ✅ | 起始行號（1-based） |
| `end_line` | integer | ✅ | 結束行號（1-based，包含） |

**行為：** 刪除指定範圍的行。

### 5. replace_text - 替換文字（精確比對）

| 參數 | 類型 | 必填 | 說明 |
|------|------|------|------|
| `type` | string | ✅ | 固定值 `"replace_text"` |
| `old_text` | string | ✅ | 要替換的文字（精確比對） |
| `new_text` | string | ✅ | 替換後的文字 |

**行為：** 使用**精確字串比對**，在檔案中尋找所有出現的 `old_text`。

- **1 處相符**：直接替換，無需確認。
- **多處相符**：顯示 diff 並列出順序編號，由使用者選擇：
  - `[N]` — 不替換（取消操作）
  - `[num]` — 只替換指定編號的那一處
  - `[A]` — 全部替換

**使用時機：**
- **不需要知道行號**，只要文字存在就會替換
- 適合「替換所有出現的文字」這種需求（例如：將所有 `printf` 改為 `std::cout`）
- 即使檔案結構改變也能正常工作

---

## 行號偏移處理機制

**核心原則：所有行號都是基於原始檔案的行號。**

工具在內部會按以下步驟處理編輯：
1. **讀取檔案到記憶體**：將整個檔案載入記憶體，記錄每一行的內容。
2. **驗證所有操作**：檢查行號範圍是否有效、行號操作是否有重疊。如果任何一個操作無效或重疊，該檔案修改失敗並回滾。
3. **同時計算所有變更**：基於原始行號，對所有操作（含行號操作與 `replace_text`）同時做偏移計算，確保每個操作的目標位置都正確。
4. **一次性寫入結果**：將記憶體中的最終內容寫回檔案。

> 這種做法確保了原子性 — 檔案要麼完全不改，要麼一次性寫入所有變更的結果。

**範例：**
```
原始檔案：
1: // Line 1
2: // Line 2
3: // Line 3
4: // Line 4
5: // Line 5

編輯操作（基於原始行號）：
- insert_before_line(3, "New line")   → 在第 3 行前插入
- delete_lines(4, 5)                    → 刪除第 4-5 行

結果：
1: // Line 1
2: // Line 2
3: New line
4: // Line 3
```

---

## 回應格式

成功（部分檔案可能失敗，每個檔案獨立）：

```json
{
  "edited_files": [
    {
      "path": "src/main.cpp",
      "original_lines": 100,
      "new_lines": 95,
      "operations_applied": [
        {"type": "replace_line_range", "start_line": 10, "end_line": 15},
        {"type": "insert_before_line", "line_number": 42}
      ]
    }
  ],
  "failed_files": [
    {
      "path": "include/game.h",
      "error": "Overlapping line operations detected: replace_line_range(5-10) overlaps with insert_before_line(7)",
      "operations_applied": []
    }
  ]
}
```

全部失敗：

```json
{
  "edited_files": [],
  "failed_files": [
    {
      "path": "src/main.cpp",
      "error": "Line number out of range: insert_before_line at line 150 (file has only 100 lines)",
      "operations_applied": []
    }
  ]
}
```

**`replace_text` 常見錯誤訊息：**

| 錯誤類型 | 範例 | 說明 |
|----------|------|------|
| **文字不存在** | `old_text not found in file: "printf("` | 檔案中找不到要替換的文字 |
| **空文字** | `old_text cannot be empty string` | `old_text` 不能為空 |

> **注意**：如果 `old_text` 在檔案中出現多次，會顯示 diff 並由使用者選擇替換哪些位置。

---

## 使用情境範例

### 複雜的重構（單一檔案多個修改點）
```json
{
  "tool": "edit_files",
  "edits": [
    {
      "path": "src/main.cpp",
      "operations": [
        {
          "type": "replace_line_range",
          "start_line": 10,
          "end_line": 25,
          "new_text": "// New function definition\nvoid process() {\n    // Implementation\n}"
        },
        {
          "type": "insert_before_line",
          "line_number": 30,
          "content": "#include \"new_header.h\""
        },
        {
          "type": "delete_lines",
          "start_line": 50,
          "end_line": 55
        }
      ]
    }
  ]
}
```

### 替換多個文字（單一檔案）
```json
{
  "tool": "edit_files",
  "edits": [
    {
      "path": "src/main.cpp",
      "operations": [
        {
          "type": "replace_text",
          "old_text": "printf(\"Hello\")",
          "new_text": "std::cout << \"Hello\""
        },
        {
          "type": "replace_text",
          "old_text": "#include <stdio.h>",
          "new_text": "#include <iostream>"
        }
      ]
    }
  ]
}
```

### 修改多個檔案
```json
{
  "tool": "edit_files",
  "edits": [
    {
      "path": "src/main.cpp",
      "operations": [
        {
          "type": "replace_text",
          "old_text": "printf(\"Hello\")",
          "new_text": "std::cout << \"Hello\""
        }
      ]
    },
    {
      "path": "include/game.h",
      "operations": [
        {
          "type": "replace_line_range",
          "start_line": 5,
          "end_line": 10,
          "new_text": "// Updated header"
        }
      ]
    }
  ]
}
```

### 單一檔案的簡單修改（取代現有的 edit_file）
```json
{
  "tool": "edit_files",
  "edits": [
    {
      "path": "src/main.cpp",
      "operations": [
        {
          "type": "replace_text",
          "old_text": "int x = 0;",
          "new_text": "int x = 1;"
        }
      ]
    }
  ]
}
```

---

## 行號編輯 vs 文字替換：何時使用哪種方式？

| 情境 | 推薦方式 | 原因 |
|------|----------|------|
| **在特定位置插入/刪除**（例如：在第 10 行前插入新函數） | 行號編輯 | 需要精確控制修改位置 |
| **替換所有出現的文字**（例如：將所有 `printf` 改為 `std::cout`） | 文字替換 | 不需要知道每個出現的位置 |
| **替換單一特定位置的內容**（例如：修改第 25 行的變數宣告） | 行號編輯 | 更精確，不會誤改其他位置 |
| **檔案結構可能變動**（例如：程式碼會經常重構） | 文字替換 | 不受行號變化影響 |
| **需要同時做多種操作**（插入、刪除、替換） | 混合使用 | 根據需求選擇最適合的方式 |

### 範例對比

情境：將 `printf("Hello")` 改為 `std::cout << "Hello"`

- **如果只有一個出現位置**，兩種方式都可以：

  行號編輯（需要知道在第幾行）：
  ```json
  {
    "type": "replace_line_range",
    "start_line": 10,
    "end_line": 10,
    "new_text": "std::cout << \"Hello\""
  }
  ```

- **如果有多个出現位置**，文字替換更方便：

  文字替換（不需要知道在哪幾行）：
  ```json
  {
    "type": "replace_text",
    "old_text": "printf(\"Hello\")",
    "new_text": "std::cout << \"Hello\""
  }
  ```

---

## 注意事項與限制

1. **行號範圍**：所有行號必須在檔案的有效範圍內。
2. **行號操作重疊**：同一個檔案內的行號操作如果範圍重疊，該檔案修改失敗並回滾（例如 `replace_line_range(5-10)` 與 `insert_before_line(7)` 重疊）。
3. **原子性保證**：每個檔案獨立。單一檔案內任何操作失敗，該檔案全部回滾；其他檔案不受影響。
4. **`replace_text` 精確比對**：使用精確字串比對，不會模糊匹配。如果 `old_text` 在檔案中出現多次，會顯示 diff 並由使用者選擇替換哪些位置。
5. **路徑安全**：使用 `SafetyGuard::is_path_ok()` 檢查路徑是否允許。
6. **路徑正規化**：所有路徑會經過 `normalize_path()` 處理，確保反斜線轉換為正斜線、移除結尾斜線。

---

## 與現有工具的比較

| 特性 | edit_files | edit_file |
|------|------------|-----------|
| 同時處理多個檔案 | ✅ | ❌（需多次呼叫） |
| 自動處理行號偏移 | ✅ | ❌（需手動計算） |
| 原子性操作 | ✅（每檔獨立） | ❌ |
| Token 效率 | ✅（一次請求） | ❌（多次請求） |

# 未來調整 [ ]
