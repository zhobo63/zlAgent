# ZL Agent - 單元測試計畫

## 框架選擇：Catch2 (header-only)

- **零編譯依賴** — FetchContent 自動下載，無需手動安裝
- **單一 include** — `#include <catch2/catch_all.hpp>`
- **自描述測試** — `TEST_CASE("name", "[tag]")` + `REQUIRE()` / `CHECK()`

## 建構方式

```bash
cmake --build build --target tests
./build/tests.exe
```

## 測試範圍

| # | 模組 | 檔案 | 測試數 | 關鍵場景 |
|---|------|------|--------|----------|
| 1 | Config | `test_config.cpp` | ~8 | INI parse, load defaults, override values, RAG config |
| 2 | Memory | `test_memory.cpp` | ~6 | add/get/clear, sliding window, system prompt replace |
| 3 | ToolRegistry | `test_tool_registry.cpp` | ~4 | register/execute/find, unknown tool error |
| 4 | DocumentChunker | `test_document_chunker.cpp` | ~8 | paragraph-aware split, overlap, extension filter |
| 5 | VectorStore | `test_vector_store.cpp` | ~10 | cosine similarity, top-K, JSON roundtrip |
| 6 | TfidfEmbeddingProvider | `test_tfidf_embedding.cpp` | ~6 | vocabulary building, L2 normalization |
| 7 | CommandDispatcher | `test_command_dispatcher.cpp` | ~8 | dispatch, tokenize, unknown command |
| 8 | SafetyGuard | `test_safety_guard.cpp` | ~6 | path whitelist, injection detection |
| 9 | SkillLoader | `test_skill_loader.cpp` | ~4 | SKILL.md parse, directory scan |
| 10 | RAGManager (mock) | `test_rag_manager.cpp` | ~6 | add/search with MockEmbeddingProvider |
| 11 | LongTermMemory (mock) | `test_long_term_memory.cpp` | ~8 | JSON persistence, facts CRUD, session save |

**總計：~74 個測試用例**

## Mock 設計

- **MockEmbeddingProvider** — 返回確定性向量（"hello" → [1.0, 0.0]）
- **臨時目錄** — filesystem tests 使用 `std::filesystem::temp_directory_path()`

使用檔案:test_write.cpp 測試寫檔工具

測試 WriteFileTool 工具 到
1. **文字寫入** - 基本文字寫入
2. **二進位寫入** - 二進位資料寫入
3. **覆蓋寫入** - 覆蓋現有檔案內容
4. **追加寫入** - 在現有檔案末尾追加內容
5. **空檔案寫入** - 寫入空內容
6. **大檔案寫入** - 大量資料寫入
7. **特殊字元處理** - 包含特殊字元的文字
8. **多行文字寫入** - 多行內容
9. **UTF-8/Big5 編碼測試** - 不同編碼的寫入
10. **路徑安全檢查** - 測試 SafetyGuard 的路徑白名單

