# ZL Agent 優化清單

## P0 — 關鍵 Bug / 穩定性

### [x] 4. `run_planned` — replan 後修改正在迭代的 vector（未定義行為）
**文件:** `src/agent.cpp:520`

在 `for (auto& step : plan.steps)` 循環中替換了整個 `plan.steps = new_plan.steps;`，導致迭代器失效。

```cpp
// 現在：UB — 在遍歷時替換 vector
plan.steps = new_plan.steps;
```

**建議:** 改用索引方式遍歷，或在 replan 後跳出循環重新開始；或使用 `std::replace` / splice 操作。

---

### [ ] 18. `build_chat_json` — JSON 序列化失敗後靜默返回空串
**文件:** `src/llm_client.cpp:145-156`

```cpp
try { req_str = req.dump(); }
catch(const std::exception& e) {
    std::cerr << "Failed to serialize chat request: " + std::string(e.what());
}
return req_str;  // ← 空字符串！LLM 調用靜默失敗。
```

**建議:** 拋出異常或返回 `std::optional<std::string>`，讓上層知道請求構建失敗。

---

### [ ] 20. RAG Manager — unsafe static_cast
**文件:** `src/rag_manager.cpp:19,51,85`

```cpp
static_cast<TfidfEmbeddingProvider*>(provider_)
```

若實際是 LLMEmbeddingProvider 會導致未定義行為。

**建議:** 改用 `dynamic_cast` + nullptr 判斷。

---

## P1 — 架構 / DRY

### [x] 3. `chat_stream_impl` — JSON body 重複構建（DRY violation）
**文件:** `src/llm_client.cpp:291-347` vs `build_chat_json()` (L83-156)

Streaming 路徑中手動重建了整個 JSON request，與 `build_chat_json()` 幾乎完全相同的邏輯。

**建議:** 讓 `chat_stream_impl` 調用 `build_chat_json(messages, tools, temperature, max_tokens, true)` 來構建 body。

---

### [x] 5. `parse_sse_data` — 同時處理 streaming 和 non-streaming
**文件:** `src/llm_client.cpp:162-251`

這個函數同時處理 SSE streaming chunks 和非流式完整 JSON response，邏輯混亂。

**建議:** 拆分為兩個獨立函數：
- `parse_sse_chunk()` — 只處理單個 SSE data line
- `parse_full_response()` — 只處理非流式完整 JSON

---

### [x] 21. substr() — 缺少邊界檢查
**文件:** `src/long_term_memory.cpp:217`, `src/safety_guard.cpp:91`, `src/plugin_loader.cpp:189`

```cpp
k.substr(0, prefix.size())
```

若 prefix 比原字串長會拋異常。

**建議:** 改用 `k.compare(0, prefix.size(), prefix) == 0`

---

### [ ] 22. write_json_file() — 失敗靜默返回
**文件:** `src/vector_store.cpp:73`

```cpp
ofs.is_open() 檢查後 return，沒有向上層報告錯誤。
```

**建議:** 拋出異常或返回 bool。

---

## P2 — 性能

### [x] 1. `ToolRegistry::execute` — O(n) 線性查找
**文件:** `src/tool.cpp:23-34`

每次工具調用都要遍歷整個 tools_ vector。

```cpp
// 現在：O(n)
for (const auto& tool : tools_) {
    if (tool->name() == tool_name) { ... }
}
```

**建議:** 改用 `std::unordered_map<std::string, ToolPtr>`，將查找降到 O(1)。

---

### [x] 2. `ToolRegistry::get_tools()` — 不必要的深拷貝
**文件:** `src/tool.cpp:11-13`

```cpp
std::vector<ToolPtr> ToolRegistry::get_tools() const {
    return tools_;  // 每次調用都複製整個 vector
}
```

**建議:** 返回 `const std::vector<ToolPtr>&` 引用。

**結論:** 不可行 — 內部存儲是 `std::unordered_map<std::string, ToolPtr>`，不是 vector。要返回 vector 就必須做轉換，無法避免拷貝。當前實現正確。

---

### [x] 12. `ToolRegistry::get_definitions()` — 沒有 reserve
**文件:** `src/tool.cpp:15-21`

**建議:** `defs.reserve(tools_.size())` 避免多次重新分配。

---

### [ ] 16. `LLMClient` — 沒有 HTTP 連接池 / keep-alive
**文件:** `src/llm_client.cpp:49-77`

每次 HTTP 請求都創建新的 `httplib::Client` 實例，意味著每次都建立 TCP 連接。對於 Agent 的推理循環（可能多次調用 LLM），這會浪費大量時間在握手和 TLS 協商上。

**建議:** 緩存 Client 實例或使用 keep-alive 連接。考慮將 `httplib::Client` 作為成員變量，而非每次請求新建。

---

### [ ] 9. `chat_stream_impl` — 每次讀取都 LOG 整個 buffer
**文件:** `src/llm_client.cpp:432`

```cpp
LOG(Color::GRAY, "%s", buffer.c_str());
```

buffer 會隨著時間增長，這意味著每次讀取都會打印越來越大的字符串。I/O 開銷隨時間線性增長。

**建議:** 只 LOG 新讀取的數據（`read_buf`），而非累積的 `buffer`；或移除這條 LOG。

---

### [x] 10. `chat_stream_impl` — buffer 的 substr 操作
**文件:** `src/llm_client.cpp:485`

```cpp
buffer = buffer.substr(line_start);
```

每次處理完行後都用 `substr` 創建新字符串。

**建議:** 改用 `buffer.erase(0, line_start)` 原地刪除，避免分配新字符串。

---

### [ ] 23. 全局狀態管理不安全 (g_rag_manager, g_long_term_memory)
**文件:** `src/rag_manager.cpp:9-18`, `src/long_term_memory.cpp:12-18`

無線程安全、無清理機制，測試難以隔離。

**建議:** 改用依賴注入或 unique_ptr 單例。

---

### [x] 24. RemoteLog — 使用裸 new/delete
**文件:** `include/remote_log.h:51-56`

```cpp
gInstance = new RemoteLog / delete gInstance
```

**建議:** 改用 `std::make_unique<RemoteLog>()`，RAII 管理。

---

## P3 — 代碼質量 / 可維護性

### [x] 6. `IniParser::parse` — ltrim/rtrim lambda 重複定義
**文件:** `src/config.cpp:23-24, 171-180, 204-215`

ltrim/rtrim lambda 在多個地方重複定義了 **4 次**。

```cpp
// 建議：提取為靜態成員函數或自由函數
static void trim(std::string& s);
```

---

### [x] 7. `SafetyGuard::is_command_dangerous` — 每次都做 tolower 拷貝
**文件:** `src/safety_guard.cpp:23-24`

對於長命令（如編譯指令）這很浪費。

**建議:** 使用逐字符不區分大小寫比較，避免拷貝整個字符串。

---

### [x] 8. `Memory::summarize` — 截斷邏輯太粗暴
**文件:** `src/memory.cpp:86-87`

硬編碼的 2048 字節截斷可能切斷代碼塊或重要上下文。

**建議:** 基於 token 預算動態計算截斷長度；或使用 TokenCounter 來估算。

---

### [x] 11. `TaskPlanner::planning_system_prompt` — JSON key typo
**文件:** `src/task_planner.cpp:18, src/task_planner.cpp:104`

```json
"overall_goa": "<one-line summary>"   // ← 少了 'l'！
```

雖然內部一致，但這個 typo 會讓 LLM 輸出也帶 typo。

**建議:** 修正為 `"overall_goal"`。

---

### [x] 13. `Config::load` — 大量重複的 if (s.count(...)) 模式
**文件:** `src/config.cpp:109-217`

每個 section 都是相同的模式：檢查 key → 轉換類型 → 賦值。

```cpp
// 建議：提取為模板輔助函數
template<typename T>
void read_if_exists(const std::map<std::string, std::string>& s, const char* key, T& target);
```

---

### [x] 14. `reasoning_loop` — User Reply 邏輯重複
**文件:** `src/agent.cpp:195-212` vs `L220-246`

OnError 和 Always 模式的 reply 處理幾乎相同，switch-case 結構完全一樣。

**建議:** 提取為共用函數 `handle_user_reply()`。

---

### [x] 15. `Agent::needs_planning` — 硬編碼的關鍵詞列表
**文件:** `src/agent.cpp:47-75`

已修復：不再使用靜態關鍵詞列表，改為通過 LLM 調用來判斷任務是否需要規劃。短輸入（<30 字符）直接返回 false；LLM 分類失敗時保守地回退到不規劃。

---

### [ ] 17. `SafetyGuard` — 全局靜態變量
**文件:** `src/safety_guard.cpp:54`

```cpp
static std::vector<std::string> g_path_whitelist;
```

使用全局狀態使得測試困難且容易出錯。

**建議:** 改為實例化對象或使用單例模式，將 whitelist 作為成員變量。

---

### [ ] 25. document_chunker.cpp — 配置值未驗證
**文件:** `src/document_chunker.cpp:53`

chunk_size、overlap 可能為 0 或不合理大，未做任何校驗。

**建議:** 在建構子或 setter 中驗證配置範圍。

---

### [ ] 26. Task Planner — fallback 解析缺少步驟驗證
**文件:** `src/task_planner.cpp:104-141`

fallback 路徑捕獲到 step regex 就直接 push，沒有檢查步驟是否非空或長度合理。

**建議:** 加入基本驗證（non-empty、合理長度）。

---

### [x] 27. 錯誤訊息格式不一致
**文件:** 多處 cerr/cout 輸出

已修復：新增 `include/logger.h`，提供統一日誌系統。

- **嚴重級別:** DEBUG / INFO / WARN / ERROR（可通過 INI `[logging] level = info` 配置）
- **格式:** `[HH:MM:SS.fff] [LEVEL] [Component] message`
- **顏色編碼:** DEBUG=灰色, INFO=綠色, WARN=黃色, ERROR=紅色
- **輸出分流:** INFO/DEBUG → stdout；WARN/ERROR → stderr
- **線程安全:** 內建 mutex 保護
- **宏接口:** `LOG_DEBUG/INFO/WARN/ERROR(component, msg)`
- 已遷移所有源文件中的 `std::cout`/`std::cerr` 調用（UI 交互元素除外）

---

## 其他觀察

- **`include/pch.h`** — 預編譯頭文件使用良好，但可檢查是否有不必要的 include 拖慢編譯速度
- **`CMakeLists.txt`** — 缺少 `target_compile_options` 的警告標記（如 `-Wall -Wextra` for GCC/Clang, `/W4` for MSVC）