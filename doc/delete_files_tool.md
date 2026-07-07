# delete_files_tool - 批量刪除多個檔案工具規格

## 1. 概述

`delete_files_tool` 是一個批量刪除多個檔案的工具，用於減少 API 呼叫次數、提升效率。當需要同時刪除多個檔案時，使用此工具比逐一呼叫 `delete_path` 更有效率。

---

## 2. 設計目標

- **減少 API 呼叫次數**：N 個檔案從 N+1 次（find_files + delete_path x N）減少到 2 次
- **提升處理速度**：一次刪除所有檔案，避免多次等待回應
- **降低 token 消耗**：減少每次呼叫的 overhead
- **保持與現有工具的一致性**：參數設計與 `delete_path`、`read_files` 保持一致
- **安全考量**：提供預覽模式，防止誤刪

---

## 3. 工具規格

### 3.1 基本參數

| 參數 | 類型 | 必要 | 預設值 | 說明 |
|------|------|------|--------|------|
| `paths` | string[] | ❌ | - | 指定要刪除的檔案路徑列表（與 glob 互斥） |
| `directory` | string | ❌ | "." | 搜尋目錄 |
| `glob` | string | ❌ | "*" | 檔案匹配模式（與 paths 互斥） |

### 3.2 可選參數

| 參數 | 類型 | 必要 | 預設值 | 說明 |
|------|------|------|--------|------|
| `dry_run` | boolean | ❌ | false | 預覽模式，不實際刪除，只顯示會刪除的檔案列表 |
| `recursive` | boolean | ❌ | true | 是否遞迴搜尋子目錄（僅在 glob 模式下有效） |
| `confirm` | boolean | ❌ | false | 是否需要確認才能執行刪除（dry_run=true 時自動忽略此參數） |

### 3.3 參數使用規則

- **模式一：指定 paths** → 刪除指定的檔案列表
- **模式二：directory + glob** → 搜尋目錄並刪除符合模式的檔案
- **paths 與 directory+glob 互斥**，不能同時使用
- `dry_run=true` 時不會實際刪除檔案，只顯示會刪除的清單
- `confirm=true` 時需要使用者確認才能執行（但此工具不支援互動式確認，僅作為標記）

### 3.4 安全機制

- **每個檔案都會經過 `SafetyGuard::get_instance().is_path_ok(path)` 檢查**
- **如果任何檔案路徑被拒絕，該檔案會跳過並記錄錯誤**
- **不支援遞迴刪除目錄**：此工具只刪除檔案，不刪除目錄（避免意外刪除整個目錄樹）
- **保護系統檔案**：不應允許刪除系統關鍵檔案（如 /etc/、Windows System32 等）

---

## 4. 回傳格式

### 4.1 成功回傳（JSON 格式）

```json
{
  "success": true,
  "deleted_files": [
    {
      "path": "src/temp.cpp",
      "status": "success",
      "size_bytes": 2048,
      "modified_time": "2024-01-15T10:30:00Z"
    },
    {
      "path": "build/output.o",
      "status": "success",
      "size_bytes": 4096,
      "modified_time": "2024-01-15T11:00:00Z"
    }
  ],
  "total_files": 2,
  "deleted_count": 2,
  "summary": {
    "success_count": 2,
    "error_count": 0,
    "errors": []
  },
  "dry_run": false
}
```

### 4.2 dry_run 模式回傳（JSON 格式）

如果指定了 `dry_run=true`，會先顯示預覽：

```json
{
  "success": true,
  "dry_run": true,
  "would_delete_files": [
    {
      "path": "src/temp.cpp",
      "size_bytes": 2048,
      "modified_time": "2024-01-15T10:30:00Z"
    },
    {
      "path": "build/output.o",
      "size_bytes": 4096,
      "modified_time": "2024-01-15T11:00:00Z"
    }
  ],
  "total_files": 2,
  "message": "Dry run mode: No files were actually deleted."
}
```

### 4.3 部分錯誤回傳（JSON 格式）

如果刪除失敗，會在 `summary.errors` 中記錄：

```json
{
  "success": true,
  "deleted_files": [
    {
      "path": "src/temp.cpp",
      "status": "success",
      "size_bytes": 2048
    }
  ],
  "total_files": 2,
  "deleted_count": 1,
  "summary": {
    "success_count": 1,
    "error_count": 1,
    "errors": [
      {
        "path": "src/readonly.cpp",
        "status": "error",
        "error": "Permission denied"
      }
    ]
  },
  "dry_run": false
}
```

### 4.4 安全檢查失敗回傳（JSON 格式）

如果路徑被 SafetyGuard 拒絕：

```json
{
  "success": false,
  "error": "Path 'src/../../etc/passwd' is outside allowed directories. Operation denied."
}
```

---

## 5. 使用情境與範例

### 5.1 刪除指定檔案列表

```
# 刪除多個特定檔案
delete_files(paths=["src/temp.cpp", "build/output.o", "logs/debug.log"])
```

**回傳：**
```json
{
  "deleted_files": [
    {"path": "src/temp.cpp", "status": "success"},
    {"path": "build/output.o", "status": "success"},
    {"path": "logs/debug.log", "status": "success"}
  ],
  "total_files": 3,
  "deleted_count": 3,
  "summary": {"success_count": 3, "error_count": 0}
}
```

### 5.2 刪除整個目錄的特定類型檔案

```
# 刪除 build/ 下所有 .o 檔案
delete_files(directory="build", glob="*.o")
```

**回傳：**
```json
{
  "deleted_files": [
    {"path": "build/main.o", "status": "success"},
    {"path": "build/utils.o", "status": "success"}
  ],
  "total_files": 2,
  "deleted_count": 2,
  "summary": {"success_count": 2, "error_count": 0}
}
```

### 5.3 dry_run 預覽模式（推薦先使用）

```
# 先預覽會刪除哪些檔案，不實際刪除
delete_files(directory="build", glob="*.o", dry_run=true)
```

**回傳：**
```json
{
  "would_delete_files": [
    {"path": "build/main.o", "size_bytes": 4096},
    {"path": "build/utils.o", "size_bytes": 2048}
  ],
  "total_files": 2,
  "dry_run": true,
  "message": "Dry run mode: No files were actually deleted."
}
```

### 5.4 刪除所有暫存檔案

```
# 刪除當前目錄下所有 .tmp、.bak、~結尾的檔案
delete_files(directory=".", glob="*.tmp")
delete_files(directory=".", glob="*.bak")
delete_files(directory=".", glob="*~")
```

### 5.5 遞迴刪除子目錄中的特定檔案

```
# 刪除 src/ 及其所有子目錄下的 .o 檔案
delete_files(directory="src", glob="**/*.o", recursive=true)
```

---

## 6. 與現有工具的比較

| 工具 | 優點 | 缺點 |
|------|------|------|
| `find_files` + `delete_path` x N | 靈活，可逐個處理 | API 呼叫多、速度慢 |
| **`delete_files`** | **一次刪除所有檔案、速度快、有 dry_run 保護** | **無法對個別檔案做不同操作** |

---

## 7. 實作注意事項

### 7.1 安全考量（重要）

- **dry_run 模式是必須的**：在實際刪除前，強烈建議先使用 dry_run=true 預覽
- **不支援遞迴刪除目錄**：此工具只刪除檔案，不刪除目錄（避免意外刪除整個目錄樹）
- **保護系統檔案**：不應允許刪除系統關鍵檔案（如 /etc/、Windows System32 等）
- **權限檢查**：在刪除前應檢查檔案權限，避免刪除無權限的檔案

### 7.2 效能考量

- **檔案數量限制**：建議單次不超過 1000 個檔案（刪除操作比讀取更耗資源）
- **大目錄處理**：如果 glob 模式匹配到大量檔案，應提示使用者確認

### 7.3 錯誤處理

- 個別檔案刪除失敗不應影響其他檔案的刪除
- 回傳中需明確標示哪些檔案成功、哪些失敗
- 對於不存在或無權限的檔案，應記錄詳細錯誤訊息
- **不支援復原**：刪除操作是不可逆的，應在文件中标明

### 7.4 與現有工具的一致性

- `path` / `directory` / `glob` 參數命名與 `find_files`、`read_files` 保持一致
- `dry_run` 參數設計與常見 CLI 工具（如 rm -n）一致
- 回傳格式應包含檔案路徑，方便對照

---

## 8. 未來擴充可能

### 8.1 移動到回收站

```
# 不刪除，而是移到回收站（可復原）
delete_files(paths=["src/temp.cpp"], recycle=true)
```

### 8.2 壓縮後刪除

```
# 先壓縮再刪除以節省空間
delete_files(directory="logs", glob="*.log", compress_before_delete=true)
```

### 8.3 排除特定檔案

```
# 刪除所有 .o，但排除 main.o
delete_files(directory="src", glob="**/*.o", exclude=["main.o"])
```

### 8.4 按修改時間刪除

```
# 刪除超過 7 天未修改的檔案
delete_files(directory="build", glob="*.o", older_than_days=7)
```

---

## 9. 規格版本

- **v1.0** - 初始規格（2024）
  - 基本批量刪除功能
  - paths / directory + glob 兩種模式
  - dry_run 預覽模式
  - recursive 遞迴搜尋支援
