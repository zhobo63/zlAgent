# ZL Agent 優化清單

## P0 — 關鍵 Bug / 穩定性

### [ ] 1. `build_chat_json` — JSON 序列化失敗後靜默返回空串
**文件:** `src/llm_client.cpp:184-193`

```cpp
try { req_str = req.dump(); }
catch(const std::exception& e) {
    LOG_ERROR("LLMClient", "Failed to serialize chat request: " + std::string(e.what()));
}
return req_str;  // ← 空字符串！LLM 調用靜默失敗。
```

**建議:** 拋出異常或返回 `std::optional<std::string>`，讓上層知道請求構建失敗。

---

### [ ] 2. RAG Manager — unsafe static_cast
**文件:** `src/rag_manager.cpp:19,51,85`

```cpp
static_cast<TfidfEmbeddingProvider*>(provider_)
```

若實際是 LLMEmbeddingProvider 會導致未定義行為。

**建議:** 改用 `dynamic_cast` + nullptr 判斷。

---

### [ ] 3. write_json_file() — 失敗靜默返回
**文件:** `src/vector_store.cpp:11-16`

```cpp
static void write_json_file(const std::string& path, const json& j) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        LOG_ERROR("VectorStore", "Failed to open file for writing: " + path);
        return;  // ← 沒有向上層報告錯誤。
    }
    ofs << j.dump(2);
}
```

**建議:** 拋出異常或返回 bool。

---

### [ ] 4. `chat_stream_impl` — 每次讀取都 LOG 整個 buffer
**文件:** `src/llm_client.cpp:434`

```cpp
LOG(Color::GRAY, "%s", buffer.c_str());
```

buffer 會隨著時間增長，這意味著每次讀取都會打印越來越大的字符串。I/O 開銷隨時間線性增長。

**建議:** 只 LOG 新讀取的數據（`read_buf`），而非累積的 `buffer`；或移除這條 LOG。

---

### [ ] 5. `LLMClient` — 沒有 HTTP 連接池 / keep-alive
**文件:** `src/llm_client.cpp:49-77`

每次 HTTP 請求都創建新的 `httplib::Client` 實例，意味著每次都建立 TCP 連接。對於 Agent 的推理循環（可能多次調用 LLM），這會浪費大量時間在握手和 TLS 協商上。

**建議:** 緩存 Client 實例或使用 keep-alive 連接。考慮將 `httplib::Client` 作為成員變量，而非每次請求新建。

---

### [ ] 6. 全局狀態管理不安全 (g_rag_manager, g_long_term_memory)
**文件:** `src/rag_manager.cpp:9-18`, `src/long_term_memory.cpp:12-18`

無線程安全、無清理機制，測試難以隔離。

**建議:** 改用依賴注入或 unique_ptr 單例。

---

### [ ] 8. document_chunker.cpp — 配置值未驗證
**文件:** `src/document_chunker.cpp:68,90-94`

chunk_size、overlap 可能為 0 或不合理大，未做任何校驗。

**建議:** 在建構子或 setter 中驗證配置範圍。

---

### [ ] 9. Task Planner — fallback 解析缺少步驟驗證
**文件:** `src/task_planner.cpp:127-133`

fallback 路徑捕獲到 step regex 就直接 push，沒有檢查步驟是否非空或長度合理。

**建議:** 加入基本驗證（non-empty、合理長度）。

---

### [ ] **ReadFile** read_file
  - start_line, end_line 如果沒有讀整個檔案
  - 如果整個檔案 檔案太大 回傳outline 並標明標題區塊行數
範例:
```
SUCCESS: File outline retrieved. This file is too large to read all at once, so the outline below shows the file's structure with line numbers.

IMPORTANT: Do NOT retry this call without line numbers - you will get the same outline.
Instead, use the line numbers below to read specific sections by calling this tool again with start_line and end_line parameters.

# File outline for F:\hg\zlagent\tools\file_tool.cpp

namespace agent [L9-576]
 class FileTool [L12-45]
  std::string name() [L14]
  std::string description() [L15-17]
  std::string parameters_schema() [L18-25]
   json schema [L19]
```
---

### [ ] CLI 模式
cli_mode 設定的 model_name不寫入config
parameter:
  -m <model_name>
  -p prompt
