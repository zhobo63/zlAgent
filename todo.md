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

### [x] ~~7. `SafetyGuard` — 全局靜態變量~~ ✅ **已修復**
**文件:** `src/safety_guard.cpp:71`

```cpp
// 原始問題：static std::vector<std::string> g_path_whitelist;
// 當前實作：path_whitelist_ 已是實例成員，非全局靜態變量
```

**修復狀態：** whitelist 已改為實例化對象的成員變量 `path_whitelist_`。測試時可創建獨立的 SafetyGuard 實例來隔離測試。

**剩餘潛在問題：** Singleton 模式仍然存在（`get_instance()`），線程安全問題未解決，可作為後續優化。

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
EditFileTool edit_file 流程改變
1.顯示diff
2.如果使用者模式需要(Edit, Always) 顯示User Reply
3.如果使用者回復yes 或是不需要使用者回復 執行寫入

## 其他觀察

- **`include/pch.h`** — 檢查是否有不必要的 include
