# read_files_tool - 批量讀取多個檔案工具規格

## 1. 概述

`read_files_tool` 是一個批量讀取多個檔案內容的工具，用於減少 API 呼叫次數、提升效率。當需要同時讀取多個檔案時，使用此工具比逐一呼叫 `read_file` 更有效率。

---

## 2. 設計目標

- **減少 API 呼叫次數**：N 個檔案從 N+1 次（find_files + read_file x N）減少到 2 次
- **提升處理速度**：一次取得所有檔案內容，避免多次等待回應
- **降低 token 消耗**：減少每次呼叫的 overhead
- **保持與現有工具的一致性**：參數設計與 `read_file` 保持一致

---

## 3. 工具規格

### 3.1 基本參數

| 參數 | 類型 | 必要 | 預設值 | 說明 |
|------|------|------|--------|------|
| `paths` | string[] \| object[] | ❌ | - | 指定要讀取的檔案路徑列表（與 directory+glob 互斥）。支援兩種格式：<br>1. **字串陣列**：`["file1.cpp", "file2.h"]` — 讀取完整內容<br>2. **物件陣列**：`[{"path": "file1.cpp", "start_line": 10, "end_line": 100}]` — 每個檔案可獨立指定行號範圍 |
| `directory` | string | ❌ | "." | 搜尋目錄（與 paths 互斥） |
| `glob` | string | ❌ | "*" | 檔案匹配模式（與 paths 互斥） |

### 3.2 可選參數

| 參數 | 類型 | 必要 | 預設值 | 說明 |
|------|------|------|--------|------|
| `outline` | boolean | ❌ | false | 讀取檔案 outline 而非內容。不能與物件模式中的 `start_line`/`end_line` 同時使用 |

> **注意：** `start_line` / `end_line` 只能用在**物件模式**的每個物件內部，不支援全域參數。

### 3.3 三種使用模式

#### 模式一：陣列模式（字串陣列）

```json
{"paths": ["file1.cpp", "file2.h"]}
```

讀取指定檔案的完整內容。可搭配 `outline: true` 改為讀取 outline。

#### 模式二：物件模式（物件陣列）

每個物件包含：
- `path`（必填）— 檔案路徑
- `start_line` / `end_line`（選填）— 指定行號範圍
- `outline`（選填）— 讀取 outline，與 `start_line`/`end_line` **互斥**

```json
{
  "paths": [
    {"path": "file1.cpp", "start_line": 10, "end_line": 100},
    {"path": "file2.h", "outline": true}
  ]
}
```

#### 模式三：directory + glob 模式

```json
{"directory": "src", "glob": "*.cpp"}
```

搜尋目錄並讀取符合模式的檔案完整內容。可搭配 `outline: true` 改為讀取 outline。

### 3.4 參數使用規則

- **paths 與 directory+glob 互斥**，不能同時使用
- `outline` 和物件模式中的 `start_line`/`end_line` **互斥**，不能同時指定
- `outline: true` 只有在沒有指定 `start_line` / `end_line` 時才有效

### 3.5 安全機制

- 每個檔案都會經過 `SafetyGuard::get_instance().is_path_ok(path)` 檢查
- 如果任何檔案路徑被拒絕，該檔案會跳過並記錄錯誤

---

## 4. 回傳格式

### 4.1 成功回傳（JSON 格式）

```json
{
  "files": [
    {
      "path": "src/main.cpp",
      "content": "// 檔案內容...",
      "lines_read": 150,
      "size_bytes": 4096
    },
    {
      "path": "src/utils.cpp",
      "content": "// 檔案內容...",
      "lines_read": 80,
      "size_bytes": 2048
    }
  ],
  "total_files": 2,
  "total_size_bytes": 6144,
  "summary": {
    "success_count": 2,
    "error_count": 0,
    "errors": []
  }
}
```

### 4.2 Outline 模式回傳（JSON 格式）

```json
{
  "files": [
    {
      "path": "src/main.cpp",
      "outline": "class Foo { ... }\nvoid bar() { ... }"
    },
    {
      "path": "src/utils.cpp",
      "outline": "int helper() { ... }"
    }
  ],
  "total_files": 2,
  "summary": {
    "success_count": 2,
    "error_count": 0,
    "errors": []
  }
}
```

### 4.3 部分錯誤回傳（JSON 格式）

如果讀取失敗，會在 `summary.errors` 中記錄：

```json
{
  "files": [
    {
      "path": "src/main.cpp",
      "content": "// 檔案內容...",
      "lines_read": 150,
      "size_bytes": 4096
    }
  ],
  "total_files": 2,
  "total_size_bytes": 4096,
  "summary": {
    "success_count": 1,
    "error_count": 1,
    "errors": [
      {
        "path": "src/missing.cpp",
        "error": "File not found"
      }
    ]
  }
}
```

### 4.4 安全檢查失敗回傳（JSON 格式）

如果路徑被 SafetyGuard 拒絕，該檔案會記錄在 `summary.errors`：

```json
{
  "files": [
    {
      "path": "src/main.cpp",
      "content": "// 檔案內容...",
      "lines_read": 150,
      "size_bytes": 4096
    }
  ],
  "total_files": 2,
  "total_size_bytes": 4096,
  "summary": {
    "success_count": 1,
    "error_count": 1,
    "errors": [
      {
        "path": "../../etc/passwd",
        "error": "Path is outside allowed directories. Operation denied."
      }
    ]
  }
}
```

---

## 5. 使用情境範例

### 5.1 陣列模式 — 讀取完整內容

```json
{"paths": ["src/main.cpp", "src/utils.cpp"]}
```

**回傳：**
```json
{
  "files": [
    {"path": "src/main.cpp", "content": "...", "lines_read": 150, "size_bytes": 4096},
    {"path": "src/utils.cpp", "content": "...", "lines_read": 80, "size_bytes": 2048}
  ],
  "total_files": 2,
  "summary": {"success_count": 2, "error_count": 0}
}
```

### 5.2 陣列模式 — 讀取 outline

```json
{"paths": ["src/main.cpp", "src/utils.cpp"], "outline": true}
```

**回傳：**
```json
{
  "files": [
    {"path": "src/main.cpp", "outline": "..."},
    {"path": "src/utils.cpp", "outline": "..."}
  ],
  "total_files": 2,
  "summary": {"success_count": 2, "error_count": 0}
}
```

### 5.3 物件模式 — 每個檔案不同行號範圍

```json
{
  "paths": [
    {"path": "README.md", "start_line": 1, "end_line": 50},
    {"path": "CMakeLists.txt", "start_line": 20, "end_line": 80}
  ]
}
```

**回傳：**
```json
{
  "files": [
    {"path": "README.md", "content": "...", "lines_read": 50},
    {"path": "CMakeLists.txt", "content": "...", "lines_read": 61}
  ],
  "total_files": 2,
  "summary": {"success_count": 2, "error_count": 0}
}
```

### 5.4 物件模式 — 混合使用（部分指定行號，部分讀 outline）

```json
{
  "paths": [
    {"path": "README.md", "start_line": 1, "end_line": 50},
    {"path": "CMakeLists.txt", "outline": true}
  ]
}
```

**回傳：**
```json
{
  "files": [
    {"path": "README.md", "content": "...", "lines_read": 50},
    {"path": "CMakeLists.txt", "outline": "..."}
  ],
  "total_files": 2,
  "summary": {"success_count": 2, "error_count": 0}
}
```

### 5.5 directory + glob 模式 — 讀取完整內容

```json
{"directory": "src", "glob": "*.cpp"}
```

**回傳：**
```json
{
  "files": [
    {"path": "src/main.cpp", "content": "...", "lines_read": 150, "size_bytes": 4096},
    {"path": "src/utils.cpp", "content": "...", "lines_read": 80, "size_bytes": 2048}
  ],
  "total_files": 2,
  "summary": {"success_count": 2, "error_count": 0}
}
```

### 5.6 directory + glob 模式 — 讀取 outline

```json
{"directory": "src", "glob": "*.cpp", "outline": true}
```

**回傳：**
```json
{
  "files": [
    {"path": "src/main.cpp", "outline": "..."},
    {"path": "src/utils.cpp", "outline": "..."}
  ],
  "total_files": 2,
  "summary": {"success_count": 2, "error_count": 0}
}
```

---

## 6. 與現有工具的比較

| 工具 | 優點 | 缺點 |
|------|------|------|
| `find_files` + `read_file` x N | 靈活，可逐個處理 | API 呼叫多、速度慢 |
| **`read_files`** | **一次取得所有內容、速度快；物件模式支援個別檔案不同操作** | **回傳內容可能很大，不適合極大檔案** |

---

## 7. 實作注意事項

### 7.1 效能考量

- **檔案大小限制**：建議單次讀取總大小不超過 5MB，避免記憶體問題
- **檔案數量限制**：建議單次不超過 100 個檔案
- **大檔案處理**：如果檔案很大，建議使用物件模式的 `start_line` / `end_line` 只讀取需要的部分

### 7.2 錯誤處理

- 個別檔案讀取失敗不應影響其他檔案的讀取
- 回傳中需明確標示哪些檔案成功、哪些失敗
- 對於不存在或無權限的檔案，應記錄詳細錯誤訊息

### 7.3 與現有工具的一致性

- `path` / `directory` / `glob` 參數命名與 `find_files` 保持一致
- `start_line` / `end_line` 參數命名與 `read_file` 保持一致
- 回傳格式應包含檔案路徑，方便對照

---

## 8. 未來擴充可能

### 8.1 平行讀取

```
# 使用平行讀取提升速度（如果檔案很多）
read_files(directory="src", glob="*.cpp", parallel=true)
```

### 8.2 增量更新

```
# 只讀取比指定時間戳記更新的檔案
read_files(directory="src", glob="*.cpp", modified_after="2024-01-01")
```

### 8.3 排除特定檔案

```
# 讀取所有 .cpp，但排除 test/ 目錄下的檔案
read_files(directory="src", glob="*.cpp", exclude=["test/**"])
```

---

## 9. 規格版本

- **v1.0** - 初始規格（2024）
  - 基本批量讀取功能
  - paths / directory + glob 兩種模式
  - start_line / end_line 行數範圍支援
- **v2.0** - 重新設計（2026）
  - 三種模式：陣列模式、物件模式、directory+glob 模式
  - outline 支援
  - `start_line`/`end_line` 僅限物件模式
  - 移除 dry_run、移除 success 欄位
