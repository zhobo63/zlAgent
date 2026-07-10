---
name: unit-test-conventions
description: Conventions for writing unit tests in the zlAgent project. Use when creating or modifying test files under unit_test/.
---
# 單元測試規範

## 基本結構

每個測試函式接收 `UnitReport& parent`，建立自己的 `UnitReport unit("test_name")`，最後 push 到 parent。

```cpp
void test_my_tools(UnitReport& parent)
{
    UnitReport unit("my_tools");
    LOG_INFO("test_my_tools", "my_tools");

    // ... tests ...

    parent.report.push_back(unit);
}
```

## 測試區塊

每個獨立測試用 `{}` 包起來，裡面包含：log、建立環境、執行工具、驗證、清理。

```cpp
{
    LOG_INFO("tool_name", "test_case_name");
    std::string dir = "test_some_temp";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(dir);

    // 建立測試檔案
    std::ofstream out(fs::path(dir) / "hello.txt");
    out << "content\n";
    out.close();

    auto tool = create_some_tool();
    json args;
    args["key"] = "value";
    auto args_str = args.dump();
    tool->show_arguments(args_str);
    tool->show_preview(args_str);
    std::string result = tool->execute(args_str);
    tool->show_result(result);
    UNIT_TEST("test_case_name", result.find("expected") != std::string::npos);

    safe_remove_all(dir);  // 清理
}
```

## 必要元素

### 1. safe_remove_all — 每個測試檔案都要有
```cpp
static void safe_remove_all(const std::string& path)
{
    try {
        if (fs::exists(path)) fs::remove_all(dir);
    } catch (...) {}
}
```

### 2. 臨時目錄命名 — 用 `test_<tool>_<case>_temp`，測試結束一定要清理
```cpp
std::string dir = "test_write_file_temp";
if (fs::exists(dir)) fs::remove_all(dir);
fs::create_directories(dir);
// ... test ...
safe_remove_all(dir);
```

### 3. UNIT_TEST — 測試名稱要有描述性，不要重複
```cpp
UNIT_TEST("basic_search_matches", result.find("main") != std::string::npos);
UNIT_TEST("no_error_on_write", result.find("Error") == std::string::npos);
```

### 4. LOG_INFO — 每個測試區塊開頭記錄
```cpp
LOG_INFO("tool_name", "test_case_name");
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

## 測試覆蓋範圍

每個 tool 至少要涵蓋：
- **基本功能** — 正常輸入產生預期輸出
- **檔案過濾/參數變化** — 不同參數組合的行為
- **錯誤處理** — empty input、invalid JSON、empty required field、invalid regex 等

## 常見驗證模式

```cpp
// 結果包含某字串
UNIT_TEST("has_expected", result.find("expected") != std::string::npos);

// 結果不包含 Error
UNIT_TEST("no_error", result.find("Error") == std::string::npos);

// 檔案存在
UNIT_TEST("file_exists", fs::exists(fs::path(dir) / "file.txt"));

// 讀取檔案內容驗證
std::ifstream in(fs::path(dir) / "file.txt");
std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
UNIT_TEST("content_matches", content == "expected");
```
