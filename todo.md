# ZL Agent 優化清單

## P0 — 關鍵 Bug / 穩定性

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

### [ ] 22. write_json_file() — 失敗靜默返回
**文件:** `src/vector_store.cpp:73`

```cpp
ofs.is_open() 檢查後 return，沒有向上層報告錯誤。
```

**建議:** 拋出異常或返回 bool。

---

## P2 — 性能

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

### [ ] 23. 全局狀態管理不安全 (g_rag_manager, g_long_term_memory)
**文件:** `src/rag_manager.cpp:9-18`, `src/long_term_memory.cpp:12-18`

無線程安全、無清理機制，測試難以隔離。

**建議:** 改用依賴注入或 unique_ptr 單例。

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

## P3 — 近期變更追蹤

### [x] CLI 提示符模型顯示格式調整
**文件:** `src/main.cpp:296`

將 `[model_name]` 改為 `(model_name)`，避免與工具名稱的方括號混淆。

---

## 其他觀察

- **`include/pch.h`** — 檢查是否有不必要的 include
- **`CMakeLists.txt`** — 缺少 `target_compile_options` 的警告標記（如 `-Wall -Wextra` for GCC/Clang, `/W4` for MSVC）
- needs_planning需要移去DONE的時候判斷
