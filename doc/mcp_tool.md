# McpTool 適配器 — 設計規劃（合併版）

> **目標**：讓 ZL Agent 透過 [Model Context Protocol (MCP)](https://modelcontextprotocol.io) 連接外部工具服務，無縫整合為原生工具。Agent 可在運行時自主連接/斷開 MCP Server，並將配置持久化到 INI。

---

## 📊 實作進度總覽

| Phase | 狀態 | 完成度 |
|-------|------|--------|
| [Phase 1: 核心基礎](#phase-1--核心基礎2-3-天) | ⬜ 未開始 | 0/4 |
| [Phase 2: 整合與發現](#phase-2--整合與發現1-2-天) | ⬜ 未開始 | 0/5 |
| [Phase 3: Agent 自主管理 + SSE](#phase-3--agent-自主管理--sse-傳輸2-3-天) | ⬜ 未開始 | 0/4 |
| [Phase 4: 測試與完善](#phase-4--測試與完善1-2-天) | ⬜ 未開始 | 0/3 |
| [Phase 5: 高級功能（可選）](#phase-5--高級功能可選) | ⬜ 未開始 | 0/4 |

> **追蹤符號：** `[ ]` = 未開始，`[~]` = 進行中，`[x]` = 已完成。詳細追蹤請見 [§10 實現順序與里程碑](#10--實現順序與里程碑)

---

## 1. 為什麼需要？

| 場景 | 沒有 MCP Adapter | 有此適配器 |
|------|-----------------|-----------|
| 連接 GitHub API | 需手寫 REST client + auth | Server 已內建，一行配置即用 |
| 連接資料庫 | 需自行處理連線、SQL | Database MCP Server 提供結構化工具 |
| 使用第三方工具（Slack、Notion…） | 無對接能力 | 安裝對應 Server，自動發現並註冊 |

**與現有工具的差異：**

| 層面 | 現有工具系統 | MCP Adapter |
|------|------------|-------------|
| 來源 | 編譯時靜態連結 / DLL 動態載入 | 執行時動態發現（JSON-RPC） |
| Schema | C++ 程式碼硬編 JSON Schema | Server 透過 `tools/list` 回傳 |
| 生命週期 | 啟動時載入一次 | 可熱更新（Server 推送通知 + Agent 自主管理） |

---

## 2. 整體架構

```
┌───────────────────────────────────────────────────────┐
│                     zlagent Agent                      │
│                                                        │
│  ┌─────────────────────────────────────────────────┐   │
│  │                  ToolRegistry                    │   │
│  │                                                  │   │
│  │  Built-in Tools    Local Tools    Plugin Tools   │   │
│  │       │               │              │           │   │
│  │       ▼               ▼              ▼           │   │
│  │  ┌─────────┐   ┌──────────┐   ┌────────────┐   │   │
│  │  │ C++     │   │ Shell    │   │ DLL        │   │   │
│  │  │ Native  │   │ Exec     │   │ Dynamic    │   │   │
│  │  └─────────┘   └──────────┘   └────────────┘   │   │
│  │                                                  │   │
│  │  ┌──────────────────────────────────────────┐   │   │
│  │  │         MCP Tools (動態註冊/卸載)          │   │   │
│  │  │                                          │   │   │
│  │  │  mcp_github_search_issues                │   │   │
│  │  │  mcp_slack_send_message                  │   │   │
│  │  │  ... (tag: "mcp_<server_name>")          │   │   │
│  │  └──────────────┬───────────────────────────┘   │   │
│  └─────────────────┼───────────────────────────────┘   │
│                    │                                    │
│  ┌─────────────────▼───────────────────────────────┐   │
│  │              McpManager                          │   │
│  │                                                  │   │
│  │  connect_server() / disconnect_server()          │   │
│  │  list_servers()                                  │   │
│  │  add_config_to_ini() / remove_config_from_ini()  │   │
│  └─────────────────┬───────────────────────────────┘   │
│                    │                                    │
│  ┌─────────────────▼───────────────────────────────┐   │
│  │              McpClient (per server)               │   │
│  │                                                  │   │
│  │  handshake() → discover_tools() → call_tool()    │   │
│  └─────────────────┬───────────────────────────────┘   │
│                    │                                    │
│  ┌─────────────────▼───────────────────────────────┐   │
│  │           Transport Layer                        │   │
│  │                                                  │   │
│  │  StdioTransport (子進程)    SseTransport (HTTP)   │   │
│  └──────────────────────────────────────────────────┘   │
└───────────────────────────────────────────────────────┘

Agent 工具閉環:
  mcp_connect() → McpManager.connect_server() → ToolRegistry.register_batch()
  mcp_disconnect() → McpManager.disconnect_server() → ToolRegistry.unregister_by_tag()
  mcp_list_servers() → 返回當前連接狀態
  mcp_add_config() → IniParser.update_key() → 持久化到 zlagent.ini
  mcp_remove_config() → 從 INI 刪除配置段
```

### 2.1 核心設計原則

| 原則 | 說明 |
|------|------|
| **無縫集成** | MCP 工具對 Agent 來說與內建工具完全一致，通過同一個 `ToolRegistry` 管理 |
| **按需發現** | 連接 MCP Server 時動態獲取工具列表，不預先硬編碼 |
| **雙模式加載** | INI 配置啟動時自動連接 + 運行時 Agent 自主連接 |
| **Agent 自治** | Agent 可以自主新增/刪除 MCP Server 的持久化配置 |
| **容錯隔離** | MCP Server 崩潰不應影響 zlagent 主進程；超時、重連機制 |
| **輕量依賴** | 不引入重型 JSON RPC 庫，使用現有 `httplib.h` + nlohmann/json |

---

## 3. 組件設計

### 3.1 McpServerConfig — Server 配置

```cpp
// include/mcp_client.h

struct McpServerConfig {
    std::string name;                    // 唯一標識，如 "github"
    std::string transport_type;          // "stdio" (預設) 或 "sse"
    
    // stdio transport
    std::string command;                 // 可執行文件路徑
    std::vector<std::string> args;       // 命令行參數
    std::unordered_map<std::string, std::string> env;  // 環境變數（如 GITHUB_TOKEN=xxx）
    
    // SSE transport
    std::string url;                     // SSE endpoint URL
    
    // Common
    int connect_timeout_ms = 10000;      // 連接超時
    int request_timeout_ms = 30000;      // 請求超時
    bool enabled = true;                 // 是否啟用
};
```

### 3.2 McpClient — MCP 協議客戶端

負責與 MCP Server 建立連接、發送 JSON-RPC 請求。

```cpp
// include/mcp_client.h

class McpClient : public std::enable_shared_from_this<McpClient> {
public:
    explicit McpClient(const McpServerConfig& config);
    ~McpClient();

    // 初始化：建立連接、握手、發現工具
    bool initialize();
    
    // 關閉連接，釋放資源
    void shutdown();
    
    // 檢查是否處於可用狀態
    bool is_connected() const;
    
    // 獲取服務器名稱
    const std::string& server_name() const { return config_.name; }
    
    // 發現工具：返回 MCP Server 提供的所有工具描述
    std::vector<McpToolDescriptor> discover_tools();
    
    // 調用遠程工具
    nlohmann::json call_tool(const std::string& tool_name, 
                            const nlohmann::json& arguments);

private:
    McpServerConfig config_;
    
    // --- stdio transport ---
    class StdioTransport;
    std::unique_ptr<StdioTransport> stdio_transport_;
    
    // --- SSE transport ---
    class SseTransport;
    std::unique_ptr<SseTransport> sse_transport_;
    
    // 請求 ID 計數器
    int next_request_id_ = 1;
    
    // JSON-RPC 通信核心
    McpJsonRpcResponse send_request(const McpJsonRpcRequest& request);
    
    // 握手流程：initialize -> initialized notification
    bool handshake();
};
```

### 3.3 StdioTransport — stdio 傳輸層（含具體實現）

MCP 的 stdio transport 通過啟動子進程，使用 stdin/stdout 進行 JSON-RPC 通信。

**JSON 分行協議：** 每個 JSON-RPC message 佔一行（`\n` 結尾）。讀取時維護緩衝區，遇到換行觸發解析。

```cpp
// include/mcp_client.h (內部類)

class McpClient::StdioTransport {
public:
    bool start(const McpServerConfig& config);
    void stop();
    
    // 發送 JSON-RPC 請求，等待響應（同步）
    std::string send_and_receive(const std::string& json_request, 
                                 int timeout_ms = 30000);
    
    bool is_running() const;

private:
#ifdef _WIN32
    HANDLE process_ = nullptr;
    HANDLE stdin_pipe_ = nullptr;
    HANDLE stdout_pipe_ = nullptr;
#else
    pid_t child_pid_ = -1;
    int stdin_fd_ = -1;
    int stdout_fd_ = -1;
#endif
    
    std::string pending_input_;   // 緩衝未完成的 JSON 行

public:
    // ── Stdio transport 核心邏輯：JSON 分行解析 ──
    
    /**
     * 從子進程讀取一行完整的 JSON-RPC 消息。
     * 
     * 維護 pending_input_ 緩衝區，遇到 '\n' 觸發解析。
     * 支援大於 4KB 的 message（多次 read 拼接）。
     */
    std::string read_json_line(int timeout_ms) {
        auto deadline = std::chrono::steady_clock::now() 
                      + std::chrono::milliseconds(timeout_ms);
        
        while (true) {
#ifdef _WIN32
            char buf[4096];
            DWORD n = 0;
            if (!ReadFile(stdout_pipe_, buf, sizeof(buf), &n, nullptr)) break;
#else
            char buf[4096];
            ssize_t n = read_nonblocking(stdout_fd_, buf, sizeof(buf));
#endif
            if (n == 0) {
                if (std::chrono::steady_clock::now() >= deadline) return "";
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            
            pending_input_.append(buf, static_cast<size_t>(n));
            
            size_t pos = pending_input_.find('\n');
            if (pos != std::string::npos) {
                std::string line = pending_input_.substr(0, pos);
                pending_input_ = pending_input_.substr(pos + 1);
                return line;
            }
        }
        return "";
    }
    
    // 寫入數據到子進程
    bool write_data(const std::string& data) {
#ifdef _WIN32
        DWORD written = 0;
        return WriteFile(stdin_pipe_, data.c_str(), 
                        static_cast<DWORD>(data.size()), &written, nullptr);
#else
        ssize_t n = write(stdin_fd_, data.c_str(), data.size());
        return n == static_cast<ssize_t>(data.size());
#endif
    }
};
```

### 3.4 SseTransport — SSE 傳輸層（含具體實現）

MCP 的 SSE transport 通過 HTTP POST 發送請求，通過 SSE 接收響應。利用 `httplib.h` 內建的 `SSEClient::on_event()` 處理通知推送。

```cpp
// include/mcp_client.h (內部類)

class McpClient::SseTransport {
public:
    bool connect(const std::string& url, int timeout_ms);
    void disconnect();
    
    // 發送 JSON-RPC 請求，等待 SSE 響應
    std::string send_and_receive(const std::string& json_request, 
                                 int timeout_ms = 30000);
    
    bool is_connected() const;

private:
    std::string endpoint_url_;           // POST endpoint (from SSE event)
    std::string session_id_;             // MCP session ID
    
    // ── HTTP transport: JSON-RPC over HTTP POST ──
    
    /**
     * 發送 JSON-RPC 請求到 MCP Server。
     * 
     * httplib.h 優勢：內建 set_read_timeout()，不需手動管理逾時。
     */
    nlohmann::json send_request(int id, const std::string& method, 
                                const nlohmann::json& params) {
        httplib::Client cli(endpoint_url_);
        cli.set_read_timeout(timeout_ms / 1000, (timeout_ms % 1000) * 1000);
        
        nlohmann::json req = {{"jsonrpc", "2.0"}, {"id", id}, 
                              {"method", method}, {"params", params}};
        
        auto res = cli.Post("/mcp", req.dump(), "application/json");
        if (!res || res->status != 200) return nullptr;
        return nlohmann::json::parse(res->body);
    }
    
    // ── SSE Event Stream: 接收 tools/list_changed 等通知 ──
    
    /**
     * 啟動 SSE listener，處理 MCP Server 推送的通知。
     */
    void start_sse_listener() {
        httplib::SSEClient sse(*http_cli_, "/sse");
        sse.on_event("tools/list_changed", [this](const std::string& event, 
                                                  const std::string& data) {
            // 工具列表變更 → 觸發熱更新
            auto* mgr = get_global_mcp_manager();
            if (mgr) mgr->refresh_server(config_.name);
        });
        sse.start();
    }
    
    // 從 SSE stream 獲取消息端點
    std::string discover_endpoint(const std::string& url);
    
    // 等待特定 request_id 的響應（帶超時）
    std::string wait_for_response(int request_id, int timeout_ms);
};
```

### 3.5 McpTool — MCP 工具包裝器

將遠程 MCP 工具包裝為 zlagent 的 `Tool` 接口。

**設計要點：**
- `McpTool` 持有 `shared_ptr<McpClient>` 引用，多個工具共享同一個 Client（引用計數管理）
- `execute()` 直接調用 `client_->call_tool()`，無隱式全局依賴
- `mcp_<server>_<tool>` 命名避免不同 Server 同名衝突

```cpp
// include/mcp_tool.h

class McpTool : public Tool {
public:
    McpTool(const std::string& mcp_server_name,
            const McpToolDescriptor& descriptor,
            std::shared_ptr<McpClient> client);

    // Tool interface
    std::string name() const override;           // "mcp_github_search_issues"
    std::string description() const override;
    std::string parameters_schema() const override;  // MCP inputSchema 直接回傳
    std::string execute(const std::string& json_args) override;

private:
    std::string mcp_server_name_;       // 來源服務器名稱（用於錯誤報告）
    McpToolDescriptor descriptor_;      // 工具描述
    std::shared_ptr<McpClient> client_; // MCP Client 連接
    
    // 生成 zlagent 風格的工具名稱：mcp_<server>_<tool>
    std::string make_tool_name() const;
};

/**
 * 工廠函數：從 MCP Server 創建所有工具實例
 */
std::vector<ToolPtr> create_mcp_tools(const McpServerConfig& config,
                                      std::shared_ptr<McpClient> client);
```

### 3.6 ToolRegistry — 擴展動態註冊/卸載

在現有 `ToolRegistry` 上新增 tag 機制，支持批量操作：

```cpp
// include/tool.h (新增方法)

class ToolRegistry {
public:
    void register_tool(ToolPtr tool);              // 已有
    std::vector<ToolPtr> get_tools() const;        // 已有
    
    // ── 新增：動態管理 ──
    
    // 移除工具（按名稱）
    bool unregister_tool(const std::string& name);
    
    // 批量註冊，所有工具帶上 tag
    void register_batch(const std::vector<ToolPtr>& tools, 
                        const std::string& tag);
    
    // 按 tag 卸載所有工具（斷開 MCP Server 時使用）
    void unregister_by_tag(const std::string& tag);
    
    // 檢查某個 tag 是否已註冊
    bool has_tag(const std::string& tag) const;

private:
    struct ToolEntry {
        ToolPtr tool;
        std::string tag;   // 空字符串表示無 tag（內建工具）
    };
    std::unordered_map<std::string, ToolEntry> tools_;
};
```

### 3.7 McpManager — MCP 管理器（核心動態管理組件）

管理多個 MCP Server 的連接、工具註冊/卸載、INI 配置持久化。

**支持兩種模式：**
1. **啟動時加載** — INI 中的 MCP Server 在啟動時自動連接
2. **運行時動態操作** — Agent 自主連接/斷開服務器，並可持久化到 INI

```cpp
// include/mcp_manager.h

class McpManager {
public:
    explicit McpManager(ToolRegistry& registry, 
                        const std::string& ini_path = "zlagent.ini");
    
    // ── 啟動時初始化 ──
    
    // 從 INI 配置加載 MCP Server 列表並連接
    void initialize_from_ini();
    
    // ── 運行時動態操作（Agent 工具調用） ──
    
    /**
     * 連接新的 MCP Server。
     * 
     * 流程：建立連接 → 握手 → 發現工具 → 註冊到 ToolRegistry
     * tag = "mcp_<server_name>"，方便後續卸載
     */
    std::string connect_server(const McpServerConfig& config);
    
    /**
     * 斷開 MCP Server。
     * 
     * 流程：從 ToolRegistry 卸載工具 → 關閉連接
     */
    std::string disconnect_server(const std::string& server_name);
    
    /**
     * 列出當前所有 MCP Server 的狀態。
     */
    std::vector<McpServerStatus> list_servers();
    
    // ── INI 配置持久化（Agent 工具調用） ──
    
    /**
     * 將 MCP Server 配置寫入 zlagent.ini。
     * 
     * Agent 可以自主決定哪些服務器需要持久化，
     * 這樣下次啟動時會自動連接。
     */
    std::string add_config(const McpServerConfig& config);
    
    /**
     * 從 zlagent.ini 刪除 MCP Server 配置段。
     */
    std::string remove_config(const std::string& server_name);

private:
    ToolRegistry& registry_;              // 引用，用於註冊/卸載工具
    std::string ini_path_;                // INI 文件路徑
    
    struct McpServerEntry {
        McpServerConfig config;
        std::shared_ptr<McpClient> client;
        bool connected = false;
    };
    
    std::unordered_map<std::string, McpServerEntry> servers_;
    
    // INI section 名稱規則：mcp_server_<name>
    static std::string make_ini_section(const std::string& server_name);
};

// 全局訪問（供 Agent 工具調用）
McpManager* get_global_mcp_manager();
void set_global_mcp_manager(McpManager* mgr);
```

### 3.8 Agent 工具 — MCP 管理工具

Agent 可以調用的工具，用於自主管理 MCP Server。

```cpp
// include/mcp_tool.h (新增)

/**
 * mcp_connect — 連接一個 MCP Server
 */
class McpConnectTool : public Tool { /* ... */ };

/**
 * mcp_disconnect — 斷開一個 MCP Server
 */
class McpDisconnectTool : public Tool { /* ... */ };

/**
 * mcp_list_servers — 列出當前連接的 MCP Server
 */
class McpListServersTool : public Tool { /* ... */ };

/**
 * mcp_add_config — 將 MCP Server 配置持久化到 INI
 */
class McpAddConfigTool : public Tool { /* ... */ };

/**
 * mcp_remove_config — 從 INI 刪除 MCP Server 配置
 */
class McpRemoveConfigTool : public Tool { /* ... */ };

// Factory functions
ToolPtr create_mcp_connect_tool();
ToolPtr create_mcp_disconnect_tool();
ToolPtr create_mcp_list_servers_tool();
ToolPtr create_mcp_add_config_tool();
ToolPtr create_mcp_remove_config_tool();
```

---

## 4. MCP 協議實現細節

### 4.1 JSON-RPC 2.0 消息格式

MCP 基於 JSON-RPC 2.0，所有通信都是 JSON 對象：

**請求：**
```json
{"jsonrpc": "2.0", "id": 1, "method": "tools/list", "params": {}}
```

**響應：**
```json
{
    "jsonrpc": "2.0", "id": 1,
    "result": {
        "tools": [{
            "name": "search_issues",
            "description": "Search GitHub issues...",
            "inputSchema": {"type": "object", "properties": {...}}
        }]
    }
}
```

**錯誤響應：**
```json
{
    "jsonrpc": "2.0", "id": 1,
    "error": {
        "code": -32601,
        "message": "Method not found"
    }
}
```

### 4.2 MCP 核心方法

| 方法 | 方向 | 說明 |
|------|------|------|
| `initialize` | Client → Server | 握手，交換協議版本和能力 |
| `notifications/initialized` | Client → Server | 通知服務器客戶端已準備就緒 |
| `tools/list` | Client → Server | 獲取所有可用工具列表 |
| `tools/call` | Client → Server | 調用指定工具 |
| `resources/list` | Client → Server | 獲取資源列表（可選） |
| `prompts/list` | Client → Server | 獲取提示模板列表（可選） |

### 4.3 初始化流程

```
Client                          MCP Server
  │                                │
  │──── initialize {…} ──────────►│
  │◄── {protocolVersion, …} ─────│
  │── initialized/notify ────────►│
  │──── tools/list ──────────────►│
  │◄── {tools: [...]} ───────────│
```

### 4.4 工具呼叫流程

```
Client                          MCP Server
  │──── tools/call {            ►│
  │       name: "search_issues", │
  │       arguments: {"query": …}│
  │─────────────────────────────►│
  │◄── { content: [{             │
  │       type: "text",           │
  │       text: "結果…" }]        │
  │     } ──────────────────────│
```

---

## 5. 配置設計

### 5.1 INI 配置擴展

在 `zlagent.ini` 中添加 MCP Server 配置。每個 Server 使用獨立 section，方便 Agent 自主操作：

```ini
[mcp]
enabled = true

# Server 1: stdio transport (e.g., Python-based MCP server)
[mcp_server_github]
name = GitHub MCP
transport_type = stdio
command = python3
args = -m mcp_github.server
env_GITHUB_TOKEN = ghp_xxxxxxxxxxxx
connect_timeout_ms = 10000
request_timeout_ms = 30000
enabled = true

# Server 2: SSE transport (e.g., remote HTTP-based MCP server)
[mcp_server_slack]
name = Slack MCP
transport_type = sse
url = http://localhost:8765/sse
connect_timeout_ms = 10000
request_timeout_ms = 30000
enabled = true

# Server 3: disabled example
[mcp_server_database]
name = Database MCP
transport_type = stdio
command = npx
args = -y @modelcontextprotocol/server-postgres postgresql://localhost/mydb
enabled = false
```

### 5.2 Config 結構擴展

在 `Config` 中添加：

```cpp
// ── MCP (Model Context Protocol) ────────────────
struct Mcp {
    bool enabled = true;                              // 全局開關
    
    struct Server {
        std::string name;                             // 顯示名稱
        std::string transport_type = "stdio";         // "stdio" or "sse"
        std::string command;                          // stdio: executable path
        std::vector<std::string> args;                // stdio: arguments
        std::unordered_map<std::string, std::string> env;  // 環境變數
        std::string url;                              // sse: endpoint URL
        int connect_timeout_ms = 10000;
        int request_timeout_ms = 30000;
        bool enabled = true;                          // 單個服務器開關
    };
    
    std::vector<Server> servers;                      // MCP Server 列表
} mcp;
```

### 5.3 INI Section 命名規則

MCP Server 的 INI section 使用固定前綴：`mcp_server_<name>`

| Agent 操作 | INI Section | 說明 |
|-----------|-------------|------|
| `mcp_add_config(github, ...)` | `[mcp_server_github]` | 新增/更新配置段 |
| `mcp_remove_config(github)` | 刪除 `[mcp_server_github]` | 移除整個 section |
| 啟動時掃描 | 所有 `[mcp_server_*]` | 自動加載 |

---

## 6. Agent 自主管理 MCP Server — 完整閉環

### 6.1 場景一：Agent 發現需要新工具 → 連接 + 持久化

```
用戶: "幫我查一下 GitHub 上這個專案的 issue"

Agent 思考:
  - 我沒有 GitHub 相關的工具
  - 我可以連接一個 MCP Server 來獲取這些能力
  
Agent 調用 mcp_connect():
  {
    "name": "github",
    "transport_type": "stdio",
    "command": "python3",
    "args": ["-m", "mcp_github.server"],
    "env": {"GITHUB_TOKEN": "ghp_xxx"},
    "connect_timeout_ms": 10000,
    "request_timeout_ms": 30000
  }

McpManager.connect_server():
  → 建立 stdio 連接，啟動 python3 -m mcp_github.server
  → handshake() 成功
  → discover_tools() → [search_issues, get_repo_info, create_issue]
  → ToolRegistry.register_batch([mcp_github_search_issues, ...], "mcp_github")

Agent 現在可以使用:
  mcp_github_search_issues(query="zlagent bug")
  
Agent 決定持久化這個配置（下次啟動自動連接）:
  Agent 調用 mcp_add_config():
    {
      "name": "github",
      "transport_type": "stdio",
      "command": "python3",
      "args": ["-m", "mcp_github.server"],
      ...
    }

McpManager.add_config():
  → IniParser.update_key("zlagent.ini", "mcp_server_github", ...)
```

### 6.2 場景二：Agent 用完後清理 → 斷開 + 移除配置

```
用戶: "不用查 GitHub 了，把那個服務關掉吧"

Agent 調用 mcp_disconnect():
  { "name": "github" }

McpManager.disconnect_server("github"):
  → ToolRegistry.unregister_by_tag("mcp_github")
    → 移除 mcp_github_search_issues, mcp_github_get_repo_info, ...
  → McpClient.shutdown()
    → stdio_transport.stop() (關閉子進程)

Agent 調用 mcp_remove_config():
  { "name": "github" }

McpManager.remove_config("github"):
  → 從 zlagent.ini 刪除 [mcp_server_github] section
```

### 6.3 場景三：啟動時自動連接

```
zlagent 啟動:
  main() → McpManager.initialize_from_ini():
    → 掃描 INI 中所有 [mcp_server_*] sections
    → 對每個 enabled=true 的服務器調用 connect_server()
    → 工具自動註冊到 ToolRegistry
  
Agent 從一開始就擁有這些 MCP 工具，無需手動連接。
```

### 6.4 場景四：Agent 探索可用服務器

```
用戶: "你現在連了哪些 MCP Server？"

Agent 調用 mcp_list_servers():
  → McpManager.list_servers()
  
返回:
  [
    { name: "github", connected: true, tool_count: 3 },
    { name: "slack", connected: false, error_message: "Connection refused" }
  ]
```

---

## 7. 集成到 zlagent 主流程

### 7.1 main.cpp 中的修改點

在 `main()` 中，工具註冊之後、RAG 初始化之前添加 MCP 初始化：

```cpp
// === Register built-in tools === (現有代碼)
ag.add_tool(agent::create_read_file_tool());
// ... 其他內建工具

// === MCP Integration === (新增)
if (cfg.mcp.enabled) {
    LOG_INFO("Main", "\nInitializing MCP integration...");
    
    // McpManager 持有 ToolRegistry 引用，用於動態註冊/卸載
    agent::McpManager mcp_manager(ag.get_tool_registry(), ini_path);
    
    // 啟動時加載 INI 中的服務器
    mcp_manager.initialize_from_ini();
    
    // 將 McpManager 設置為全局可訪問（供 Agent 工具調用）
    set_global_mcp_manager(&mcp_manager);
    
    // 註冊 MCP 管理工具 — Agent 可以自主連接/斷開服務器
    ag.add_tool(agent::create_mcp_connect_tool());
    ag.add_tool(agent::create_mcp_disconnect_tool());
    ag.add_tool(agent::create_mcp_list_servers_tool());
    ag.add_tool(agent::create_mcp_add_config_tool());
    ag.add_tool(agent::create_mcp_remove_config_tool());
    
    LOG_INFO("MCP", std::to_string(mcp_manager.list_servers().size()) 
             + " MCP server(s) configured.");
}

// === Skill System === (現有代碼)
agent::SkillRegistry skill_registry;
```

### 7.2 工具命名規則

為了避免與內建工具衝突，MCP 工具使用 `mcp_` 前綴：

| MCP Server | MCP Tool Name | zlagent Tool Name |
|------------|---------------|-------------------|
| github | search_issues | `mcp_github_search_issues` |
| github | get_repo_info | `mcp_github_get_repo_info` |
| slack | send_message | `mcp_slack_send_message` |

命名規則：`mcp_<server_name>_<tool_name>`（全部小寫，空格轉下劃線）

---

## 8. 錯誤處理與容錯

### 8.1 連接失敗處理

```
MCP Server 連接失敗 → LOG_WARN + 跳過該服務器 → zlagent 繼續運行
```

不因為 MCP Server 不可用而阻止 zlagent 啟動。

### 8.2 工具調用超時

- 每次 `tools/call` 請求都有獨立的超時（默認 30s）
- httplib.h `set_read_timeout()` 自動處理 HTTP 逾時
- 超時返回錯誤結果給 LLM，讓 Agent 決定是否重試
- 錯誤格式：`[MCP Error] Tool 'xxx' timed out after 30000ms on server 'yyy'`

### 8.3 服務器崩潰恢復

```cpp
// McpTool::execute() 偽代碼
std::string McpTool::execute(const std::string& json_args) {
    try {
        if (!client_->is_connected()) {
            LOG_WARN("McpTool", "Reconnecting to MCP server: " + mcp_server_name_);
            if (!client_->initialize()) {
                return "[MCP Error] Cannot connect to server '" + mcp_server_name_ + "'";
            }
        }
        
        auto result = client_->call_tool(descriptor_.name, json::parse(json_args));
        return format_mcp_result(result);
    } catch (const std::exception& e) {
        return "[MCP Error] " + std::string(e.what());
    }
}
```

### 8.4 stdio 進程管理

- stdio MCP Server 作為子進程運行，zlagent 退出時自動清理
- 檢測子進程意外退出，嘗試重啟（最多 3 次）
- 防止僵尸進程：定期檢查進程狀態
- Windows pipe 阻塞問題：使用非阻塞 I/O

---

## 9. 安全考慮

### 9.1 工具調用隔離

MCP 工具的執行結果不經過 shell，直接通過 JSON-RPC 傳遞，避免了命令注入風險。

### 9.2 stdio 命令白名單

防止惡意配置啟動危險命令：

```cpp
bool is_safe_stdio_command(const std::string& command) {
    // 只允許已知的安全可執行文件
    static const std::set<std::string> allowed = {"python3", "node", "npx", "java"};
    auto basename = std::filesystem::path(command).filename().string();
    return allowed.count(basename) > 0 || !std::filesystem::path(command).has_parent_path();
}
```

### 9.3 Agent 自主連接的安全限制

當 Agent 調用 `mcp_connect()` 時，需要驗證：

| 檢查項 | 規則 |
|-------|------|
| command 白名單 | stdio transport 的 command 必須在白名單中 |
| args 過濾 | 禁止包含 shell metacharacters（`;`, `&`, `\|`） |
| URL 協議限制 | SSE transport 只允許 `http://localhost:*` 或配置的域名 |
| 連接數量上限 | 同時連接的 MCP Server 不超過配置的上限（默認 10） |

### 9.4 INI 寫入安全

Agent 調用 `mcp_add_config()` 時：

```cpp
std::string McpManager::add_config(const McpServerConfig& config) {
    // 驗證命令安全性
    if (config.transport_type == "stdio" && !is_safe_stdio_command(config.command)) {
        return "[MCP Error] Command '" + config.command + "' is not allowed";
    }
    
    // 寫入 INI
    auto section = make_ini_section(config.name);
    IniParser::update_key(ini_path_, section, "name", config.name);
    IniParser::update_key(ini_path_, section, "transport_type", config.transport_type);
    // ... 其他字段
    
    return "[MCP Config] Added server '" + config.name + "' to INI";
}
```

### 9.5 輸出截斷

防止 MCP Server 返回過大結果：

```cpp
static constexpr size_t MAX_MCP_RESULT_SIZE = 1024 * 1024; // 1MB

std::string format_mcp_result(const nlohmann::json& result) {
    std::string output = result.dump();
    if (output.size() > MAX_MCP_RESULT_SIZE) {
        return output.substr(0, MAX_MCP_RESULT_SIZE) + 
               "\n\n[MCP Warning] Output truncated at 1MB";
    }
    return output;
}
```

---

## 10. 實現順序與里程碑

> **追蹤說明：** `[ ]` = 未開始，`[~]` = 進行中，`[x]` = 已完成。每項任務完成後勾選並可填寫備註。

### Phase 1: 核心基礎（2-3 天）

| # | 狀態 | 項目 | 檔案 | 說明 |
|---|------|------|------|------|
| P1.1 | [ ] | `McpServerConfig` + INI 解析 | `include/mcp_client.h`, `src/config.cpp` | 配置結構體，含 env map |
| P1.2 | [ ] | JSON-RPC 2.0 封裝 | `src/mcp_jsonrpc.cpp` | request/response/error 格式 |
| P1.3 | [ ] | Stdio Transport | `src/mcp_stdio_transport.cpp` | 子進程管理 + stdin/stdout I/O + JSON 分行解析 |
| P1.4 | [ ] | `McpTool` 類別 | `include/mcp_tool.h`, `src/mcp_tool.cpp` | Tool 介面實作 |

**Phase 1 備註：**

---

### Phase 2: 整合與發現（1-2 天）

| # | 狀態 | 項目 | 檔案 | 說明 |
|---|------|------|------|------|
| P2.1 | [ ] | ToolRegistry tag 擴展 | `include/tool.h`, `src/agent.cpp` | register_batch / unregister_by_tag |
| P2.2 | [ ] | `McpClient` connect/list/call | `src/mcp_client.cpp` | initialize + tools/list + tools/call |
| P2.3 | [ ] | `McpManager` | `src/mcp_manager.cpp` | Server 管理 + ToolRegistry 整合 |
| P2.4 | [ ] | Agent 整合 | `src/main.cpp` | MCP 初始化 + 管理工具註冊 |
| P2.5 | [ ] | CMakeLists.txt | `CMakeLists.txt` | 新增來源檔案 |

**Phase 2 備註：**

---

### Phase 3: Agent 自主管理 + SSE 傳輸（2-3 天）

| # | 狀態 | 項目 | 檔案 | 說明 |
|---|------|------|------|------|
| P3.1 | [ ] | HTTP/SSE Transport | `src/mcp_sse_transport.cpp` | httplib.h POST + SSE event stream |
| P3.2 | [ ] | 通知處理 | `src/mcp_client.cpp` | `tools/list_changed` 熱更新 |
| P3.3 | [ ] | Agent 管理工具 | `src/mcp_agent_tools.cpp` | mcp_connect / disconnect / list / add_config / remove_config |
| P3.4 | [ ] | INI 持久化 | `src/mcp_manager.cpp` | add_config / remove_config |

**Phase 3 備註：**

---

### Phase 4: 測試與完善（1-2 天）

| # | 狀態 | 項目 | 說明 |
|---|------|------|------|
| P4.1 | [ ] | Mock MCP Server + 單元測試 | stdio transport 的 mock server |
| P4.2 | [ ] | 集成測試 | 完整閉環：連接→使用→斷開→持久化→重啟恢復 |
| P4.3 | [ ] | 安全檢查、邊界條件處理 | 白名單驗證、輸出截斷、超時測試 |

**Phase 4 備註：**

---

### Phase 5: 高級功能（可選）

| # | 狀態 | 任務 | 說明 |
|---|------|------|------|
| P5.1 | [ ] | MCP Resources 支持 | 讓 Agent 可以讀取 MCP Server 提供的資源 |
| P5.2 | [ ] | MCP Prompts 支持 | 讓 Agent 可以使用 MCP Server 提供的提示模板 |
| P5.3 | [ ] | MCP Sameloop 模式 | stdio transport 的雙向流式通信 |
| P5.4 | [ ] | MCP 工具緩存 | 避免每次對話都重新發現工具 |

**Phase 5 備註：**

---

### 整體進度摘要

| Phase | 任務數 | 已完成 | 進行中 | 未開始 | 完成度 |
|-------|--------|--------|--------|--------|--------|
| Phase 1 | 4 | 0 | 0 | 4 | 0% |
| Phase 2 | 5 | 0 | 0 | 5 | 0% |
| Phase 3 | 4 | 0 | 0 | 4 | 0% |
| Phase 4 | 3 | 0 | 0 | 3 | 0% |
| Phase 5 | 4 | 0 | 0 | 4 | 0% |
| **總計** | **20** | **0** | **0** | **20** | **0%** |

---

## 11. 文件結構總覽

```
zlagent/
├── include/
│   ├── mcp_client.h          # McpClient, McpServerConfig, JSON-RPC structures
│   ├── mcp_tool.h            # McpTool wrapper + Agent tools (mcp_connect etc.)
│   └── mcp_manager.h         # McpManager for multi-server management
├── src/
│   ├── mcp_jsonrpc.cpp       # JSON-RPC message encoding/decoding
│   ├── mcp_client.cpp        # McpClient core logic (handshake, tool discovery)
│   ├── mcp_stdio_transport.cpp  # stdio transport implementation + JSON line parsing
│   ├── mcp_sse_transport.cpp    # SSE transport implementation (httplib.h)
│   ├── mcp_tool.cpp          # McpTool wrapper + factory function
│   ├── mcp_agent_tools.cpp   # Agent tools: mcp_connect, mcp_disconnect etc.
│   └── mcp_manager.cpp       # McpManager implementation
├── zlagent.ini               # Extended with [mcp] and [mcp_server_*] sections
├── CMakeLists.txt            # Added new source files
└── doc/
    └── mcp_tool.md           # This document
```

---

## 12. 與現有系統的交互

### 12.1 ToolRegistry

MCP 工具通過 tag 註冊，斷開時按 tag 卸載：

```cpp
// McpManager::connect_server()
auto tools = create_mcp_tools(config, client);
registry_.register_batch(tools, "mcp_" + config.name);

// McpManager::disconnect_server("github")
registry_.unregister_by_tag("mcp_github");  // 一鍵卸載所有 github MCP 工具
```

### 12.2 SafetyGuard

MCP 工具的執行不經過文件系統操作，所以不需要通過 `SafetyGuard`。但 MCP Server 的 stdio 命令路徑需要驗證（見 §9.2）。

### 12.3 Skill System

MCP 工具可以被 Skill 引用，在 `SKILL.md` 的 `tools_required` 中聲明：

```yaml
# SKILL.md
name: github_issue_triage
tools_required: [mcp_github_search_issues, mcp_github_get_repo_info]
```

### 12.4 Task Planner / Self Reflector

不需要修改。MCP 工具對它們來說是透明的，因為它們通過 `ToolRegistry` 訪問工具。

---

## 13. 測試策略

### 13.1 Mock MCP Server

實現一個簡單的本地 MCP Server 用於測試：

```python
# tests/mcp/mock_server.py
import json, sys

def handle_request(request):
    method = request.get("method", "")
    
    if method == "initialize":
        return {"protocolVersion": "2024-11-05", "capabilities": {}}
    elif method == "tools/list":
        return {"tools": [{"name": "echo", "description": "Echo input", 
                "inputSchema": {"type": "object", "properties": {"message": {"type": "string"}}}}]}
    elif method == "tools/call":
        args = request.get("params", {}).get("arguments", {})
        return {"content": [{"text": f"Echo: {args.get('message', '')}"}]}
    
    return {}

# stdio transport loop
while True:
    line = sys.stdin.readline().strip()
    if not line: break
    request = json.loads(line)
    response = handle_request(request)
    result = {"jsonrpc": "2.0", "id": request.get("id"), "result": response}
    print(json.dumps(result), flush=True)
```

### 13.2 測試用例

| # | 測試場景 | 預期結果 |
|---|---------|---------|
| 1 | stdio transport 連接 + 握手 | 成功建立連接，協議版本匹配 |
| 2 | SSE transport 連接 + 握手 | 成功獲取 endpoint，完成握手 |
| 3 | tools/list 發現工具 | 返回正確的工具列表 |
| 4 | tools/call 調用工具 | 返回正確的執行結果 |
| 5 | 超時處理 | 返回錯誤信息，不崩潰 |
| 6 | MCP Server 崩潰恢復 | 自動重連或返回錯誤 |
| 7 | 多服務器並行連接 | 所有服務器正常運行 |
| 8 | **Agent 自主連接** | mcp_connect() → 工具出現，mcp_disconnect() → 工具消失 |
| 9 | **INI 持久化** | mcp_add_config() → INI 更新，重啟後自動連接 |
| 10 | **INI 刪除** | mcp_remove_config() → section 被移除 |
| 11 | 安全檢查（非法 command） | mcp_connect() 拒絕不安全命令 |
| 12 | 大輸出截斷 | 超過 1MB 時正確截斷 |

---

## 14. 風險與注意事項

| 風險 | 緩解措施 |
|------|---------|
| MCP 協議版本變更 | initialize 回應中檢查 `protocolVersion`，不相容時拒絕連接 |
| stdio JSON 分行解析錯誤 | 健壯的緩衝區管理（pending_input_），支援大於 4KB message |
| Windows pipe 阻塞 | 使用非阻塞 I/O 或 async task |
| Server 返回非標準 JSON Schema | 對 `inputSchema` 做基本驗證 |
| 多個 Server 同時初始化耗時 | 並行初始化（各 Server 獨立線程） |

---

## 15. 參考資源

- [MCP 官方規範](https://modelcontextprotocol.io/specification/2024-11-05/basic/)
- [MCP TypeScript SDK](https://github.com/modelcontextprotocol/typescript-sdk) — 參考實作
- [cpp-httplib](https://github.com/yhirose/cpp-httplib) — HTTP/SSE transport（專案已內建）
