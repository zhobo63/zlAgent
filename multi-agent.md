# Multi-Agent WebSocket 協作架構規劃

## 1. 現狀分析

### 現有問題
- `MultiAgent` 三個子代理（Coder / Reviewer / Tester）在同一進程內同步執行
- 代理間通訊僅透過記憶體共享，無法跨機器/容器擴展
- 無法動態加入新類型的代理（如 Security Agent、Architect Agent）
- 單一代理卡住會阻塞整個流程

### 目標
- 將代理解耦為獨立進程/服務
- 使用 **httplib.h** 內建的 WebSocket（無需額外依賴）作為代理間通訊協議
- 支援動態代理註冊與發現
- 保持現有 API 相容性（`MultiAgent::execute_task()` 介面不變）

---

## 2. 設定文件規劃

### 2.1 zlagent.ini 新增區段

```ini
[multi_agent_ws]
enabled = false                    # 是否啟用 WebSocket 協作模式
broker_url = ws://127.0.0.1:8765/ws   # Broker/Orchestrator 的 WebSocket URL（Agent Client 端用）
listen_port = 8766                 # Agent Server 監聽 port（預設 8766，避免與 Orchestrator 衝突）
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

同一個 `agent` 程式，根據啟動參數決定角色：

| 參數 | 角色 | 說明 |
|------|------|------|
| `--role=coder` | Server + Client | 監聽 port 8766（Server），同時連線 Orchestrator（Client） |
| `--broker-url=ws://...` | Client | 主動連線到 Orchestrator |

```cpp
// Agent 程式：同時是 Server（接收請求）和 Client（發起請求）
int main(int argc, char* argv[]) {
    Config config;
    
    // 1. 作為 Server，監聽 port（接收來自其他代理的請求）
    if (config.listen_port() > 0) {
        Server server;
        server.WebSocket("/ws", handler);
        server.listen("0.0.0.0", config.listen_port());
        
        // 2. 作為 Client，連線 Orchestrator（註冊、回報任務）
        if (config.broker_url() != "") {
            WebSocketClient broker_client(config.broker_url());
            broker_client.connect();
            
            AgentInfo info;
            info.id = generate_id("coder");
            info.role = AgentRole::Coder;
            info.workspace.project_root = get_project_root();
            info.workspace.summary = generate_summary_from_llm();
            broker_client.send(register_msg);
        }
    }
}
```

### 2.3 Orchestrator（Broker）角色

Orchestrator **只**是 Server，不主動連線其他代理：

| 參數 | 說明 |
|------|------|
| `--ws-port=8765` | 監聽 port，等待 Agent 連線 |
| `--role=orchestrator` | 指定角色為 Orchestrator（不會作為 Client） |

```cpp
// Orchestrator：只當 Server
int main(int argc, char* argv[]) {
    Config config;
    
    if (config.role() == Role::Orchestrator) {
        Server orchestrator_server;
        orchestrator_server.WebSocket("/ws", handler);
        orchestrator_server.listen("0.0.0.0", config.ws_port());
        
        // 維護 Agent Registry（接收註冊、心跳）
    }
}
```

### 2.4 設定優先級

| 來源 | 優先級 | 說明 |
|------|--------|------|
| Command-line flags | 1（最高） | `--ws-port=8765` / `--broker-url=ws://...` |
| Environment variables | 2 | `ZLAGENT_WS_LISTEN_PORT` / `ZLAGENT_BROKER_URL` |
| zlagent.ini | 3（預設） | 常規設定檔 |

---

## 2. 系統架構

```
┌─────────────────────────────────────────────────────┐
│                    Orchestrator                      │
│              (Coordinator Agent)                     │
│                                                      │
│  ┌──────────┐  WS   ┌──────────┐  WS   ┌──────────┐ │
│  │ Coder    │◄──────►│ Reviewer │◄──────►│ Tester   │ │
│  │ Agent    │        │ Agent    │        │ Agent    │ │
│  └──────────┘        └──────────┘        └──────────┘ │
│                                                      │
│  ┌──────────┐  WS   ┌──────────┐                     │
│  │ Security │◄──────►│ Architect│                     │
│  │ Agent    │        │ Agent    │                     │
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
```

---

## 3. WebSocket 通訊協議設計

### 3.1 訊息格式（JSON）

```json
{
    "type": "task_request",           // 訊息類型
    "id": "req_001",                  // 唯一 ID，用於追蹤請求-回應配對
    "timestamp": 1719584400,          // Unix timestamp (ms)
    "from": "orchestrator",            // 來源代理 ID
    "to": "coder_01",                  // 目標代理 ID（空字串 = broadcast）
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

### 3.2 訊息類型定義

| type | 用途 | direction |
|------|------|-----------|
| `task_request` | 委派任務給其他代理 | Orchestrator → Agent / Agent ↔ Agent |
| `task_response` | 回報任務結果 | Agent → Orchestrator / Agent → Agent |
| `review_request` | 請求審查（Coder → Reviewer） | Coder → Reviewer |
| `review_response` | 回覆審查結果 | Reviewer → Coder |
| `build_request` | 請求編譯/測試 | Coder → Tester |
| `build_response` | 回報編譯/測試結果 | Tester → Coder |
| `agent_register` | 代理註冊到 Orchestrator | Agent → Orchestrator |
| `agent_unregister` | 代理取消註冊 | Agent → Orchestrator |
| `heartbeat` | 心跳偵測存活 | Agent ↔ Orchestrator |
| `error_response` | 回報錯誤 | Agent → Orchestrator / Agent → Agent |

### 3.3 任務回應格式

```json
{
    "type": "task_response",
    "id": "req_001",
    "timestamp": 1719584460,
    "from": "coder_01",
    "to": "orchestrator",
    "status": "success",              // success | failed | needs_review
    "payload": {
        "result": "Created file: src/binary_search.cpp\nAdded unit tests...",
        "files_modified": [
            {"path": "src/binary_search.cpp", "action": "created"},
            {"path": "tests/test_binary_search.cpp", "action": "created"}
        ],
        "needs_review": true,          // 是否需要審查
        "next_step": "reviewer"        // 建議的下一步代理
    }
}
```

---

## 4. 代理註冊與發現機制

### 4.1 代理能力宣告（Register）

```json
{
    "type": "agent_register",
    "id": "coder_01",
    "role": "Coder",
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

### 4.2 Orchestrator 維護的代理目錄

```cpp
struct WorkspaceInfo {
    std::string project_root;       // 專案根目錄路徑
    std::string branch;             // 目前 git branch
    std::string git_remote;         // Git remote URL（origin + URL）
    std::string summary;            // 專案摘要（由 LLM 生成或手動提供）
};

struct AgentInfo {
    std::string id;
    AgentRole role;
    std::vector<std::string> capabilities;
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
    
    // 取得指定角色的第一個可用代理
    AgentInfo* get_available_agent(AgentRole role) const;
    
private:
    std::unordered_map<std::string, AgentInfo> agents_;
};

// WorkspaceInfo 用途說明：
// - project_root: Orchestrator 需要知道檔案操作的路徑範圍，防止代理越權讀寫
// - branch: 任務完成後自動 commit/PR 時使用
// - git_remote: 用於推送程式碼或建立 Pull Request
// - summary: 讓其他代理了解專案結構與技術棧，避免重複造輪子
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
    
    // 根據步驟類型路由到對應代理
    void route_step(const TaskStep& step, const AgentInfo* source_agent = nullptr);
    
    // WebSocket 發送輔助函式
    void send_ws_message(const std::string& agent_id, const JsonMessage& msg);
    
private:
    AgentRegistry registry_;
    LLMClient llm_client_;          // Orchestrator 本身也需與 LLM 互動
    Memory memory_;                 // Orchestrator 的對話記憶
};
```

### 5.2 任務拆解範例（Coder → Reviewer → Tester）

```
外部請求: "Implement binary search in C++"

Orchestrator 拆解:
├─ Step 1: [Coder] Write implementation
│   └─ Orchestrator → coder_01 (task_request)
│       └─ coder_01 → orchestrator (task_response, needs_review=true)
│           └─ Orchestrator → reviewer_01 (review_request)
│               └─ reviewer_01 → orchestrator (review_response)
│                   └─ 如果有問題: Orchestrator → coder_01 (fix_task)
├─ Step 2: [Tester] Build & test
│   └─ Orchestrator → tester_01 (build_request)
│       └─ tester_01 → orchestrator (build_response)
```

### 5.3 代理間直接通訊（不經過 Orchestrator）

```cpp
// Coder 完成後可以直接請求 Reviewer，無需等待 Orchestrator
class SubAgent {
public:
    void on_task_complete(const TaskResponse& response) {
        if (response.needs_review && role_ == AgentRole::Coder) {
            // 直接找 Reviewer 代理
            auto reviewer = registry_.get_available_agent(AgentRole::Reviewer);
            if (reviewer) {
                JsonMessage msg;
                msg.type = "review_request";
                msg.to = reviewer->id;
                msg.payload.review_content = response.result;
                send_ws_message(reviewer->ws_conn, msg);
            }
        }
    }
};
```

---

## 6. 心跳與存活偵測

### 6.1 Heartbeat 機制

```json
{
    "type": "heartbeat",
    "id": "hb_001",
    "timestamp": 1719584400,
    "from": "coder_01"
}
```

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
    "from": "coder_01",
    "to": "orchestrator",
    "status": "failed",
    "payload": {
        "error_code": "TOOL_EXECUTION_FAILED",
        "message": "Failed to write file: permission denied",
        "retryable": true,
        "retry_after_ms": 5000
    }
}
```

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
# 啟動 Coder Agent
./agent --role=coder --ws-port=8765 --llm-url=http://localhost:1234

# 啟動 Reviewer Agent  
./agent --role=reviewer --ws-port=8766 --llm-url=http://localhost:1234

# Orchestrator 自動發現並連線
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

### Phase 5: 錯誤處理與重試機制

**實作步驟：**

| # | 步驟 | 狀態 |
|---|------|------|
| 5.1 | 錯誤訊息格式定義（error_response） | ⬜ 未完成 |
| 5.2 | Orchestrator 重試策略實現（含延遲、最大次數） | ⬜ 未完成 |

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
| 7.2 | Orchestrator Agent Registry（能力宣告、角色映射） | ⬜ 未完成 |

### Phase 8: 任務路由與分發流程

**實作步驟：**

| # | 步驟 | 狀態 |
|---|------|------|
| 8.1 | Orchestrator 路由邏輯（基於 Agent Registry） | ⬜ 未完成 |
| 8.2 | 任務拆解範例（Coder → Reviewer → Tester） | ⬜ 未完成 |
| 8.3 | 代理間直接通訊（不經過 Orchestrator） | ⬜ 未完成 |

### Phase 9: 測試與整合驗證

**實作步驟：**

| # | 步驟 | 狀態 |
|---|------|------|
| 9.1 | WebSocket 連線測試（Agent ↔ Orchestrator） | ⬜ 未完成 |
| 9.2 | 任務路由端到端測試 | ⬜ 未完成 |
| 9.3 | 心跳/重試/錯誤處理整合測試 | ⬜ 未完成 |

---

## 10. 通訊流程圖

### Coder → Reviewer（代理間直接通訊）

```
Coder Agent              WebSocket               Reviewer Agent
     │                        │                          │
     │── task_request ──────►│                          │
     │   (type: review_req)  │                          │
     │                       │── review_request ───────►│
     │                       │   (payload: code content)│
     │                       │                          │
     │                       │◄─── review_response ─────│
     │                       │    (type: review_resp)   │
     │◄── task_response ─────│                          │
     │   (status: needs_fix) │                          │
```

### Orchestrator 協調完整流程

```
User              Orchestrator         Coder Agent       Reviewer Agent      Tester Agent
  │                     │                   │                  │                 │
  │── task ────────────►│                   │                  │                 │
  │                     ├── route_step ────►│                  │                 │
  │                     │   (task_request)  │                  │                 │
  │                     │                   │── execute ──────►│                 │
  │                     │                   │                  │                 │
  │                     │◄── task_response ◄───────────────────│                 │
  │                     │   (needs_review)  │                  │                 │
  │                     ├── route_step ───────────────────────►│                 │
  │                     │   (review_req)    │                  │                 │
  │                     │                   │◄── review_request │                 │
  │                     │                   │                  │── execute ─────►│
  │                     │                   │                  │                 │
  │                     │◄── review_response ◄─────────────────│                 │
  │                     │   (issues found)  │                  │                 │
  │                     ├── route_step ────►│                  │                 │
  │                     │   (fix_task)      │                  │                 │
  │                     │                   │── execute ──────►│                 │
  │                     │                   │                  │                 │
  │                     │◄── task_response ◄───────────────────│                 │
  │                     │   (status: success) │                │                 │
  │                     ├── route_step ───────────────────────────────────────►│
  │                     │   (build_req)     │                  │                 │
  │                     │                   │                  │                 │── execute ──►│
  │                     │                   │                  │                 │         │
  │                     │◄── build_response ◄────────────────────────────────────│
  │                     │   (status: pass)  │                  │                 │
  │◄── result ──────────│                   │                  │                 │
```

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
