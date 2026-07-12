---
name: unit-test-conventions
description: Conventions for writing unit tests in the zlAgent project. Use when creating or modifying test files under unit_test/.
---
# 單元測試規範

`test_<category>` - category: filename without extension, e.g. `config.cpp` → `test_config()`
  - `test_<class_name>`: 每個 class 的測試拆成獨立子函式
    - 測試區塊：用 `{}` 包起來，`UNIT_TEST("描述性名稱", condition)`

e.g.  `config.cpp` →
test_config()
  - static test_ini_parser()
    - { UNIT_TEST("parse_success", ...) }
    - { UNIT_TEST("update_key_success", ...) }
  - static test_config_class()
    - { UNIT_TEST("parse_bool_true", ...) }
    - { UNIT_TEST("load_success", ...) }
    - { UNIT_TEST("save_success", ...) }

## 檔案結構

每個測試檔案的標準結構：

```cpp
#include "pch.h"
#include "unit_test.h"
// #include "tools.h"
// #include "safety_guard.h"  // 如果需要設定 SafetyGuard 白名單

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

每個測試檔案必須有一個入口函式，名稱格式為 `test_<category>`：

- category: filename without extension, e.g. `config.cpp` → `test_config()`
- 實作檔案 `config.cpp`: 測試檔案 `test_config.cpp` → 入口函式 `test_config()`

```cpp
void test_config(UnitReport& parent)
{
    // Ensure SafetyGuard whitelist contains current path for tests.
    auto& sg = SafetyGuard::get_instance();
    sg.set_path_whitelist({fs::current_path().string()});

    UnitReport unit("config");
    LOG_INFO("config", "entry");

    test_ini_parser(unit);
    test_config_class(unit);

    parent.report.push_back(unit);
}
```

**注意：**
- 如果測試涉及檔案系統操作，必須確保 SafetyGuard 白名單包含當前路徑
- `LOG_INFO` 第一個參數是 class/tool 名稱，第二個參數用描述性名稱
- 如果測試檔案裡面有不同 class，每個 class 的測試拆成獨立子函式 `test_<class_name>`



## 子測試函式

- 如果一個測試檔案包含多個 class，則每個 class 的測試需拆成獨立子函式，命名格式為 `test_<class_name>`（例如 `test_ini_parser`、`test_config_class`）。如果只有一個 class，則直接在入口函式中撰寫即可。
- **注意：** 當 class name 和 category name 相同時（如 `config.cpp` 中的 `Config`），子函式命名為 `test_<class_name>_class` 以避免與入口函式衝突。
- **獨立子函式使用靜態** — 所有被入口函式呼叫的子測試函式必須宣告為 `static`，限制其連結範圍於該翻譯單元。

```cpp
static void test_ini_parser(UnitReport& parent)
{
    UnitReport unit("ini_parser");
    LOG_INFO("ini_parser", "entry");

    // ... tests ...

    parent.report.push_back(unit);
}
```

當 class name 與 category 衝突時：

```cpp
static void test_config_class(UnitReport& parent)
{
    UnitReport unit("config_class");
    LOG_INFO("config_class", "entry");

    // ... tests for Config class ...

    parent.report.push_back(unit);
}
```

## 測試區塊

每個獨立測試用 `{}` 包起來，標準流程：

```cpp
{
    LOG_INFO("ini_parser", "parse_success");
    ...
    UNIT_TEST("parse_success", condition);

    safe_remove_all(dir);  // 清理
}
```

### 必要元素

1. **`safe_remove_all`** — 每個測試檔案都要有，參數名稱必須是 `path`：
```cpp
static void safe_remove_all(const std::string& path)
{
    try {
        if (fs::exists(path)) fs::remove_all(path);
    } catch (...) {}
}
```

2. **臨時目錄命名** — 格式為 `test_<簡寫>_<測試項目>_temp`，例如 `test_ip_basic_temp`（ip = ini_parser），測試結束一定要清理：
```cpp
std::string dir = "test_ip_basic_temp";  // ip = ini_parser, basic = 基本解析
safe_remove_all(dir);
fs::create_directories(dir);
// ... test ...
safe_remove_all(dir);
```

3. **`UNIT_TEST`** — 測試名稱用描述性字串，不要重複：
```cpp
UNIT_TEST("basic_success", result.find("Successfully created") != std::string::npos);
UNIT_TEST("dir_exists", fs::exists(fs::path(dir) / "new_folder"));
```

4. **`LOG_INFO`** — 每個測試區塊開頭記錄，第一個參數是 class/tool 名稱，第二個參數用描述性名稱：
```cpp
LOG_INFO("ini_parser", "parse_success");
```

### Tool 類型測試

- **class Tool 執行流程** — 正常測試要呼叫完整流程：
```cpp
auto args_str = args.dump();
tool->show_arguments(args_str);
tool->show_preview(args_str);
std::string result = tool->execute(args_str);
tool->show_result(result);
```

- **class Tool name 驗證** — 第一個測試要驗證 tool 的 `name()`：
```cpp
auto tool = create_create_directory_tool();
UNIT_TEST("name_is_create_directory", tool->name() == "create_directory");
```

## JSON array 賦值規則（重要）

- **`{...}` 會被 nlohmann::json 的型別推導誤判為 object，所以要用 `json::array(...)` 明確指定型別。**
- 這不是絕對禁止 `{}`，而是當你要建立 array 時必須用 `json::array()` 避免被推導成 object。

```cpp
// ❌ 錯誤 — {obj1, obj2} 會被型別推導為 object（而非 array）
args["files"] = {f1, f2};
args["edits"] = {edit_obj};
edit_obj["operations"] = {op};

// ✅ 正確 — 用 json::array() 明確指定型別為 array
args["files"] = json::array({f1, f2});
args["edits"] = json::array({edit_obj});
edit_obj["operations"] = json::array({op});
```

**例外：已經用 push_back 建立的 array，型別已確定，直接賦值即可。**
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
| `safe_remove_all` 參數名與呼叫端不一致 | 命名不統一容易混淆，參數名必須是 `path` |
| `LOG_INFO` 第一個參數不是 class/tool 名稱 | 應該用 class/tool 名稱，不要用函式名稱（如寫成 `"test_ini_parser"`） |
| 忘記清理臨時目錄 | 測試結束後一定要 `safe_remove_all(dir)` |
| 沒有設定 SafetyGuard 白名單 | 涉及檔案操作的測試必須確保白名單包含當前路徑 |
| `{...}` 賦值給 JSON array | 要用 `json::array({ ... })` |
| 缺少錯誤處理測試 | 每個 tool 都要有 empty / invalid / null input 測試 |
| 子函式沒有宣告為 static | 獨立子測試函式必須使用 `static`，限制連結範圍於該翻譯單元 |
