---
name: unit-test-conventions
description: Conventions for writing unit tests in the zlAgent project. Use when creating or modifying test files under unit_test/.
---
# 單元測試規範

## 檔案結構

每個測試檔案的標準結構：

```cpp
#include "pch.h"
#include "unit_test.h"
#include "tools.h"
// #include "safety_guard.h"  // 如果需要 Disable SafetyGuard

using namespace agent;
namespace fs = std::filesystem;
using json = nlohmann::json;

static void safe_remove_all(const std::string& path)
{
    try {
        if (fs::exists(path)) fs::remove_all(path);
    } catch (...) {}
}
```

## 入口函式（Entry Point）

每個測試檔案必須有一個入口函式，名稱格式為 `test_<category>_tool`：

```cpp
void test_fs_tool(UnitReport& parent)
{
    // Disable SafetyGuard for tests.
    auto& sg = SafetyGuard::get_instance();
    sg.reset_path_whitelist();
    sg.set_working_directory("");

    UnitReport unit("fs_tools");
    LOG_INFO("test_fs_tools", "fs_tools");

    test_create_directory_tool(unit);
    test_delete_path_tool(unit);
    test_copy_path_tool(unit);

    parent.report.push_back(unit);
}
```

**注意：**
- 如果測試涉及檔案系統操作，必須 Disable SafetyGuard
- `LOG_INFO` 的第一個參數要跟函式名稱一致（包含 `_tool` 結尾）
- 每個 tool 的測試拆成獨立子函式，方便維護

## 子測試函式

每個 tool 一個子函式，格式為 `test_<tool_name>_tool`：

```cpp
void test_create_directory_tool(UnitReport& parent)
{
    UnitReport unit("create_directory");
    LOG_INFO("test_create_directory", "create_directory");

    // ... tests ...

    parent.report.push_back(unit);
}
```

**注意：** `LOG_INFO` 的第一個參數要跟函式名稱一致（包含 `_tool` 結尾）。

## 測試區塊

每個獨立測試用 `{}` 包起來，標準流程：

```cpp
{
    LOG_INFO("tool_name", "test_case_name");
    std::string dir = "test_<abbrev>_<case>_temp";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(dir);

    // 建立測試檔案/環境
    {
        std::ofstream f(fs::path(dir) / "file.txt");
        f << "content";
    }

    auto tool = create_<tool_name>_tool();
    json args;
    args["key"] = "value";
    auto args_str = args.dump();
    tool->show_arguments(args_str);
    tool->show_preview(args_str);
    std::string result = tool->execute(args_str);
    tool->show_result(result);
    UNIT_TEST("test_case_name", condition);

    safe_remove_all(dir);  // 清理
}
```

### 必要元素

1. **`safe_remove_all`** — 每個測試檔案都要有，參數名稱必須是 `path`（不是 `dir`）：
```cpp
static void safe_remove_all(const std::string& path)
{
    try {
        if (fs::exists(path)) fs::remove_all(path);
    } catch (...) {}
}
```

2. **臨時目錄命名** — 用 `test_<abbrev>_<case>_temp`，測試結束一定要清理：
```cpp
std::string dir = "test_cd_basic_temp";  // cd = create_directory, basic = case name
if (fs::exists(dir)) fs::remove_all(dir);
fs::create_directories(dir);
// ... test ...
safe_remove_all(dir);
```

3. **`UNIT_TEST`** — 測試名稱要有描述性，不要重複：
```cpp
UNIT_TEST("basic_success", result.find("Successfully created") != std::string::npos);
UNIT_TEST("dir_exists", fs::exists(fs::path(dir) / "new_folder"));
```

4. **`LOG_INFO`** — 每個測試區塊開頭記錄：
```cpp
LOG_INFO("tool_name", "test_case_name");
```

5. **Tool 執行流程** — 正常測試要呼叫完整流程：
```cpp
auto args_str = args.dump();
tool->show_arguments(args_str);
tool->show_preview(args_str);
std::string result = tool->execute(args_str);
tool->show_result(result);
```

6. **Tool name 驗證** — 第一個測試要驗證 tool 的 `name()`：
```cpp
auto tool = create_create_directory_tool();
UNIT_TEST("name_is_create_directory", tool->name() == "create_directory");
```

## JSON array 賦值規則（重要）

**一律用 `json::array(...)`，不要用 `{...}`。**

```cpp
// ❌ 錯誤 — {obj1, obj2} 會被推導為 object
args["files"] = {f1, f2};
args["edits"] = {edit_obj};
edit_obj["operations"] = {op};

// ✅ 正確
args["files"] = json::array({f1, f2});
args["edits"] = json::array({edit_obj});
edit_obj["operations"] = json::array({op});
```

**例外：已經用 push_back 建立的 array，直接賦值即可。**
```cpp
json ops;
ops.push_back(op1);
ops.push_back(op2);
edit_obj["operations"] = ops;  // ✅ 不需要 json::array()
```

## 測試覆蓋範圍（每個 Tool 必測 8 項）

每個 tool 至少要涵蓋以下 **8 項**：

| # | 測試項目 | 說明 |
|---|---------|------|
| 1 | **基本成功** | 正常參數執行，驗證回傳字串含 `"Successfully"` + 檔案系統狀態正確 |
| 2 | **巢狀/遞迴操作** | 如 `mkdir -p`、遞迴刪除、遞迴複製等 |
| 3 | **已存在路徑處理** | 目標已存在時的行為（不報錯 or 報錯，視 Tool 而定） |
| 4 | **不存在路徑錯誤** | 來源/目標不存在時回傳 `"Error"` |
| 5 | **空參數錯誤** | 必填欄位為 `""` 時回傳 `"Error"` |
| 6 | **無效 JSON 錯誤** | 輸入 `"not json"` 時回傳 `"Error"` |
| 7 | **空輸入錯誤** | 輸入 `""` 時回傳 `"Error"` |
| 8 | **Tool name 正確性** | `tool->name()` 等於預期字串 |

### 雙路徑 Tool 額外測試項（copy / move）

具有 `source_path` + `destination_path` 的 Tool，除了上述 8 項外，還要涵蓋：

| # | 測試項目 | 說明 |
|---|---------|------|
| A | **來源不存在** | source_path 不存在 → Error |
| B | **目標已存在** | destination_path 已存在 → Error |
| C | **空 source_path** | `args["source_path"] = ""` → Error |
| D | **空 destination_path** | `args["destination_path"] = ""` → Error |
| E | **內容一致性驗證** | 複製/移動後比對檔案內容相同 |

### 錯誤處理三件套（每個 tool 都要有）

以下三個測試區塊是**強制要求**，不可省略：

```cpp
// empty required field
{
    LOG_INFO("tool_name", "empty_param_error");
    auto tool = create_<tool_name>_tool();
    json args;
    args["<required_field>"] = "";
    std::string result = tool->execute(args.dump());
    UNIT_TEST("empty_param_returns_error", result.find("Error") != std::string::npos);
}

// invalid JSON
{
    LOG_INFO("tool_name", "invalid_json_returns_error");
    auto tool = create_<tool_name>_tool();
    std::string result = tool->execute("not json");
    UNIT_TEST("invalid_json_returns_error", result.find("Error") != std::string::npos);
}

// empty input
{
    LOG_INFO("tool_name", "empty_input_returns_error");
    auto tool = create_<tool_name>_tool();
    std::string result = tool->execute("");
    UNIT_TEST("empty_input_returns_error", result.find("Error") != std::string::npos);
}
```

## 常見驗證模式

```cpp
// 結果包含某字串
UNIT_TEST("has_expected", result.find("expected") != std::string::npos);

// 結果不包含 Error
UNIT_TEST("no_error", result.find("Error") == std::string::npos);

// 檔案存在
UNIT_TEST("file_exists", fs::exists(fs::path(dir) / "file.txt"));

// 檔案不存在（刪除後驗證）
UNIT_TEST("file_not_exist", !fs::exists(fs::path(dir) / "file.txt"));

// 讀取檔案內容驗證
{
    std::ifstream src_f(fs::path(dir) / "source.txt");
    std::string src_content((std::istreambuf_iterator<char>(src_f)), std::istreambuf_iterator<char>());
    UNIT_TEST("content_matches", src_content == "expected");
}

// 內容相同驗證（copy 後）
{
    std::ifstream src_f(fs::path(dir) / "source.txt");
    std::string src_content((std::istreambuf_iterator<char>(src_f)), std::istreambuf_iterator<char>());
    std::ifstream dst_f(fs::path(dir) / "dest.txt");
    std::string dst_content((std::istreambuf_iterator<char>(dst_f)), std::istreambuf_iterator<char>());
    UNIT_TEST("content_same", src_content == dst_content);
}
```

## 常見錯誤（避免）

| 錯誤 | 說明 |
|------|------|
| `safe_remove_all` 參數用 `dir` 但函式宣告是 `path` | 編譯錯誤，參數名稱要一致 |
| `LOG_INFO` 第一個參數跟函式名不一致 | 例如函式叫 `test_overview_tool` 但寫成 `"test_overview_tools"` |
| 忘記清理臨時目錄 | 測試結束後一定要 `safe_remove_all(dir)` |
| 沒有 Disable SafetyGuard | 涉及檔案操作的測試必須 Disable |
| `{...}` 賦值給 JSON array | 要用 `json::array({ ... })` |
| 缺少錯誤處理測試 | 每個 tool 都要有 empty/invalid/empty input 測試 |
