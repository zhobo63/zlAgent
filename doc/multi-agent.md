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
- 子代理像是工具一樣 由代理發布任務 處理完後回報結果
  - 子代理有獨自處理任務的能力
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
ws_url = ws://127.0.0.1:8765/ws   # Server URL（有=Client，無=Server）
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
| `--ws-url=ws://...` | Client — 主動連線到指定 Server |
| `--port=88766` | Server — 接受子代理連線 |

Agent **沒有角色定義**（coder / reviewer / tester），每個 Agent 只是單純接收任務並執行。

```cpp
// Agent 程式：根據 --ws-url 決定連線方向
int main(int argc, char* argv[]) {
    string ws_url;
    int listen_port=0;

    Config config;
    ws_url=config.ws_url();
    listen_port=config.listen_port();
    {
      get ws_url from argv if `--ws-url=`
      get listen_port from argv if `--port=`
    }
    
    if (ws_url != "") {
        // Client：主動連線 Server（註冊、回報任務）
        auto& client = WebSocketClient::instance();
        client.connect(ws_url);
        
        AgentInfo info = generate_info();
        client.send(register_msg);
    } 
    if (listen_port != 0) {
        // Server：監聽 port，等待其他代理連線
        auto& server = Server::instance();
        server.WebSocket("/ws", handler);
        server.listen("0.0.0.0", listen_port);
    }
}
```

### 2.5 設定優先級

| 來源 | 優先級 | 說明 |
|------|--------|------|
| Command-line flags | 1（最高） | `--ws-port=8765`, `--ws-url=ws://...` |
| zlagent.ini | 2（預設） | 常規設定檔 |

---

## 3. 系統架構

```

Agent #1 (Server)
  - Agent #2 (Client)
  - Agent #3 (Client)
  - Agent #4 (Client)
  - Agent #5 (Client)


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
    "from": "192.168.1.20:1234",      // 來源代理 IP:Port
    "to": "192.168.1.38:5678",        // 目標代理 IP:Port
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

**注意**：Agent ID **不使用角色命名**（如 coder_01、reviewer_01），統一使用通用格式 `agent_XX`。Server = Agent #1，Clients = Agents #2-5。

### 3.2 訊息類型定義

| type | 用途 | direction |
|------|------|-----------|
| `task_request` | 委派任務給其他代理 | Server → Agent |
| `task_response` | 回報任務結果 | Agent → Server |
| `agent_register` | 代理註冊到 Server | Agent → Server |
| `agent_unregister` | 代理取消註冊 | Agent → Server |
| `heartbeat` | 心跳偵測存活 | Agent ↔ Server |
| `error_response` | 回報錯誤 | Agent → Server |

### 3.3 任務回應格式

```json
{
    "type": "task_response",
    "id": "req_001",
    "timestamp": 1719584460,
    "from": "192.168.1.20:1234",
    "to": "192.168.1.38:5678",
    "status": "success",              // success | failed | needs_review
    "payload": {
        "result": "Created file: src/binary_search.cpp\nAdded unit tests..."
        ,
        "files_modified": [
            {"path": "src/binary_search.cpp", "action": "created"},
            {"path": "tests/test_binary_search.cpp", "action": "created"}
        ],
        "needs_review": true,          // 是否需要審查
    }
}
```

---

## 5. 代理註冊與發現機制

### 4.1 代理能力宣告（Register）

Agent **沒有角色定義**，只宣告自身能力。每個 SubAgent 等同於一個「工具」：

```json
{
    "type": "agent_register",
    "tool_name": "agent",              // Agent = Tool（固定值）
    "project_name": "lobby",           // 專案名稱（如 Lobby、Report）
    "summary": "Main server agent for project coordination",  // 專案簡介
    "capabilities": [
        "task",
        "plan"
    ],
    "max_concurrent_tasks": 3,
    "metadata": {
        "version": "1.0.0",
        "llm_model": "gpt-4o-mini"
    },
    "workspace": {
        "project_root": "/home/user/lobby",
        "branch": "main",
        "git_remote": "origin https://github.com/user/my-project.git",
        "summary": "A C++17 project implementing a multi-agent code review system with WebSocket communication. Uses httplib.h for HTTP/WebSocket, OpenSSL for HTTPS support."
    }
}
```

**注意**：`project_name` + `summary` 讓 Server 知道這個 Agent 隸屬哪個專案、能提供什麼服務（對應目標中的 Lobby/Report 應用情境）。

### 5.2 Server 維護的代理目錄

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
    std::string summary;            // 專案簡介（業務層級，讓 Server 知道這個 Agent 能提供什麼服務）
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
// - project_root: Server 需要知道檔案操作的路徑範圍，防止代理越權讀寫
// - branch: 任務完成後自動 commit/PR 時使用
// - git_remote: 用於推送程式碼或建立 Pull Request
// - summary: 讓其他代理了解專案結構與技術棧，避免重複造輪子

// AgentInfo.project_name / summary 用途說明：
// - project_name: 區分 Agent 隸屬哪個專案（如 Lobby、Report）
//   - 主代理 → 專案 Lobby → /hg/Lobby
//   - 子代理 → 專案 Report → /hg/Report
// - summary: 業務層級簡介，讓 Server 知道這個 Agent 能提供什麼服務
```

---

## 5. 任務路由與分發流程

### 5.1 Server 路由邏輯

```cpp
class ServerAgent {
public:
    // 接收外部任務請求，決定如何拆解並委派
    void on_task_request(const std::string& task);
    
private:
       
    // WebSocket 發送輔助函式
    void send_ws_message(const std::string& agent_id, const JsonMessage& msg);
    
private:
    AgentRegistry registry_;
    LLMClient llm_client_;          // Server 本身也需與 LLM 互動
    Memory memory_;                 // Server 的對話記憶
};
```

### 5.2 任務拆解範例（Agent #1 → Agent #2, Agent #1 → Agent #3）

- 每個Agent都有獨立處理能力 交代任務尤其獨自完成 最後回報任務處理狀況

---

## 6. 心跳與存活偵測

### 6.1 Server 心跳處理

```json
{
    "type": "heartbeat",
    "timestamp": 1719584400,
}
```

### 6.2 Server 心跳處理

- 每 1 秒發心跳
- 5 秒無回應視為離線

---


## 8. 安全性考量

### 8.1 代理認證

- WebSocket 連線時要求提供 (IP, WorkDir, Summery)
- Server 驗證代理身份後才接受註冊
  - 首次連線顯示子代理資訊 (IP, WorkDir, Summery)
  - 使用者確認後 (輸入 Y/n) 儲存子代理資訊 之後連線不需要再次驗證 (判斷IP, WorkDir相符)

```cpp
class AgentAuthenticator {
public:
    bool verify_agent(const std::string& ip, 
                      const std::string& token);
};
```

---

## 9. 現有程式碼的改造計畫

### Phase 1: WebSocket 基礎設施（無破壞性變更）

```
新增檔案：
├── include/ws_protocol.h          // WebSocket 通訊協議定義
├── include/ws_agent.h             // WebSocket Agent 抽象類別
├── src/ws_protocol.cpp            // JSON 序列化/反序列化
├── src/ws_agent.cpp               // WebSocket 連線管理
└── src/ws_server.cpp              // Server Agent（新）
```

**現有 `MultiAgent` 不變**，新增 `NetAgent`：

```cpp
// 現有介面維持不變
class MultiAgent {
public:
    std::string execute_task(const std::string& task_description);
};

// 新的 WebSocket 協作版本
class NetAgent {
public:
    // 與 LLM 互動（取代現有 LLMClient）
    ChatResponse chat(...);
    
    // 管理代理註冊/發現
    void register_agent(const AgentInfo& info);
    void unregister_agent(const std::string& agent_id);
    
    // 路由任務到 WebSocket 代理
    TaskResult execute_task(const std::string& task_description);
};
```

**實作步驟：**

| # | 步驟 | 狀態 |
|---|------|------|
| 1.1 | 建立 `ws_protocol.h` — WebSocket 通訊協議定義（訊息格式、類型） | ⬜ 未完成 |
| 1.2 | 建立 `ws_agent.h/cpp` — WebSocket Agent 抽象類別與連線管理 | ⬜ 未完成 |
| 1.3 | 實作 `ws_protocol.cpp` — JSON 序列化/反序列化 | ⬜ 未完成 |
| 1.4 | 建立 `ws_server.cpp` — Server Agent（websocket Server） | ⬜ 未完成 |
| 1.5 | 整合現有 `MultiAgent`，確保無破壞性變更 | ⬜ 未完成 |
| 2.1 | 建立 `NetAgent` — WebSocket Client 實現 | ⬜ 未完成 |
| 2.2 | 實作任務發送/接收邏輯（含超時處理） | ⬜ 未完成 |

### Phase 3: Agent 獨立進程（可選）

每個代理可以作為獨立進程執行：

```bash
# Server（無 --ws-url）
./agent --listen-port=8765

# Agent #1 — Client + Server（同時指定兩者）
./agent --ws-url=ws://127.0.0.1:8765 --listen-port=8766

# Agent #2 — Client + Server
./agent --ws-url=ws://127.0.0.1:8765 --listen-port=8767

# 純 Client（不監聽 port，只連線 Server)
./agent --ws-url=ws://127.0.0.1:8765
```

**實作步驟：**

| # | 步驟 | 狀態 |
|---|------|------|
| 3.1 | Agent 獨立進程啟動器（CLI 參數解析） | ⬜ 未完成 |
| 3.2 | Server 自動發現機制（Agent Registry 查詢） | ⬜ 未完成 |

### Phase 4: 心跳與存活偵測

**實作步驟：**

| # | 步驟 | 狀態 |
|---|------|------|
| 4.1 | Agent → Server Heartbeat 發送（每 N 秒） | ⬜ 未完成 |
| 4.2 | Server 心跳處理與超時偵測 | ⬜ 未完成 |

**注意**：Agent 間通訊必須經過 Server，不存在 Server 端連線資訊註冊。

### Phase 5: 錯誤處理與重試機制

**實作步驟：**

| # | 步驟 | 狀態 |
|---|------|------|
| 5.1 | 錯誤訊息格式定義（error_response） | ⬜ 未完成 |
| 5.2 | Server 重試策略實現（含延遲、最大次數） | ⬜ 未完成 |

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
| 7.2 | Server Agent Registry（能力宣告、Server 端連線資訊） | ⬜ 未完成 |

**注意**：Agent Registry **不包含角色映射**，只記錄代理具備的能力清單。

### Phase 8: 任務路由與分發流程

**實作步驟：**

| # | 步驟 | 狀態 |
|---|------|------|
| 8.1 | Server 路由邏輯（基於 Agent Registry） | ⬜ 未完成 |
| 8.2 | 任務拆解範例（具備 write_file 能力的 Agent → 具備 code_review 能力的 Agent → 具備 run_build 能力的 Agent） | ⬜ 未完成 |
| 8.3 | Agent 間間接通訊（透過 Server） | ⬜ 未完成 |

**注意**：任務拆解範例不再使用角色名稱，改用能力描述。

### Phase 9: 測試與整合驗證

**實作步驟：**

| # | 步驟 | 狀態 |
|---|------|------|
| 9.1 | WebSocket 連線測試（Agent ↔ Server） | ⬜ 未完成 |
| 9.2 | 任務路由端到端測試 | ⬜ 未完成 |
| 9.3 | 心跳/重試/錯誤處理整合測試 | ⬜ 未完成 |

---

## 12. 實作優先級建議

| # | 階段 | 內容 | 預估工作量 | 狀態 |
|---|------|------|-----------|------|
| **P0** | Phase 1 | WebSocket 基礎設施 + Agent Registry | 2-3 天 | ⬜ 未完成 |
| **P1** | Phase 7 | Agent 註冊與發現機制（Agent Registry） | 1-2 天 | ⬜ 未完成 |
| **P2** | Phase 8 | Server 路由邏輯（現有管線） | 2-3 天 | ⬜ 未完成 |
| **P3** | Phase 2 | NetAgent | 2-3 天 | ⬜ 未完成 |
| **P4** | Phase 4+5 | 心跳 + 錯誤處理 + 重試 | 1-2 天 | ⬜ 未完成 |
| **P5** | Phase 6 | 安全性（認證、授權） | 2-3 天 | ⬜ 未完成 |
| **P6** | Phase 3 | Agent 獨立進程啟動器 | 2-3 天 | ⬜ 未完成 |

---

## 13. 工作情境與流程模擬

