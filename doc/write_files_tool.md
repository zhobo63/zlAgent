# write_files - 寫入檔案工具規劃

## 概述

本文件規劃 `write_files` 工具，用於一次性寫入一個或多個檔案，取代現有的 `write_file`。

---

## 目的

一次性寫入一個或多個檔案，解決以下問題：
- 減少工具呼叫次數（降低延遲）
- 節省 Token 消耗
- 允許部分成功（能寫的就寫，回傳成功與失敗列表）

> **注意**：此工具將取代現有的 `write_file`。無論要寫入一個還是多個檔案，一律使用 `write_files`。如果要寫入單一檔案，只需傳入包含單個元素的陣列即可。

---

## API 格式

**文字檔案：**
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

**二進位檔案（base64）：**
```json
{
  "tool": "write_files",
  "files": [
    {
      "path": "assets/icon.png",
      "content": "iVBORw0KGgoAAAANSUhEUgAA...",
      "encoding": "base64"
    }
  ]
}
```

---

## 參數說明

| 參數 | 類型 | 必填 | 說明 |
|------|------|------|------|
| `files` | array | ✅ | 檔案列表，每個檔案包含 `path`、`content` 和可選的 `encoding` |
| `files[].path` | string | ✅ | 檔案路徑（相對於專案根目錄） |
| `files[].content` | string | ✅ | 檔案內容。若 `encoding` 為 `base64`，則為 base64 編碼字串；否則為原始文字 |
| `files[].encoding` | string | ❌ | 編碼方式。預設為 `text`（純文字）。可選值：`text`、`base64`。使用 `base64` 時，工具會自動解碼後以二進位模式寫入檔案。**此模式下不干預 BOM** |

---

## 回應格式欄位說明

| 參數 | 類型 | 說明 |
|------|------|------|
| `written_files` | array | 成功寫入的檔案列表，每個元素包含 `path`（string）和 `status`（固定為 `"ok"`） |
| `failed_files` | array | 寫入失敗的檔案列表，每個元素包含 `path`（string）、`status`（固定為 `"error"`）和 `error`（錯誤訊息字串） |

---

## 行為規範

1. **部分成功**：允許部分寫入成功。能寫的檔案就寫，回傳 `written_files`（成功列表）和 `failed_files`（失敗列表）。不進行回滾。
2. **自動建立目錄**：如果檔案所在的目錄不存在，自動建立（類似 `mkdir -p`）。
3. **覆寫模式**：如果檔案已存在，直接覆寫（不詢問）。若檔案為唯讀，直接回報失敗，不嘗試修改權限。
4. **UTF8 BOM**：副檔名為 `.c`、`.cpp`、`.h`、`.hpp` 的檔案，工具會自動加上 UTF8 BOM（`\xEF\xBB\xBF`）。寫入前會先檢查內容是否已有 BOM，若已存在則不重複添加。**僅適用於 `text` 編碼模式**；`base64` 模式下不干預 BOM，由使用者自行控制二進位內容。

---

## 單一檔案寫入範例

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

---

## 回應格式

成功（全部寫入成功）：

```json
{
  "written_files": [
    {"path": "src/main.cpp", "status": "ok"},
    {"path": "include/game.h", "status": "ok"}
  ],
  "failed_files": []
}
```

部分成功（有些寫入失敗）：

```json
{
  "written_files": [
    {"path": "src/main.cpp", "status": "ok"}
  ],
  "failed_files": [
    {"path": "include/game.h", "status": "error", "error": "Permission denied"}
  ]
}
```

全部失敗：

```json
{
  "written_files": [],
  "failed_files": [
    {"path": "src/main.cpp", "status": "error", "error": "No such file or directory"},
    {"path": "include/game.h", "status": "error", "error": "Permission denied"}
  ]
}
```

---

## 使用情境範例

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

**寫入二進位圖片：**
```json
{
  "tool": "write_files",
  "files": [
    {
      "path": "assets/icon.png",
      "content": "iVBORw0KGgoAAAANSUhEUgAA...",
      "encoding": "base64"
    }
  ]
}
```

---

## 注意事項與限制

1. **路徑安全**：使用 `SafetyGuard::is_path_ok()` 檢查路徑是否允許（防止路徑穿越攻擊如 `../../etc/passwd`）。
2. **權限檢查**：寫入前檢查目錄是否可寫。
3. **路徑正規化**：所有路徑會經過 `normalize_path()` 處理，確保：
   - 反斜線（`\`）轉換為正斜線（`/`），統一 Windows/Linux 格式
   - 移除結尾的斜線（但保留根目錄的 `/`）

---

## 路徑安全機制（SafetyGuard）

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

| 特性 | write_files |
|------|-------------|
| 同時處理多個檔案 | ✅ |
| 部分成功支援 | ✅（回傳 `written_files` + `failed_files`） |
| Token 效率 | ✅（一次請求寫入多個檔案） |
| 二進位檔支援（base64） | ✅ |
| BOM 自動添加 | ✅（僅 text 模式，`.c`/`.cpp`/`.h`/`.hpp`） |

---
