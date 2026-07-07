# Batch File Operations - 批量檔案操作工具規劃

## 概述

本文件規劃兩個新的工具，用於提升大量檔案操作的效率：

1. **write_files** - 寫入一個或多個檔案（取代現有的 `write_file`）
2. **edit_files** - 編輯一個或多個檔案的多個位置（取代現有的 `edit_file`）

---

## 1. write_files - 寫入檔案

### 目的

一次性寫入一個或多個檔案，解決以下問題：
- 減少工具呼叫次數（降低延遲）
- 節省 Token 消耗
- 確保原子性（全部成功或全部失敗）

> **注意**：此工具將取代現有的 `write_file`。無論要寫入一個還是多個檔案，一律使用 `write_files`。如果要寫入單一檔案，只需傳入包含單個元素的陣列即可。

### API 格式

```json
{
  "tool": "write_files",
  "files": [
    {
      "path": "src/main.cpp",
      "content": "// C++ source code with UTF8 BOM\n"
    },
    {
      "path": "include/game.h",
      "content": "// Header file content\n"
    }
  ]
}
```

### 參數說明

| 參數 | 類型 | 必填 | 說明 |
|------|------|------|------|
| `files` | array | ✅ | 檔案列表，每個檔案包含 `path` 和 `content` |
| `files[].path` | string | ✅ | 檔案路徑（相對於專案根目錄） |
| `files[].content` | string | ✅ | 檔案內容 |

### 行為規範

1. **原子性**：如果任何一個檔案寫入失敗，所有已寫入的檔案都應該被回滾。
2. **自動建立目錄**：如果檔案所在的目錄不存在，自動建立（類似 `mkdir -p`）。
3. **覆寫模式**：如果檔案已存在，直接覆寫（不詢問）。
4. **UTF8 BOM**：C++ 來源檔應包含 UTF8 BOM。包含 `.c` `.cpp`、`.h` 和 `.hpp` 檔案

### 單一檔案寫入範例

如果要寫入單一檔案，只需傳入一個元素的陣列：

```json
{
  "tool": "write_files",
  "files": [
    {
      "path": "src/main.cpp",
      "content": "// C++ source code with UTF8 BOM\n"
    }
  ]
}
```

### 回應格式

成功：

```json
{
  "success": true,
  "written_files": [
    {"path": "src/main.cpp"},
    {"path": "include/game.h"}
  ],
  "failed_files": []
}
```

如果失敗：

```json
{
  "success": false,
  "written_files": [
    {"path": "src/main.cpp"}
  ],
  "failed_files": [
    {"path": "include/game.h", "error": "Permission denied"}
  ]
}
```

### 使用情境範例

**建立專案結構：**
```json
{
  "tool": "write_files",
  "files": [
    {"path": "src/main.cpp", "content": "..."},
    {"path": "src/game.h", "content": "..."},
    {"path": "CMakeLists.txt", "content": "..."}
  ]
}
```

**更新單一檔案：**
```json
{
  "tool": "write_files",
  "files": [
    {"path": "src/main.cpp", "content": "// Updated content"}
  ]
}
```

---

## 2. edit_files - 編輯檔案

### 目的

一次性修改一個或多個檔案的多個位置，解決以下問題：
- **行號偏移問題**：不需要手動計算插入/刪除後的行號變化
- **衝突錯誤**：避免「改完上面，下面卻對不上」的問題
- **效率低落**：減少工具呼叫次數

### API 格式

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

### 參數說明

| 參數 | 類型 | 必填 | 說明 |
|------|------|------|------|
| `edits` | array | ✅ | 編輯列表，每個元素包含檔案路徑和該檔案的編輯操作 |
| `edits[].path` | string | ✅ | 要編輯的檔案路徑（相對於專案根目錄） |
| `edits[].operations` | array | ✅ | 該檔案的編輯操作列表 |

### 支援的編輯類型

#### 1. replace_line_range - 替換行範圍

| 參數 | 類型 | 必填 | 說明 |
|------|------|------|------|
| `type` | string | ✅ | 固定值 `"replace_line_range"` |
| `start_line` | integer | ✅ | 起始行號（1-based） |
| `end_line` | integer | ✅ | 結束行號（1-based，包含） |
| `new_text` | string | ✅ | 替換後的內容 |

**行為：** 將 `start_line` 到 `end_line` 的行替換為 `new_text`。如果 `new_text` 有多行，則會改變檔案的總行數。

#### 2. insert_before_line - 在指定行之前插入

| 參數 | 類型 | 必填 | 說明 |
|------|------|------|------|
| `type` | string | ✅ | 固定值 `"insert_before_line"` |
| `line_number` | integer | ✅ | 要插入的行號（1-based） |
| `content` | string | ✅ | 要插入的內容 |

**行為：** 在指定行之前插入內容。如果 `content` 有多行，則會改變檔案的總行數。

#### 3. insert_after_line - 在指定行之後插入

| 參數 | 類型 | 必填 | 說明 |
|------|------|------|------|
| `type` | string | ✅ | 固定值 `"insert_after_line"` |
| `line_number` | integer | ✅ | 要插入的行號（1-based） |
| `content` | string | ✅ | 要插入的內容 |

**行為：** 在指定行之後插入內容。如果 `content` 有多行，則會改變檔案的總行數。

#### 4. delete_lines - 刪除行範圍

| 參數 | 類型 | 必填 | 說明 |
|------|------|------|------|
| `type` | string | ✅ | 固定值 `"delete_lines"` |
| `start_line` | integer | ✅ | 起始行號（1-based） |
| `end_line` | integer | ✅ | 結束行號（1-based，包含） |

**行為：** 刪除指定範圍的行。

#### 5. replace_text - 替換文字（類似現有 edit_file）

| 參數 | 類型 | 必填 | 說明 |
|------|------|------|------|
| `type` | string | ✅ | 固定值 `"replace_text"` |
| `old_text` | string | ✅ | 要替換的文字（會替換檔案中所有出現的該文字） |
| `new_text` | string | ✅ | 替換後的文字 |

**行為：** 將檔案中所有出現的 `old_text` 替換為 `new_text`。

**使用時機：**
- **不需要知道行號**，只要文字存在就會替換
- 適合「替換所有出現的文字」這種需求（例如：將所有 `printf` 改為 `std::cout`）
- 即使檔案結構改變也能正常工作

**注意事項：**
- 如果 `old_text` 在檔案中出現多次，會全部被替換
- 建議使用足夠獨特的文字來避免意外匹配

**⚠️ 誤改風險與解決方案：**

由於 `replace_text` 會替換所有出現的文字，可能會誤改不該修改的地方：

| 風險類型 | 範例 | 說明 |
|----------|------|------|
| **註解中的文字** | `// int x = 0;` 和 `int x = 0;` | 兩行都會被替換 |
| **不同變數但相同值** | `int x = 0;` 和 `int y = 0;` | 如果只替換 `= 0;`，會誤改 |
| **字串中的文字** | `"The value is: %d"` 和實際程式碼 | 可能誤改字串內容 |

**解決方案：**

1. **限制替換次數**（推薦）：新增 `max_replacements` 參數，只替換前 N 次出現
   ```json
   {
     "type": "replace_text",
     "old_text": "int x = 0;",
     "new_text": "int x = 1;",
     "max_replacements": 1
   }
   ```

2. **精確匹配模式**：支援正則表達式來確保唯一性
   ```json
   {
     "type": "replace_text",
     "pattern": "^\\s*int x = 0;",
     "replacement": "    int x = 1;"
   }
   ```

3. **預覽功能**：先顯示會修改哪些行，讓使用者確認
   ```json
   {
     "type": "replace_text",
     "old_text": "int x = 0;",
     "new_text": "int x = 1;",
     "dry_run": true
   }
   ```

4. **排除特定區域**：支援跳過註解和字串
   ```json
   {
     "type": "replace_text",
     "old_text": "printf(",
     "new_text": "std::cout <<",
     "exclude_comments": true,
     "exclude_strings": true
   }
   ```

**建議：** 在實作時，優先支援 **方案 1（限制替換次數）** 和 **方案 3（預覽功能）**，因為這兩者最容易實作且能有效降低誤改風險。

### 行號偏移處理機制

**核心原則：所有行號都是基於原始檔案的行號。**

工具在內部會按以下順序處理編輯：
1. **先計算總體變化**：統計所有插入和刪除操作的總行數變化。
2. **依序應用變更**：從檔案結尾向開頭處理（避免行號偏移問題）。
3. **一次性寫入結果**。

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

### 回應格式

成功：

```json
{
  "success": true,
  "edited_files": [
    {
      "path": "src/main.cpp",
      "original_lines": 100,
      "new_lines": 95,
      "operations_applied": [
        {"type": "replace_line_range", "start_line": 10, "end_line": 15},
        {"type": "insert_before_line", "line_number": 42}
      ]
    },
    {
      "path": "include/game.h",
      "original_lines": 50,
      "new_lines": 50,
      "operations_applied": [
        {"type": "replace_text", "old_text": "#include <stdio.h>", "matches_found": 1}
      ]
    }
  ],
  "failed_files": []
}
```

如果失敗：

```json
{
  "success": false,
  "edited_files": [],
  "failed_files": [
    {
      "path": "src/main.cpp",
      "error": "Line number out of range: insert_before_line at line 150 (file has only 100 lines)",
      "operations_applied": []
    }
  ],
  "rollback_status": "No changes made - atomic operation"
}
```

**`replace_text` 常見錯誤訊息：**

| 錯誤類型 | 範例 | 說明 |
|----------|------|------|
| **文字不存在** | `old_text not found in file: "printf("` | 檔案中找不到要替換的文字 |
| **空文字** | `old_text cannot be empty string` | `old_text` 不能為空 |

> **注意**：如果 `old_text` 在檔案中出現多次，會全部被替換（這是預設行為）。如果需要限制替換次數，請使用 `max_replacements` 參數。

### 使用情境範例

**複雜的重構（單一檔案多個修改點）：**
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

**替換多個文字（單一檔案）：**
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

**修改多個檔案：**
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

**單一檔案的簡單修改（取代現有的 edit_file）：**
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

### 行號編輯 vs 文字替換：何時使用哪種方式？

| 情境 | 推薦方式 | 原因 |
|------|----------|------|
| **在特定位置插入/刪除**（例如：在第 10 行前插入新函數） | 行號編輯 | 需要精確控制修改位置 |
| **替換所有出現的文字**（例如：將所有 `printf` 改為 `std::cout`） | 文字替換 | 不需要知道每個出現的位置 |
| **替換單一特定位置的內容**（例如：修改第 25 行的變數宣告） | 行號編輯 | 更精確，不會誤改其他位置 |
| **檔案結構可能變動**（例如：程式碼會經常重構） | 文字替換 | 不受行號變化影響 |
| **需要同時做多種操作**（插入、刪除、替換） | 混合使用 | 根據需求選擇最適合的方式 |

**範例對比：**

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

### write_files

1. **檔案大小限制**：建議單個檔案不超過 1MB，總計不超過 50MB。
2. **路徑安全**：使用 `SafetyGuard::is_path_ok()` 檢查路徑是否允許（防止路徑穿越攻擊如 `../../etc/passwd`）。
3. **權限檢查**：寫入前檢查目錄是否可寫。
4. **路徑正規化**：所有路徑會經過 `normalize_path()` 處理，確保：
   - 反斜線（`\`）轉換為正斜線（`/`），統一 Windows/Linux 格式
   - 移除結尾的斜線（但保留根目錄的 `/`）

### edit_files

1. **行號範圍**：所有行號必須在檔案的有效範圍內。
2. **文字替換衝突**：如果多個 `replace_text` 操作修改同一個位置，可能會產生衝突（工具應檢測並報告）。
3. **原子性保證**：任何一個編輯失敗，整個操作應該回滾。
4. **`replace_text` 誤改風險**：使用 `replace_text` 時，如果 `old_text` 在檔案中出現多次，會全部被替換。建議：
   - 使用足夠獨特的文字來避免意外匹配
   - 考慮使用 `max_replacements` 參數限制替換次數
   - 優先使用行號編輯（如 `replace_line_range`）來確保精確性

### 路徑安全機制（SafetyGuard）

```cpp
enum class PathCheckResult {
    Allowed,           // 在 working directory 或 whitelist 內 — 自動允許
    NeedsConfirmation, // 不在兩者內，且 strict_mode = OFF — 詢問使用者
    Denied             // 不在兩者內，且 strict_mode = ON — 拒絕
};

PathCheckResult SafetyGuard::is_path_ok(const std::string& path);
```

**檢查邏輯：**
1. **Working directory**：路徑在 `working_directory_` 內 → ✅ Allowed
2. **Whitelist**：路徑在 `path_whitelist_` 任一目錄內 → ✅ Allowed
3. **Strict mode = OFF**：不在兩者內 → ⚠️ NeedsConfirmation（詢問使用者）
4. **Strict mode = ON**：不在兩者內 → ❌ Denied

**範例：**
- `src/main.cpp` → ✅ Allowed（在 working directory 內）
- `../../etc/passwd` → ❌ Denied（路徑穿越攻擊，不在允許範圍內）

---

## 與現有工具的比較

| 特性 | write_files | write_file (已淘汰) | edit_files | edit_file (已淘汰) |
|------|-------------|---------------------|------------|--------------------|
| 同時處理多個檔案 | ✅ | ❌（需多次呼叫） | ✅ | ❌（需多次呼叫） |
| 自動處理行號偏移 | ✅ | N/A | ✅ | ❌（需手動計算） |
| 原子性操作 | ✅ | ❌ | ✅ | ❌ |
| Token 效率 | ✅（一次請求） | ❌（多次請求） | ✅（一次請求） | ❌（多次請求） |

---

## 未來擴展可能性

1. **read_files** - 批量讀取多個檔案
2. **delete_files** - 批量刪除多個檔案
3. **rename_files** - 批量重新命名檔案
4. **conditional_edits** - 支援條件式編輯（如果某行存在則替換，否則插入）
5. **replace_text 改進**：
   - `max_replacements` - 限制替換次數
   - `dry_run` - 預覽功能，顯示會修改哪些行
   - `exclude_comments/strings` - 排除註解和字串中的匹配
   - `pattern/replacement` - 支援正則表達式模式
