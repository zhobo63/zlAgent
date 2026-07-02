# ZL Agent 優化清單

## P0 — 關鍵 Bug / 穩定性

---

### [ ] 2. RAG Manager — unsafe static_cast
**文件:** `src/rag_manager.cpp:19,51,85`

```cpp
static_cast<TfidfEmbeddingProvider*>(provider_)
```

若實際是 LLMEmbeddingProvider 會導致未定義行為。

**建議:** 改用 `dynamic_cast` + nullptr 判斷。

---

### [x] 5. `LLMClient` — 沒有 HTTP 連接池 / keep-alive
**文件:** `src/llm_client.cpp:49-77`

每次 HTTP 請求都創建新的 `httplib::Client` 實例，意味著每次都建立 TCP 連接。對於 Agent 的推理循環（可能多次調用 LLM），這會浪費大量時間在握手和 TLS 協商上。

**建議:** 緩存 Client 實例或使用 keep-alive 連接。考慮將 `httplib::Client` 作為成員變量，而非每次請求新建。

**完成:** 將 `httplib::Client` / `SSLClient` 改為 `LLMClient` 的成員變量（`mutable std::unique_ptr<httplib::Client> client_`），首次使用時懶初始化並設置超時。後續所有 `post_json`、`chat_stream`、`list_models` 都復用同一個 Client 實例，利用 HTTP keep-alive 避免重複的 TCP/TLS 握手。

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

### [x] 9. Task Planner — fallback 解析缺少步驟驗證
**文件:** `src/task_planner.cpp:127-138`

fallback 路徑捕獲到 step regex 就直接 push，沒有檢查步驟是否非空或長度合理。

**建議:** 加入基本驗證（non-empty、合理長度）。

**已修復:** 加入描述驗證 — 跳過空字串與少於 2 字元的噪音條目，超過 500 字元則截斷。
