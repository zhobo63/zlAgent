---
name: nlohmann-json-array
description: Rules for correctly creating JSON arrays with nlohmann::json. Use when writing C++ code that assigns arrays to json objects.
---
# nlohmann::json Array 用法規則

## 核心問題

```cpp
// ❌ 錯誤 — {obj1, obj2} 會被解析為 object，不是 array
args["files"] = {f1, f2};

// ✅ 正確 — 明確使用 json::array()
args["files"] = json::array({f1, f2});
```

## 規則

### 賦值給 json 的 array 欄位時，一律用 `json::array(...)`

```cpp
// 單一元素
args["files"] = json::array({file_obj});
args["edits"] = json::array({edit_obj});

// 多個元素
args["files"] = json::array({f1, f2, f3});
edit_obj["operations"] = json::array({op1, op2});

// ops 已經用 push_back 建立 → 直接賦值，不要用 json::array() 包
json ops;
ops.push_back(op1);
ops.push_back(op2);
edit_obj["operations"] = ops;

// 空陣列
args["files"] = json::array();
```

### push_back 也適用同樣邏輯

```cpp
// ✅ 正確
json arr = json::array();
arr.push_back(obj);
args["files"] = arr;

// ❌ 錯誤 — 這樣會變成 object
json arr;
arr.push_back(obj);  // arr 是 null，push_back 後變成 object
```

### 檢查是否為 array

```cpp
if (!args.contains("files") || !args["files"].is_array()) {
    return "Error: 'files' array is required.";
}
```

## 為什麼會出錯？

`json = {obj1, obj2}` 會被 nlohmann::json 推導為 `object`（因為 `{}` 是 object initializer），而不是 `array`。必須用 `json::array({})` 明確指定型別。
