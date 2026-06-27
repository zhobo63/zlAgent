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

- **ReadFile** read_file
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

- **EditFileTool** edit_file 流程改變 ✅ Done
1. 顯示diff — 在 agent.cpp reasoning_loop_stream 中，edit_file 先解析 old_text/new_text，呼叫 DiffEdit() 並 LOG_INFO 輸出 diff
2. 如果使用者模式需要(Edit, Always) 顯示User Reply — 在顯示 diff 後，若 mode 為 Edit/Always 則 prompt_user_reply()
3. 如果使用者回復yes 或是不需要使用者回復 執行寫入 — 使用者按 yes 或 Off 模式下直接繼續執行

附帶修復：
- command_handlers.cpp: /reply help text 中的 on_error 改為正確的 exec/edit/always
- command_handlers.cpp: 新增 /reply [mode] 命令處理器（之前只有 help 文字但沒有實際 handler）

- **WriteFileTool** write_file 流程改變 ✅ **已修復**
1. 如果檔案存在 讀取存在檔案 顯示diff
2. 如果使用者模式需要(Edit, Always) 顯示User Reply — 在顯示 diff 後，若 mode 為 Edit/Always 則 prompt_user_reply()
3. 如果使用者回復yes 或是不需要使用者回復 執行寫入 — 使用者按 yes 或 Off 模式下直接繼續執行

## 其他觀察

- **`include/pch.h`** — 檢查是否有不必要的 include

- [INFO ] [Memory] Context compressed via summarization. 之後就壞了

---

## P1 — Isocline 自動補全功能

- auto_complete.cpp

### [x] 1. isocline 自動補全 — 註冊補全回呼函數
**文件:** `include/completion.h`, `src/completion.cpp`, `src/main.cpp`

✅ 已完成：
- 建立 `completion.h` / `completion.cpp`，定義 `register_completion()`
- 實作 `on_completion()` 回呼函數，支援所有 slash commands 的 Tab 補全
- 在 `main.cpp` 中於 `ic_init()` 之後呼叫 `agent::register_completion()`
- CMakeLists.txt 已加入 `src/completion.cpp`

目前 `main.cpp` 使用 isocline (`ic_readline`, `ic_set_history`)，但沒有註冊任何自動完成功能。使用者按下 Tab 鍵時不會有任何提示。

**建議實作：**

#### a) 建立補全回呼函數
```cpp
// 新增於 main.cpp 或 command_dispatcher.h/cpp

void completions(const char *text, int start, int end) {
    // text: 目前輸入的文字（尚未補全的部分）
    // start: 補全開始的游標位置
    // end:   補完結束的游標位置
    
    if (strlen(text) == 0) {
        // text 為空時，顯示所有可用命令
        icl_add_completion("/help");
        icl_add_completion("/status");
        icl_add_completion("/config");
        icl_add_completion("/skills");
        icl_add_completion("/model");
        icl_add_completion("/facts");
        icl_add_completion("/sessions");
        icl_add_completion("/new");
        icl_add_completion("/summary");
        icl_add_completion("/clear-memory");
        icl_add_completion("/save-session");
        icl_add_completion("/search-kb");
        icl_add_completion("/add-doc");
        icl_add_completion("/quit");
        icl_add_completion("/exit");
        icl_add_completion("/reply");
    } else {
        // 根據輸入文字過濾命令
        if (strncmp(text, "/h", strlen(text)) == 0) {
            icl_add_completion("/help");
        }
        if (strncmp(text, "/s", strlen(text)) == 0) {
            icl_add_completion("/status");
            icl_add_completion("/sessions");
            icl_add_completion("/summary");
            icl_add_completion("/save-session");
        }
        // ... 其他命令過濾
    }
    
    icl_show_completions();
}
```

#### b) 註冊補全函數（在 main.cpp 的 `ic_init()` 之後）
```cpp
// Initialize isocline for rich console input (handles terminal setup on all platforms)
icl_set_completion(completions);  // ← 新增這行
```

---

### [x] 2. 命令參數自動補全 — /model, /facts, /sessions, /search-kb, /add-doc
**文件:** `src/completion.cpp`

✅ 已完成：
- `/model` — 模型編號補全（透過 `get_global_agent()` 取得可用模型）
- `/reply` — 模式補全（off, exec, edit, always）
- `/facts` — prefix 過濾補全（列出已知 fact key）
- `/sessions` — n（數量）補全（建議 5/10/20）
- `/search-kb` — 查詢詞補全（目前無 API，保留擴充空間）
- `/add-doc` — 檔案路徑補全（使用 `ic_complete_filename()`）
- `/config` — 配置補全
- 新增 `get_global_agent()` / `set_global_agent()` 全域存取器

不同命令有不同的參數類型，需要根據上下文提供不同的補全選項：

#### a) `/model` — 模型編號補全
```cpp
void model_arg_completions(const char *text, int start, int end) {
    // 如果使用者輸入 "/model "，則列出可用模型編號
    if (strlen(text) == 0) {
        auto models = ag->get_llm().list_models();
        for (size_t i = 0; i < models.size(); ++i) {
            icl_add_completion(std::to_string(i + 1));
        }
    } else if (isdigit(text[0])) {
        // 如果使用者輸入 "/model 1"，自動補全為 "/model 1<tab>" → "/model 1"
        icl_set_auto_completion(text);
    }
}
```

#### b) `/facts` — prefix 過濾補全
```cpp
void facts_arg_completions(const char *text, int start, int end) {
    auto ltm = get_global_long_term_memory();
    if (!ltm) return;
    
    // 列出所有已知的 fact key 作為補全選項
    auto all_facts = ltm->get_facts("");
    for (const auto& f : all_facts) {
        if (strncmp(text, f.key.c_str(), strlen(text)) == 0) {
            icl_add_completion(f.key);
        }
    }
}
```

#### c) `/sessions` — n（數量）補全
```cpp
void sessions_arg_completions(const char *text, int start, int end) {
    auto ltm = get_global_long_term_memory();
    if (!ltm) return;
    
    // 如果使用者輸入 "/sessions "，建議常見數字
    if (strlen(text) == 0) {
        icl_add_completion("5");
        icl_add_completion("10");
        icl_add_completion("20");
    } else if (isdigit(text[0])) {
        // 如果使用者輸入 "/sessions 1"，自動補全為 "/sessions 1<tab>" → "/sessions 1"
        icl_set_auto_completion(text);
    }
}
```

#### d) `/search-kb` — 查詢詞補全（RAG knowledge base）
```cpp
void search_kb_arg_completions(const char *text, int start, int end) {
    auto rag = get_global_rag_manager();
    if (!rag) return;
    
    // 如果使用者輸入 "/search-kb "，列出知識庫中的常見主題或文件名稱
    // （需要 RAGManager 提供索引查詢 API）
}
```

#### e) `/add-doc` — 檔案/目錄路徑補全
```cpp
void add_doc_arg_completions(const char *text, int start, int end) {
    if (strlen(text) == 0) return;
    
    // 列出符合 text 的檔案或目錄名稱
    std::string dir = text.substr(0, text.find_last_of("/\\") + 1);
    std::string prefix = text.substr(dir.length());
    
    DIR *d = opendir(dir.empty() ? "." : dir.c_str());
    if (!d) return;
    
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strncmp(prefix.c_str(), entry->d_name, strlen(prefix)) == 0) {
            std::string path = dir.empty() ? "" : dir + "/";
            icl_add_completion(path + entry->d_name);
        }
    }
    
    closedir(d);
}
```

#### f) `/reply` — 模式補全（off, exec, edit, always）
```cpp
void reply_arg_completions(const char *text, int start, int end) {
    if (strlen(text) == 0) {
        icl_add_completion("off");
        icl_add_completion("exec");
        icl_add_completion("edit");
        icl_add_completion("always");
    } else if (strncmp(text, "o", strlen(text)) == 0) {
        icl_set_auto_completion("off");
    } else if (strncmp(text, "e", strlen(text)) == 0) {
        // exec vs edit — 需要更多上下文判斷
        icl_add_completion("exec");
        icl_add_completion("edit");
    } else if (strncmp(text, "a", strlen(text)) == 0) {
        icl_set_auto_completion("always");
    }
}
```

---

### [x] 3. 上下文感知的補全 — 根據命令類型切換回呼函數
**文件:** `src/completion.cpp`

✅ 已完成：
- `on_completion()` 為統一入口點，判斷是否正在輸入命令（以 / 開頭）
- 沒有參數時顯示所有可用命令
- 有參數時根據命令類型切換補全策略（model → model_arg_completer, reply → reply_arg_completer, etc.）

isocline 的 `icl_set_completion()` 只接受單一回呼函數。為了支援不同命令的不同參數補全，需要一個統一的入口點：

```cpp
void context_aware_completions(const char *text, int start, int end) {
    // 判斷目前是否正在輸入命令（以 / 開頭）
    if (strlen(text) == 0 || text[0] != '/') {
        // 非命令模式 — 不顯示補全或顯示其他選項
        return;
    }
    
    auto tokens = tokenize_command(text);  // 分割命令和參數
    
    if (tokens.size() <= 1) {
        // 只有命令，沒有參數 — 顯示所有可用命令（如果 text 不完整）
        command_completions(text, start, end);
    } else {
        // 有參數 — 根據命令類型切換補全策略
        const std::string& cmd = tokens[0];
        
        if (cmd == "/model") {
            model_arg_completions(tokens[1].c_str(), start, end);
        } else if (cmd == "/facts") {
            facts_arg_completions(tokens[1].c_str(), start, end);
        } else if (cmd == "/sessions") {
            sessions_arg_completions(tokens[1].c_str(), start, end);
        } else if (cmd == "/search-kb") {
            search_kb_arg_completions(tokens[1].c_str(), start, end);
        } else if (cmd == "/add-doc") {
            add_doc_arg_completions(tokens[1].c_str(), start, end);
        } else if (cmd == "/reply") {
            reply_arg_completions(tokens[1].c_str(), start, end);
        }
    }
}

// 在 main.cpp 中註冊：
icl_set_completion(context_aware_completions);
```

---

### [x] 4. 自動補全（單次 Tab）— 唯一匹配時自動插入
**文件:** `src/completion.cpp`

✅ 已完成：
- 在 `register_completion()` 中呼叫 `ic_enable_auto_tab(true)`
- isocline 內建支援：單次 Tab = 唯一匹配自動補全，雙擊 Tab = 顯示所有選項

當只有一個匹配結果時，isocline 可以自動將該選項插入到命令列中：

```cpp
// 在 command_completions() 中：
void command_completions(const char *text, int start, int end) {
    // ... 過濾邏輯 ...
    
    if (matches.size() == 1) {
        // 唯一匹配 — 自動補全（單次 Tab）
        icl_set_auto_completion(matches[0]);
    } else if (matches.size() > 1) {
        // 多個匹配 — 顯示列表（多次 Tab）
        for (const auto& m : matches) {
            icl_add_completion(m);
        }
        icl_show_completions();
    }
}
```

---

### [x] 5. 命令參數的自動補全（單次 Tab）— 數字/路徑等
**文件:** `src/completion.cpp`

✅ 已完成：
- `ic_enable_auto_tab(true)` 啟用單次 Tab 唯一匹配自動插入
- `/model`、`/sessions` 等數字參數支援單次 Tab 補全

對於 `/model`, `/sessions` 等需要輸入數字的命令，當只有一個可能的數字時自動插入：

```cpp
void model_arg_completions(const char *text, int start, int end) {
    auto models = ag->get_llm().list_models();
    
    std::vector<std::string> matches;
    for (size_t i = 0; i < models.size(); ++i) {
        if (strncmp(text, std::to_string(i + 1).c_str(), strlen(text)) == 0) {
            matches.push_back(std::to_string(i + 1));
        }
    }
    
    if (matches.size() == 1) {
        icl_set_auto_completion(matches[0]);
    } else if (matches.size() > 1) {
        for (const auto& m : matches) {
            icl_add_completion(m);
        }
        icl_show_completions();
    }
}
```

---

### [x] 6. 檔案路徑補全 — /add-doc 的完整實作
**文件:** `src/completion.cpp`

✅ 已完成：
- `/add-doc` 使用 isocline 內建的 `ic_complete_filename()` 進行檔案/目錄路徑補全
- Windows 下自動使用 `\` 作為目錄分隔符，Unix 下使用 `/`
- 支援目錄跳躍、檔案跳躍、相對路徑補全

對於 `/add-doc`，需要支援：
- **目錄跳躍**：輸入 `/add-doc src/` → Tab → `/add-doc src/<dir>/`
- **檔案跳躍**：輸入 `/add-doc src/m` → Tab → `/add-doc src/main.cpp`
- **相對路徑補全**：自動處理 `./`, `../` 等前綴

```cpp
void add_doc_arg_completions(const char *text, int start, int end) {
    if (strlen(text) == 0) return;
    
    // 分離目錄和檔名部分
    std::string dir_part = text;
    auto last_slash = text.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        dir_part = text.substr(0, last_slash + 1);
    } else {
        dir_part = "";
    }
    
    std::string prefix = last_slash == std::string::npos ? text : text.substr(last_slash + 1);
    
    // 開啟目錄並列出匹配項目
    DIR *d = opendir(dir_part.empty() ? "." : dir_part.c_str());
    if (!d) return;
    
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strncmp(prefix.c_str(), entry->d_name, strlen(prefix)) == 0) {
            std::string path = dir_part.empty() ? "" : dir_part;
            
            // 如果是目錄，加上 / 結尾（Unix）或 \ 結尾（Windows）
            bool is_dir = false;
#ifdef _WIN32
            struct stat st;
            if (stat((dir_part + entry->d_name).c_str(), &st) == 0) {
                is_dir = S_ISDIR(st.st_mode);
            }
#else
            // Unix: 使用 lstat 檢查
#endif
            
            path += entry->d_name;
            if (is_dir) path += "/";
            
            icl_add_completion(path.c_str());
        }
    }
    
    closedir(d);
}
```

---

### [x] 7. isocline API 對照表（供實作參考）

> **注意：** todo.md 原始版本使用 `icl_` 前綴，但實際 isocline API 使用 `ic_` 前綴。

| API | 說明 |
|-----|------|
| `ic_set_default_completer(callback, arg)` | 註冊補全回呼函數 |
| `ic_add_completion(cenv, text)` | 添加一個補全選項 |
| `ic_add_completions(cenv, prefix, completions[])` | 批量添加補全選項（null-terminated） |
| `ic_complete_word(cenv, prefix, fun, is_word_char)` | 對當前單詞進行補全 |
| `ic_complete_filename(cenv, prefix, sep, roots, exts)` | 檔案路徑補全 |
| `ic_enable_auto_tab(true)` | 啟用自動 Tab（唯一匹配時自動插入） |

---

### [x] 8. 實作順序建議

1. **Step 1** — 基礎命令補全（/help, /status, /config...）
2. **Step 2** — `/model` 參數補全（模型編號）
3. **Step 3** — `/reply` 參數補全（off, exec, edit, always）
4. **Step 4** — `/facts`, `/sessions` 參數補全
5. **Step 5** — `/add-doc` 檔案路徑補全
6. **Step 6** — `/search-kb` 查詢詞補全（需要 RAGManager API）
7. **Step 7** — 優化：自動補全（單次 Tab）、上下文感知切換

---
