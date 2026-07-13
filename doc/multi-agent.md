# Multi-Agent 架構設計

## 關聯
- agent.h
- multi_agent.h

## 核心概念

多智能體（Multi-Agent）協調系統：將不同類型的任務分配給專門的子智能體來處理，避免單一 LLM 過度負擔。

每個 sub-agent 註冊為一個 Tool，由主 Agent 的 LLM 透過 function calling 決定路由——與現有 ToolRegistry 機制完全一致。

## 架構分層

### 1. SubAgent — 基底類別

每個子智能體的基礎介面。

- **name_**：專屬名稱（同時作為 tool name）
- **description_**：描述文字（同時作為 tool description，供 LLM 判斷路由）
- **execute(task)**：統一的執行介面，外部呼叫者不需知道是哪種 agent
- **run_loop(task)**：虛擬函數，預設回傳空 `ChatResponse`，允許子智能體進行最多 5 次的 mini reasoning loop

### 2. SubAgentLLM — LLM 推理型任務

- 設定項

```
[llm_agent]
enable=true
workdir = path1/ path2/ # 可以設定多組工作目錄
```
- 每個工作目錄建立一個 SubAgentLLM 實例，註冊為 Tool 加入 ToolRegistry

一個完整 agent，可獨立進行多輪思考。

- **run_loop(task) override**：實作獨立的推理循環
- 使用工作目錄下的設定檔
- 對工作目錄建立專案 overview，作為 tool description 供 LLM 判斷路由

### 3. SubAgentNet — WebSocket Client（類似 Telegram Bot）

- 設定項

```
[net_agent]
enable=true
url = ws://127.0.0.1:8765/ws
```

- 設定項 enable=true && url 時候啟用
- 啟用時
  - 建立實例 Agent::sub_agent_
  - 使用 httplib WebSocket API 開始連線流程

類似 Telegram Bot 的事件驅動模式：接收遠端指令 → 處理 → 回報結果。

### 4. SubAgentCLI — 命令列工具任務（待實作）

執行 shell command、檔案操作等命令列操作。

### 5. MultiAgent — 協調者（Coordinator / Server）

- 設定項

```
[multi_agent]
enable=true
listen_port = 8766
```

- 設定項 enable=true && listen_port > 0 時候啟用
- 啟用時
  - 建立實例 Agent::multi_agent_
  - 建立實例 MultiAgent::server_ 並且開始監聽 WebSocket

持有所有子智能體的列表，負責管理 sub-agent 生命週期。

- **server_**：httplib Server，用於 WebSocket 連線共用（keep-alive）
- **agents_**：`vector<shared_ptr<SubAgent>>`，管理所有 sub-agent
- **register_agent(agent)**：註冊 sub-agent 並自動包裝為 Tool 加入 ToolRegistry

## 整體流程

```
使用者任務 → Agent reasoning_loop
                ↓
          LLM 看到所有 tool definitions（包含 sub-agents）
                ↓
          LLM 決定呼叫哪個 agent (function call)
                ↓
     SubAgentLLM::execute(task) — 獨立推理循環
```

## 設計原則

1. **職責分離**：不同類型的任務由專門的 agent 處理
2. **LLM 路由**：sub-agent 註冊為 Tool，由 LLM 透過 function calling 決定路由，無需手動 match_task
3. **獨立推理循環**：每個 SubAgentLLM 有自己的 agent，可以獨立進行多輪思考
4. **可擴展性**：未來可輕易新增其他類型的 SubAgent（例如圖形處理、資料庫查詢等）
5. **懶加載優化**：HTTP Server/Client 使用 `mutable unique_ptr` + 懶加載模式，只在第一次使用時初始化，避免不必要的開銷

## 架構確認

### Agent 成員關係

```cpp
// Agent 持有兩個獨立角色：
std::shared_ptr<MultiAgent> multi_agent_;   // Server — 管理本地 sub-agents，註冊為 Tools
std::shared_ptr<SubAgentNet> sub_agent_;    // Client — 類似 Telegram Bot，接收遠端指令並回報
```

- `multi_agent_`：Server mode，管理本地 SubAgentLLM 等 sub-agent，每個 sub-agent 註冊為 Tool 加入 ToolRegistry，由 LLM 透過 function calling 路由
- `sub_agent_`：Client mode，類似 Telegram Bot 的事件驅動模式，接收遠端 WebSocket 指令 → 處理 → 回報結果，不走 MultiAgent 的路由

### SubAgentNet — WebSocket Client

使用 httplib 的 WebSocket API（新版支援），非 HTTP。事件驅動模式與 Telegram Bot 一致：
- 建立 `SubAgentNet::client_`（httplib WebSocket client）
- 透過 `on_event("websocket.incoming", ...)` 接收訊息
- 處理後透過 WebSocket 回報結果

### MultiAgent — Server

- `[multi_agent]` 設定項只有 `enable` + `listen_port`
- 使用 httplib Server 監聽 WebSocket 連線
- 路由由 LLM 透過 Tool function calling 處理

## 實作計畫

### Phase 1: 基礎結構

| # | 項目 | 內容 |
|---|------|------|
| 1 | SubAgent 加入 description | 建構子接受 `name` + `description` |
| 2 | SubAgent → Tool 包裝器 | 建立 wrapper class，讓 SubAgent 可註冊到 ToolRegistry（提供 `name()`、`description()`、`parameters_schema()`、`execute(json_args)`） |
| 3 | MultiAgent::register_agent() | 公開方法，加入 sub-agent 並自動註冊為 tool |

### Phase 2: SubAgentLLM 實作

| # | 項目 | 內容 |
|---|------|------|
| 4 | set_workdir() 實作 | 建立內部 `agent_`、讀取該目錄下的 zlagent.ini、產生 project overview 作為 description |
| 5 | run_loop() 迭代邏輯 | 最多 5 次的 mini reasoning loop |

### Phase 3: Config + Agent 整合

| # | 項目 | 內容 |
|---|------|------|
| 6 | Config 新增設定結構 | `[llm_agent]`、`[net_agent]`、`[multi_agent]` |
| 7 | Agent::load_config() 整合 | 根據 config 建立 MultiAgent、SubAgentLLM 實例，並註冊為 tools |

### Phase 4: SubAgentNet（WebSocket client）

| # | 項目 | 內容 |
|---|------|------|
| 8 | SubAgentNet WebSocket 連線 | 使用 httplib 的 WebSocket API，類似 Telegram Bot 的事件驅動模式 |
