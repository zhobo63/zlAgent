# Multi-Agent WebSocket 協作架構規劃

## 1. 現狀分析

### 現有問題
- `MultiAgent` 三個子代理在同一進程內同步執行
- 代理間通訊僅透過記憶體共享，無法跨機器/容器擴展
- 無法動態加入新類型的代理（如 Security Agent、Architect Agent）
- 單一代理卡住會阻塞整個流程

### 目標
- 將代理解耦為獨立進程/服務
- 使用 **httplib.h** 內建的 WebSocket（無需額外依賴）作為代理間通訊協議
- 支援動態代理註冊與發現
- 保持現有 API 相容性（`MultiAgent::execute_task()` 介面不變）
- 子代理像是工具一樣 由代理發布任務 處理完後回報結果
  - 子代理連線後像是註冊工具 提供專案名稱 專案簡介
  - 應用情境:
    - 主代理 專案 Lobby (大廳) 目錄 /hg/Lobby
    - 子代理 專案 Report (報表) 目錄 /hg/Report


---

## 2. 設定文件規劃

### 2.1 zlagent.ini 新增區段

```ini
[multi_agent_ws]
enabled = false                    # 是否啟用 WebSocket 協作模式
ws_url = ws://127.0.0.1:8765/ws   # Orchestrator URL（有=Client，無=Server）
listen_port = 8766                 # Agent Server 監聽 port（預設 8766）
heartbeat_interval_sec = 30        # 心跳間隔（秒）
heartbeat_timeout_sec = 90         # 心跳逾時（秒），超過視為離線
max_concurrent_tasks = 5           # 代理同時處理的最大任務數
task_retry_limit = 3               # 任務失敗重試次數
task_retry_delay_ms = 5000         # 重試延遲（毫秒）

[agent_registry]
auto_discover = true               # 是否自動發現其他代理
discovery_ttl_sec = 120            # 代理註冊 TTL（秒），過期自動移除
register_on_start = true           # 啟動時自動向 Broker 註冊

[security]
require_auth = false               # 是否需要 WebSocket 認證
token_file = .ws_token             # API Token 存放檔案
allowed_origins = http://localhost:8765   # CORS 白名單
```

### 2.2 Agent 程式角色（Server + Client）

- WebSocketClient client: singleton
- Server: singleton

同一個 `agent` 程式，根據是否有 `--ws-url` 決定連線方向：

| 參數 | 說明 |
|------|------|
| **無** `--ws-url` | Server — 監聽 port，等待其他代理連線 |
| **有** `--ws-url=ws://...` | Client — 主動連線到指定 Orchestrator |

Agent **沒有角色定義**（coder / reviewer / tester），每個 Agent 只是單純接收任務並執行。

```cpp
// Agent 程式：根據 --ws-url 決定連線方向
int main(int argc, char* argv[]) {
    Config config;
    
    if (config.ws_url() != "") {
        // Client：主動連線 Orchestrator（註冊、回報任務）
        auto& client = WebSocketClient::instance();
        client.connect(config.ws_url());
        
        AgentInfo info;
        info.id = generate_id("agent");
        client.send(register_msg);
    } else {
        // Server：監聽 port，等待其他代理連線
        auto& server = Server::instance();
        server.WebSocket("/ws", handler);
        server.listen("0.0.0.0", config.listen_port());
    }
}
```

### 2.4 Agent 獨立進程（Client only）

Agent **只作為 Client**，連線 Orchestrator：

| 參數 | 說明 |
|------|------|
| `--ws-url=ws://127.0.0.1:8765` | Client — 連線 Orchestrator |

```cpp
// Agent：只作為 Client（所有通訊經過 Orchestrator）
int main(int argc, char* argv[]) {
    Config config;
    
    // Client：連線 Orchestrator
    if (config.ws_url() != "") {
        WebSocketClient client(config.ws_url());
        client.connect();
        client.send(register_msg);
    }
}
```

**注意**：Agent **不再需要 Server 端監聽 port**，所有 Agent 間通訊都透過 Orchestrator 路由。

### 2.5 設定優先級

| 來源 | 優先級 | 說明 |
|------|--------|------|
| Command-line flags | 1（最高） | `--ws-port=8765` / `--broker-url=ws://...` |
| Environment variables | 2 | `ZLAGENT_WS_LISTEN_PORT` / `ZLAGENT_BROKER_URL` |
| zlagent.ini | 3（預設） | 常規設定檔 |

---

## 3. 系統架構

```
┌─────────────────────────────────────────────────────┐
│                    Orchestrator                      │
│              (Coordinator Agent)                     │
│                                                      │
│  ┌──────────┐  WS   ┌──────────┐  WS   ┌──────────┐ │
│  │ Agent    │◄──────►│ Agent    │◄──────►│ Agent    │ │
│  │ #1       │        │ #2       │        │ #3       │ │
│  └──────────┘        └──────────┘        └──────────┘ │
│                                                      │
│  ┌──────────┐  WS   ┌──────────┐                     │
│  │ Agent    │◄──────►│ Agent    │                     │
│  │ #4       │        │ #5       │                     │
│  └──────────┘        └──────────┘                     │
└─────────────────────────────────────────────────────┘

每個 Agent 內部：
┌──────────────────────────────────────┐
│           SubAgent (現有)             │
│                                      │
│  ┌──────────┐    ┌───────────────┐   │
│  │ LLMClient│◄──►│ ToolRegistry  │   │
│  └──────────┘    └───────────────┘   │
│                                      │
│  ┌──────────┐                         │
│  │ Memory   │                         │
│  └──────────┘                         │
└──────────────────────────────────────┘

Agent **沒有角色定義**，每個 Agent 只是單純接收任務並執行。
```

---

## 4. WebSocket 通訊協議設計

### 3.1 訊息格式（JSON）

```json
{
    "type": "task_request",           // 訊息類型
    "id": "req_001",                  // 唯一 ID，用於追蹤請求-回應配對
    "timestamp": 1719584400,          // Unix timestamp (ms)
    "from": "orchestrator",            // 來源代理 ID（通用格式：agent_XX / orchestrator）
    "to": "agent_02",                  // 目標代理 ID（空字串 = broadcast）
    "payload": {
        "task": "Implement binary search in C++",
        "context": {                    // 任務上下文
            "project_root": "/path/to/project",
            "branch": "main",
            "previous_review": null     // 可攜帶前一個代理的審查結果
        }
    }
}
```

**注意**：Agent ID **不使用角色命名**（如 coder_01、reviewer_01），統一使用通用格式 `agent_XX`。

### 3.2 訊息類型定義

| type | 用途 | direction |
|------|------|-----------|
| `task_request` | 委派任務給其他代理 | Orchestrator → Agent |
| `task_response` | 回報任務結果 | Agent → Orchestrator |
| `agent_register` | 代理註冊到 Orchestrator | Agent → Orchestrator |
| `agent_unregister` | 代理取消註冊 | Agent → Orchestrator |
| `heartbeat` | 心跳偵測存活 | Agent ↔ Orchestrator |
| `error_response` | 回報錯誤 | Agent → Orchestrator |

**注意**：訊息類型名稱不使用角色命名（如 review、build），改用通用描述。Agent **沒有角色定義**。所有 Agent 間通訊都必須經過 Orchestrator，因此不存在 Agent → Agent 的訊息類型。

### 3.3 任務回應格式

```json
{
    "type": "task_response",
    "id": "req_001",
    "timestamp": 1719584460,
    "from": "agent_01",
    "to": "orchestrator",
    "status": "success",              // success | failed | needs_review
    "payload": {
        "result": "Created file: src/binary_search.cpp\nAdded unit tests...",
        "files_modified": [
            {"path": "src/binary_search.cpp", "action": "created"},
            {"path": "tests/test_binary_search.cpp", "action": "created"}
        ],
        "needs_review": true,          // 是否需要審查
        "next_step": "code_review"     // 建議的下一步動作（取代 review）
    }
}
```

**注意**：`next_step` 使用通用描述而非角色名稱。

---

## 5. 代理註冊與發現機制

### 4.1 代理能力宣告（Register）

Agent **沒有角色定義**，只宣告自身能力。每個 Agent 等同於一個「工具」：

```json
{
    "type": "agent_register",
    "id": "agent_01",
    "tool_name": "agent",              // Agent = Tool（固定值）
    "project_name": "Lobby",           // 專案名稱（如 Lobby、Report）
    "summary": "Main orchestrator agent for project coordination",  // 專案簡介
    "capabilities": [
        "write_file",
        "edit_file",
        "read_file",
        "run_build"
    ],
    "max_concurrent_tasks": 3,
    "metadata": {
        "version": "1.0.0",
        "hostname": "workstation-01",
        "llm_model": "gpt-4o-mini"
    },
    "workspace": {
        "project_root": "/home/user/my-project",
        "branch": "main",
        "git_remote": "origin https://github.com/user/my-project.git",
        "summary": "A C++17 project implementing a multi-agent code review system with WebSocket communication. Uses httplib.h for HTTP/WebSocket, OpenSSL for HTTPS support."
    }
}
```

**注意**：`tool_name` 固定為 `"agent"`，Agent = Tool。`project_name` + `summary` 讓 Orchestrator 知道這個 Agent 隸屬哪個專案、能提供什麼服務（對應目標中的 Lobby/Report 應用情境）。

### 5.2 Orchestrator 維護的代理目錄

```cpp
struct WorkspaceInfo {
    std::string project_root;       // 專案根目錄路徑
    std::string branch;             // 目前 git branch
    std::string git_remote;         // Git remote URL（origin + URL）
    std::string summary;            // 專案摘要（由 LLM 生成或手動提供）
};

struct AgentInfo {
    std::string id;
    std::string tool_name;          // Agent = Tool（固定為 "agent"）
    std::string project_name;       // 隸屬專案名稱（如 Lobby、Report）
    std::string summary;            // 專案簡介（業務層級，讓 Orchestrator 知道這個 Agent 能提供什麼服務）
    std::vector<std::string> capabilities;   // 代理具備的能力清單
    int max_concurrent_tasks = 1;
    int active_tasks = 0;
    WebSocketConnection* ws_conn = nullptr;
    WorkspaceInfo workspace;        // 代理的工作目錄與專案資訊
    std::chrono::steady_clock::time_point last_heartbeat;
};

class AgentRegistry {
public:
    void register_agent(AgentInfo info);
    void unregister_agent(const std::string& agent_id);
    
    // 根據能力尋找可用代理
    std::vector<AgentInfo> find_agents_by_capability(
        const std::string& capability) const;
    
    // 取得具備指定能力的第一個可用代理
    AgentInfo* get_available_agent_with_capability(
        const std::string& capability) const;
    
    // 根據專案名稱尋找代理（對應 Lobby/Report 應用情境）
    std::vector<AgentInfo> find_agents_by_project(
        const std::string& project_name) const;
    
private:
    std::unordered_map<std::string, AgentInfo> agents_;
};

// WorkspaceInfo 用途說明：
// - project_root: Orchestrator 需要知道檔案操作的路徑範圍，防止代理越權讀寫
// - branch: 任務完成後自動 commit/PR 時使用
// - git_remote: 用於推送程式碼或建立 Pull Request
// - summary: 讓其他代理了解專案結構與技術棧，避免重複造輪子

// AgentInfo.project_name / summary 用途說明：
// - project_name: 區分 Agent 隸屬哪個專案（如 Lobby、Report）
//   - 主代理 → 專案 Lobby → /hg/Lobby
//   - 子代理 → 專案 Report → /hg/Report
// - summary: 業務層級簡介，讓 Orchestrator 知道這個 Agent 能提供什麼服務
```

---

## 5. 任務路由與分發流程

### 5.1 Orchestrator 路由邏輯

```cpp
class OrchestratorAgent {
public:
    // 接收外部任務請求，決定如何拆解並委派
    void on_task_request(const std::string& task_description);
    
private:
    // 將任務拆解為子步驟（可參考現有 TaskPlanner）
    std::vector<TaskStep> decompose_task(const std::string& task);
    
    // 根據步驟所需能力路由到對應代理
    void route_step(const TaskStep& step, const AgentInfo* source_agent = nullptr);
    
    // WebSocket 發送輔助函式
    void send_ws_message(const std::string& agent_id, const JsonMessage& msg);
    
private:
    AgentRegistry registry_;
    LLMClient llm_client_;          // Orchestrator 本身也需與 LLM 互動
    Memory memory_;                 // Orchestrator 的對話記憶
};
```

**注意**：`route_step()` 根據步驟所需的「能力」而非「角色」來路由任務。

### 5.2 任務拆解範例（Agent #1 → Agent #2 → Agent #3）

```
外部請求: "Implement binary search in C++"

Orchestrator 拆解：
├─ Step 1: [具備 write_file + edit_file 能力的 Agent] Write implementation
│   └─ Orchestrator → agent_01 (task_request)
│       └─ agent_01 → orchestrator (task_response, needs_review=true)
│           └─ Orchestrator → agent_02 (code_review_request)
│               └─ agent_02 → orchestrator (code_review_response)
│                   └─ 如果有問題: Orchestrator → agent_01 (fix_task)
├─ Step 2: [具備 run_build + run_test 能力的 Agent] Build & test
│   └─ Orchestrator → agent_03 (compile_test_request)
│       └─ agent_03 → orchestrator (compile_test_response)
```

**注意**：範例中不再使用角色名稱（如 Coder、Reviewer），改用「具備 XX 能力的 Agent」描述。

---

## 6. 心跳與存活偵測

### 6.1 Heartbeat 機制

```json
{
    "type": "heartbeat",
    "id": "hb_001",
    "timestamp": 1719584400,
    "from": "agent_01"
}
```

**注意**：Agent ID 使用通用格式 `agent_XX`，不使用角色命名。

### 6.2 Orchestrator 心跳處理

```cpp
class OrchestratorAgent {
private:
    void on_heartbeat(const std::string& agent_id) {
        auto it = registry_.agents_.find(agent_id);
        if (it != registry_.agents_.end()) {
            it->second.last_heartbeat = std::chrono::steady_clock::now();
        }
    }
    
    // 定期清理無回應的代理
    void cleanup_stale_agents() {
        auto now = std::chrono::steady_clock::now();
        for (auto& [id, info] : registry_.agents_) {
            if (std::chrono::duration_cast<std::chrono::seconds>(
                    now - info.last_heartbeat).count() > HEARTBEAT_TIMEOUT) {
                // 代理已離線，重新路由其進行中的任務
                on_agent_offline(id);
            }
        }
    }
    
private:
    static constexpr int HEARTBEAT_INTERVAL = 30;   // 每 30 秒發心跳
    static constexpr int HEARTBEAT_TIMEOUT = 90;     // 90 秒無回應視為離線
};
```

---

## 7. 錯誤處理與重試機制

### 7.1 錯誤訊息格式

```json
{
    "type": "error_response",
    "id": "req_001",
    "timestamp": 1719584460,
    "from": "agent_01",
    "to": "orchestrator",
    "status": "failed",
    "payload": {
        "error_code": "TOOL_EXECUTION_FAILED",
        "message": "Failed to write file: permission denied",
        "retryable": true,
        "retry_after_ms": 5000,
        "retry_limit": 3              // 統一使用 retry_limit（取代 max_retries）
    }
}
```

**注意**：Agent ID 使用通用格式 `agent_XX`，不使用角色命名。

### 7.2 重試策略

```cpp
class OrchestratorAgent {
private:
    void on_error_response(const JsonMessage& msg) {
        auto retryable = msg.payload.retryable;
        auto max_retries = msg.payload.max_retries.value_or(3);
        
        if (retryable && current_retries[msg.id] < max_retries) {
            // 延遲後重試
            std::this_thread::sleep_for(
                std::chrono::milliseconds(msg.payload.retry_after_ms));
            route_step(current_task_[msg.id]);
        } else {
            // 超過重試次數，回報給使用者
            notify_user("Task failed after retries: " + msg.payload.message);
        }
    }
};
```

---

## 8. 安全性考量

### 8.1 代理認證

- WebSocket 連線時要求提供 API Key / Token
- Orchestrator 驗證代理身份後才接受註冊

```cpp
class AgentAuthenticator {
public:
    bool verify_agent(const std::string& agent_id, 
                      const std::string& token);
};
```

### 8.2 任務隔離

- 每個代理的檔案操作限制在其授權目錄內
- 防止惡意代理讀取/修改其他代理的檔案

### 8.3 LLM API Key 保護

- Orchestrator 不將 LLM API Key 轉發給子代理
- 子代理使用自己的 API Key（如果有的話）或透過 Orchestrator 代理請求

---

## 9. 現有程式碼的改造計畫

### Phase 1: WebSocket 基礎設施（無破壞性變更）

```
新增檔案：
├── include/ws_protocol.h          // WebSocket 通訊協議定義
├── include/ws_agent.h             // WebSocket Agent 抽象類別
├── src/ws_protocol.cpp            // JSON 序列化/反序列化
├── src/ws_agent.cpp               // WebSocket 連線管理
└── src/orchestrator.cpp           // Orchestrator Agent（新）
```

**現有 `MultiAgent` 不變**，新增 `OrchestratorAgent`：

```cpp
// 現有介面維持不變
class MultiAgent {
public:
    std::string execute_task(const std::string& task_description);
};

// 新的 WebSocket 協作版本
class OrchestratorAgent {
public:
    // 與 LLM 互動（取代現有 LLMClient）
    ChatResponse chat(...);
    
    // 管理代理註冊/發現
    void register_agent(const AgentInfo& info);
    void unregister_agent(const std::string& agent_id);
    
    // 路由任務到 WebSocket 代理
    OrchestratorTaskResult execute_task_ws(const std::string& task_description);
};
```

**實作步驟：**

| # | 步驟 | 狀態 |
|---|------|------|
| 1.1 | 建立 `ws_protocol.h` — WebSocket 通訊協議定義（訊息格式、類型） | ⬜ 未完成 |
| 1.2 | 建立 `ws_agent.h/cpp` — WebSocket Agent 抽象類別與連線管理 | ⬜ 未完成 |
| 1.3 | 實作 `ws_protocol.cpp` — JSON 序列化/反序列化 | ⬜ 未完成 |
| 1.4 | 建立 `orchestrator.cpp` — Orchestrator Agent（Broker Server） | ⬜ 未完成 |
| 1.5 | 整合現有 `MultiAgent`，確保無破壞性變更 | ⬜ 未完成 |

### Phase 2: SubAgent 改為 WebSocket Client

```cpp
// 現有 SubAgent（同步，同進程）
class SubAgent {
public:
    std::string execute(const std::string& task);
};

// 新的 WebSocket Agent（跨進程）
class WsSubAgent : public SubAgent {
public:
    // 透過 WebSocket 發送任務並等待回應
    std::string execute(const std::string& task) override;
    
private:
    WebSocketConnection* ws_conn_ = nullptr;
    std::atomic<int> active_tasks_ = 0;
};
```

**實作步驟：**

| # | 步驟 | 狀態 |
|---|------|------|
| 2.1 | 建立 `WsSubAgent` — WebSocket Client 實現 | ⬜ 未完成 |
| 2.2 | 實作任務發送/接收邏輯（含超時處理） | ⬜ 未完成 |
| 2.3 | 整合現有 SubAgent，支援切換模式（同步 → WebSocket） | ⬜ 未完成 |

### Phase 3: Agent 獨立進程（可選）

每個代理可以作為獨立進程執行：

```bash
# Orchestrator — Server（無 --ws-url）
./agent --listen-port=8765

# Agent #1 — Client + Server（同時指定兩者）
./agent --ws-url=ws://127.0.0.1:8765 --listen-port=8766

# Agent #2 — Client + Server
./agent --ws-url=ws://127.0.0.1:8765 --listen-port=8767

# 純 Client（不監聽 port，只連線 Orchestrator）
./agent --ws-url=ws://127.0.0.1:8765
```

**實作步驟：**

| # | 步驟 | 狀態 |
|---|------|------|
| 3.1 | Agent 獨立進程啟動器（CLI 參數解析） | ⬜ 未完成 |
| 3.2 | Orchestrator 自動發現機制（Agent Registry 查詢） | ⬜ 未完成 |

### Phase 4: 心跳與存活偵測

**實作步驟：**

| # | 步驟 | 狀態 |
|---|------|------|
| 4.1 | Agent → Orchestrator Heartbeat 發送（每 N 秒） | ⬜ 未完成 |
| 4.2 | Orchestrator 心跳處理與超時偵測 | ⬜ 未完成 |

**注意**：Agent 間通訊必須經過 Orchestrator，不存在 Server 端連線資訊註冊。

### Phase 5: 錯誤處理與重試機制

**實作步驟：**

| # | 步驟 | 狀態 |
|---|------|------|
| 5.1 | 錯誤訊息格式定義（error_response） | ⬜ 未完成 |
| 5.2 | Orchestrator 重試策略實現（含延遲、最大次數） | ⬜ 未完成 |

**注意**：`retry_limit` / `max_retries` 命名統一為 `retry_limit`。

### Phase 6: 安全性（認證、授權）

**實作步驟：**

| # | 步驟 | 狀態 |
|---|------|------|
| 6.1 | Agent 認證機制（API Key / Token 驗證） | ⬜ 未完成 |
| 6.2 | 任務隔離（檔案操作限制） | ⬜ 未完成 |
| 6.3 | LLM API Key 保護（不轉發給子代理） | ⬜ 未完成 |

### Phase 7: Agent 註冊與發現機制

**實作步驟：**

| # | 步驟 | 狀態 |
|---|------|------|
| 7.1 | Register 訊息格式定義與處理 | ⬜ 未完成 |
| 7.2 | Orchestrator Agent Registry（能力宣告、Server 端連線資訊） | ⬜ 未完成 |

**注意**：Agent Registry **不包含角色映射**，只記錄代理具備的能力清單。

### Phase 8: 任務路由與分發流程

**實作步驟：**

| # | 步驟 | 狀態 |
|---|------|------|
| 8.1 | Orchestrator 路由邏輯（基於 Agent Registry） | ⬜ 未完成 |
| 8.2 | 任務拆解範例（具備 write_file 能力的 Agent → 具備 code_review 能力的 Agent → 具備 run_build 能力的 Agent） | ⬜ 未完成 |
| 8.3 | Agent 間間接通訊（透過 Orchestrator） | ⬜ 未完成 |

**注意**：任務拆解範例不再使用角色名稱，改用能力描述。

### Phase 9: 測試與整合驗證

**實作步驟：**

| # | 步驟 | 狀態 |
|---|------|------|
| 9.1 | WebSocket 連線測試（Agent ↔ Orchestrator） | ⬜ 未完成 |
| 9.2 | 任務路由端到端測試 | ⬜ 未完成 |
| 9.3 | 心跳/重試/錯誤處理整合測試 | ⬜ 未完成 |

---

## 10. 通訊流程圖

### Agent 間間接通訊（透過 Orchestrator）

```
Agent A                    WebSocket               Orchestrator              Agent B
     │                        │                          │                       │
     │── task_response ──────►│                          │                       │
     │   (type: task_resp)    │                          │                       │
     │   (needs_review=true)  │                          │                       │
     │                        ├── route_step ──────────►│                       │
     │                        │   (task_request)         │                       │
     │                        │                          │── execute ───────────►│
     │                        │                          │                       │
     │                        │◄── task_response ◄───────│                       │
     │                        │   (status: success)      │                       │
     │◄── task_response ──────│                          │                       │
     │   (review_status=approved)  │                      │                       │
```

**注意**：Agent ID 使用通用格式（如 agent_01、agent_02），不使用角色名稱。所有 Agent 間通訊都必須經過 Orchestrator，因此不存在 Agent → Agent 的訊息類型。

### Orchestrator 協調完整流程

```
User              Orchestrator         Agent A             Agent B               Agent C
  │                     │                   │                  │                 │
  │── task ────────────►│                   │                  │                 │
  │                     ├── route_step ────►│                  │                 │
  │                     │   (task_request)  │                  │                 │
  │                     │                   │── execute ──────►│                 │
  │                     │                   │                  │                 │
  │                     │◄── task_response ◄───────────────────│                 │
  │                     │   (needs_review)  │                  │                 │
  │                     ├── route_step ───────────────────────►│                 │
  │                     │   (code_review_req)    │                  │                 │
  │                     │                   │◄── code_review_request │                 │
  │                     │                   │                  │── execute ─────►│
  │                     │                   │                  │                 │
  │                     │◄── code_review_response ◄─────────────│                 │
  │                     │   (issues found)  │                  │                 │
  │                     ├── route_step ────►│                  │                 │
  │                     │   (fix_task)      │                  │                 │
  │                     │                   │── execute ──────►│                 │
  │                     │                   │                  │                 │
  │                     │◄── task_response ◄───────────────────│                 │
  │                     │   (status: success) │                │                 │
  │                     ├── route_step ───────────────────────────────────────►│
  │                     │   (compile_test_req)     │                  │                 │
  │                     │                   │                  │                 │── execute ──►│
  │                     │                   │                  │                 │         │
  │                     │◄── compile_test_response ◄───────────────────────────────│
  │                     │   (status: pass)  │                  │                 │
  │◄── result ──────────│                   │                  │                 │
```

**注意**：流程圖中不再使用角色名稱（Coder、Reviewer、Tester），改用 Agent A/B/C。

---

## 11. 未來擴展方向

### 11.1 負載均衡
- Orchestrator 根據代理的 `active_tasks` 數量選擇最閒置的代理
- 支援同一角色的多個實例（如多個 Coder Agent）

### 11.2 任務分片
- 大任務可拆分為子任務，同時派發給多個同角色代理
- 例如：Code Review 可同時審查不同檔案

### 11.3 代理自動擴展
- Kubernetes / Docker Swarm 整合
- 根據負載自動啟動/停止 Agent Pod

### 11.4 跨 LLM 路由
- Orchestrator 可將任務派發給使用不同 LLM 的代理
- 例如：簡單任務用 GPT-4o-mini，複雜任務用 Claude Opus

---

## 12. 實作優先級建議

| # | 階段 | 內容 | 預估工作量 | 狀態 |
|---|------|------|-----------|------|
| **P0** | Phase 1 | WebSocket 基礎設施 + Agent Registry | 2-3 天 | ⬜ 未完成 |
| **P1** | Phase 7 | Agent 註冊與發現機制（Agent Registry） | 1-2 天 | ⬜ 未完成 |
| **P2** | Phase 8 | Orchestrator 路由邏輯（現有管線） | 2-3 天 | ⬜ 未完成 |
| **P3** | Phase 2 | SubAgent → WsSubAgent 改造 | 2-3 天 | ⬜ 未完成 |
| **P4** | Phase 4+5 | 心跳 + 錯誤處理 + 重試 | 1-2 天 | ⬜ 未完成 |
| **P5** | Phase 6 | 安全性（認證、授權） | 2-3 天 | ⬜ 未完成 |
| **P6** | Phase 3 | Agent 獨立進程啟動器 | 2-3 天 | ⬜ 未完成 |
| **P7** | Phase 9 | 測試與整合驗證 | 1-2 天 | ⬜ 未完成 |

---

## 13. 工作情境與流程模擬

### 情境一：單一 Agent 完成任務（Client + Server）

**場景描述**：Agent #1 同時是 Client（連線 Orchestrator）和 Server（監聽 port），接收並執行一個簡單的檔案寫入任務。

```
┌─────────────┐         ┌──────────────┐
│ Agent #1    │         │ Orchestrator │
│ (Client+Server)        │              │
└─────────────┘         └──────────────┘

Step 1: Agent #1 啟動（同時指定 --ws-url + listen_port）
  → WebSocketClient::instance().connect("ws://orchestrator:8765")
  → Server::instance().listen("0.0.0.0", 8766)

Step 2: Agent #1 註冊能力
  Orchestrator ← [agent_register] id=agent_01, capabilities=[write_file, edit_file, read_file]

Step 3: Orchestrator 收到外部任務："Create a README.md for my project"
  → Orchestrator 路由：需要 write_file + edit_file 能力
  → Orchestrator 找到 agent_01（具備寫入檔案能力）

Step 4: Orchestrator 發送任務給 Agent #1
  Orchestrator → [task_request] id=agent_01, task="Create README.md", project=Lobby

Step 5: Agent #1 執行任務
  → read_file (檢查專案結構)
  → write_file (寫入 README.md)
  → edit_file (修正格式)

Step 6: Agent #1 回報結果
  Orchestrator ← [task_response] id=agent_01, status=success, result="README.md created"
```

---

### 情境二：多 Agent 協作完成複雜任務（完整流程）

**場景描述**：Orchestrator 將一個複雜的開發任務拆解，分派給三個 Agent 依序執行。

```
┌───────────┐   ┌───────────┐   ┌───────────┐
│ Agent #1  │   │ Agent #2  │   │ Agent #3  │
│ (Client)  │   │ (Client)  │   │ (Client)  │
│ write+edit│   │ code_review│   │ build+test│
└───────────┘   └───────────┘   └───────────┘

Step 1: 三個 Agent 都註冊能力
  Orchestrator ← [agent_register] id=agent_01, capabilities=[write_file, edit_file]
  Orchestrator ← [agent_register] id=agent_02, capabilities=[code_review]
  Orchestrator ← [agent_register] id=agent_03, capabilities=[run_build, run_test]

Step 2: Orchestrator 收到任務："Implement binary search in C++"
  
  Orchestrator 拆解：
  ├─ Step A: Write implementation (需要 write_file + edit_file)
  │   → Orchestrator → agent_01 [task_request, task="Write binary_search.cpp"]
  │       → Agent #1 執行寫入檔案操作
  │       → Orchestrator ← agent_01 [task_response, status=success, needs_review=true]
  │           → Orchestrator 檢查：需要 code review
  │           → Orchestrator → agent_02 [code_review_request, file="binary_search.cpp"]
  │               → Agent #2 執行 code review
  │               → Orchestrator ← agent_02 [code_review_response, status=success]
  │                   → Orchestrator: Review OK，進入 Step B
  
  ├─ Step B: Build & test (需要 run_build + run_test)
  │   → Orchestrator → agent_03 [compile_test_request, file="binary_search.cpp"]
  │       → Agent #3 執行編譯與測試
  │       → Orchestrator ← agent_03 [compile_test_response, status=success]
  
  └─ Step C: Commit & PR (需要 git push)
      → Orchestrator → agent_01 [commit_task, branch="feature/binary-search"]
          → Agent #1 執行 git commit + push

Step 3: 任務完成，Orchestrator 回報外部請求者
```

---

---

### 情境三：Agent 間間接通訊（透過 Orchestrator）

**場景描述**：Agent #1 完成寫入後，請求 Orchestrator 將 code review 任務派發給 Agent #2。

```
┌───────────┐         ┌──────────────┐         ┌───────────┐
│ Agent #1  │────────▶│ Orchestrator │────────▶│ Agent #2  │
│ (Client)  │ ◀────── │              │ ◀────── │ (Client)  │
└───────────┘         └──────────────┘         └───────────┘

Step 1: Agent #1 完成寫入檔案操作
  → on_task_complete(response) { needs_review = true }

Step 2: Agent #1 回報 Orchestrator，請求 code review
  Orchestrator ← [task_response] from=agent_01, status=success, needs_review=true

Step 3: Orchestrator 查詢 Registry，找到具備 code_review 能力的 Agent
  → Orchestrator.find_agents_by_capability("code_review") = agent_02

Step 4: Orchestrator 派發 review 任務給 Agent #2
  Orchestrator → [task_request] to=agent_02, task="Review binary_search.cpp"

Step 5: Agent #2 執行 code review
  agent_02 ← [task_request] from=Orchestrator

Step 6: Agent #2 回報結果給 Orchestrator（間接通訊）
  Orchestrator ← [task_response] to=agent_02, status=success

Step 7: Orchestrator 將結果轉發給 Agent #1
  Orchestrator → [task_response] to=agent_01, review_status=approved

Step 8: Agent #1 收到 review 通過，繼續下一步
```

---

### 情境四：跨專案請求被拒絕（安全機制）

**場景描述**：Lobby 專案的 Agent 嘗試透過 Orchestrator 請求 Report 專案的 Agent，但未被授權。

```
┌───────────┐         ┌──────────────┐         ┌───────────┐
│ Lobby     │────────▶│ Orchestrator │         │ Report    │
│ Agent #1  │ ◀────── │              │         │ Agent #3  │
└───────────┘         └──────────────┘         └───────────┘

Step 1: Agent #1（Lobby）完成任務，需要 Report 專案的 Agent #3 協助
  → Orchestrator ← [task_response] from=agent_01, needs_cross_project=true

Step 2: Orchestrator 檢查跨專案授權
  → can_cross_project_request("Report") = false

Step 3: Orchestrator 拒絕跨專案請求，回報 Agent #1
  Orchestrator → [error_response] to=agent_01, reason="Unauthorized cross-project request"

Step 4: Agent #1 回報 Orchestrator，請求授權（如果需要）
  Orchestrator ← [cross_project_request] from=Lobby/agent_01, to=Report/agent_03
  
Step 5: Orchestrator 檢查授權策略
  → Orchestrator 決定：允許 Lobby → Report（如果配置了跨專案信任）
  → Orchestrator → agent_01 [cross_project_granted] from=Lobby/agent_01, to=Report/agent_03

Step 6: Orchestrator 重新派發任務給 Agent #3
  Orchestrator → [task_request] to=agent_03 (now authorized)
```

---

### 情境五：Agent 心跳與存活偵測

**場景描述**：Orchestrator 定期檢查 Agent 是否存活，並處理離線情況。

```
┌───────────┐         ┌──────────────┐
│ Agent #1  │         │ Orchestrator │
│ (Client)  │ ◀────── │              │
└───────────┘ heartbeat│              │
                        └──────────────┘

Step 1: Agent #1 每 30 秒發送心跳
  Orchestrator ← [heartbeat] id=agent_01, timestamp=2024-01-15T10:30:00Z

Step 2: Orchestrator 更新最後心跳時間
  agent_01.last_heartbeat = now()

Step 3: Agent #2 突然斷線（未發送心跳）
  → Orchestrator 每 60 秒檢查一次存活狀態
  
Step 4: Orchestrator 檢測到 Agent #2 超時（超過 90 秒無心跳）
  → Orchestrator 將 agent_02 標記為 offline
  → Orchestrator 從 Registry 中移除 agent_02

Step 5: Orchestrator 重新路由原本分配給 agent_02 的任務
  → Orchestrator 尋找其他具備 code_review 能力的 Agent（如 agent_04）
  
Step 6: Agent #2 重連後重新註冊
  Orchestrator ← [agent_register] id=agent_02, capabilities=[code_review]
```

---

### 情境六：任務失敗與重試機制

**場景描述**：Agent #3 執行編譯時遇到錯誤，Orchestrator 觸發重試。

```
┌───────────┐         ┌──────────────┐
│ Agent #3  │         │ Orchestrator │
│ (Client)  │ ◀────── │              │
└───────────┘ error   │              │
                        └──────────────┘

Step 1: Orchestrator 發送編譯任務給 Agent #3
  Orchestrator → agent_03 [compile_test_request, file="binary_search.cpp"]

Step 2: Agent #3 執行編譯，遇到錯誤
  → compile error: undefined reference to 'BinarySearch::search()'

Step 3: Agent #3 回報錯誤給 Orchestrator
  Orchestrator ← agent_03 [task_error] id=agent_03, status=failure, 
    error="Compilation failed", retry=true
  
Step 4: Orchestrator 檢查重試策略（max_retries=3, current_attempt=1）
  → Orchestrator 決定：重新路由給 Agent #1 修復

Step 5: Orchestrator 發送修復任務給 Agent #1
  Orchestrator → agent_01 [fix_task] id=agent_01, error="undefined reference to 'BinarySearch::search()'"

Step 6: Agent #1 修復錯誤後重新編譯
  Orchestrator ← agent_01 [task_response] status=success
  
Step 7: Orchestrator 再次發送編譯任務給 Agent #3（重試）
  Orchestrator → agent_03 [compile_test_request, file="binary_search.cpp"] (retry)

Step 8: Agent #3 成功編譯與測試
  Orchestrator ← agent_03 [task_response] status=success
```

---

### 情境七：Agent Server + Client 同時運作（完整註冊流程）

**場景描述**：一個 Agent 同時作為 Client（連線 Orchestrator）和 Server（監聽 port），完成完整的註冊與能力宣告。

```
┌───────────┐         ┌──────────────┐
│ Agent #1  │         │ Orchestrator │
│ (Client+Server)       │              │
│ port:8765 │ ◀────── │              │
│ port:8766 │ ◀────── │              │
└───────────┘         └──────────────┘

Step 1: Agent #1 啟動，同時建立 Client + Server
  auto& client = WebSocketClient::instance();
  client.connect("ws://orchestrator:8765");
  
  auto& server = Server::instance();
  server.WebSocket("/ws", handler);
  server.listen("0.0.0.0", 8766);

Step 2: Client 連線成功，發送註冊訊息
  Orchestrator ← [agent_register] 
    id=agent_01,
    tool_name="agent",
    project_name="Lobby",
    capabilities=["write_file", "edit_file", "read_file"],
    server_address="192.168.1.10",  // Agent #1 的 Server IP
    server_port=8766,               // Agent #1 的 Server port
    workspace={
      project_root="/home/user/my-project",
      branch="main"
    }

Step 3: Orchestrator 接收註冊，更新 Registry
  → registry_.register_agent(agent_01)
  
Step 4: Orchestrator 回覆確認
  Orchestrator → agent_01 [registration_ack] status=success, 
    message="Agent registered successfully"

Step 5: Agent #1 的 Server 端開始接收來自其他 Agent 的直接請求
  → handler("/ws", request) {
      if (request.type == "code_review_request") {
        // 處理 code review 請求
      }
    }
```

---

### 情境八：多個 Orchestrator 協調（未來擴展）

**場景描述**：當 Agent 數量增加，單一 Orchestrator 負載過重時，引入多個 Orchestrator。

```
┌───────────┐   ┌──────────────┐   ┌──────────────┐
│ Agent #1  │   │ Orchestrator │   │ Orchestrator │
│ (Client)  │──▶│ #A           │   │ #B           │
└───────────┘   └──────────────┘   └──────────────┘

Step 1: Agent #1 連線到 Orchestrator A（根據負載均衡策略）
  auto& client = WebSocketClient::instance();
  client.connect("ws://orchestrator-a:8765");

Step 2: Orchestrator B 也接收自己的 Agent 群組
  Orchestrator ← [agent_register] id=agent_04, project_name="Report"

Step 3: Orchestrator A 與 B 之間同步 Registry（未來擴展）
  → Orchestrator A ↔ Orchestrator B [registry_sync]

Step 4: Agent #1 需要 Report 專案的 Agent #4 協助
  → Orchestrator A 查詢本地 Registry，發現 agent_04 屬於 Orchestrator B
  → Orchestrator A → Orchestrator B [cross_orchestrator_request, target=agent_04]
```
