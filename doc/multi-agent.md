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
- **run_loop(task)**：虛擬函數，預設回傳空 `ChatResponse`

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

### 4. SubAgentCLI — 命令列工具任務

執行 shell command、檔案操作等命令列操作。

```
cd workdir1/
zlAgent -p task
```

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

zlAgent [multi_agent] Server
  - Need Enable and listen_port
  - Wait for client connect
  - When client connected regist name, description as tool

zlAgent [net_agent] Client
  - Need Enable and url
  - Create name and description
  - Connect to server

```
使用者任務 → Agent reasoning_loop [multi_agent]
                ↓
            LLM 看到所有 tool definitions（包含 sub-agents）
                ↓
            LLM 決定呼叫哪個 agent (function call) [sub_agent]
                ↓
            SubAgentLLM::execute(task) — 獨立推理循環
                ↓
                Response (async)
```

1. Client 啟動，透過 WebSocket 連接到 Server
2. Client 向 Server 註冊自己（name + description），Server 將其加入 ToolRegistry
3. Server 的 LLM 看到所有 tool definitions（包含遠端 client agent）
4. 當 LLM 決定呼叫遠端 client agent 時，Server 透過 WebSocket 把任務發給 Client
5. Client 處理後回傳結果

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
- 可註冊 sub_agent: SubAgentLLM, SubAgentNet, SubAgentCLI

## 使用者確認流程

### 現有互動點總覽

系統在執行過程中會在以下情境要求使用者回覆。分為兩種輸入方式：

**`read_key()` — 單鍵確認（y/N）**

| # | 位置 | 情境 | Prompt |
|---|------|------|--------|
| 1 | `safety_guard.cpp:56` | **危險操作確認** — 偵測到 `rm -rf`、`del /f` 等破壞性指令 | `Type 'y' to confirm, anything else to cancel:` |
| 2 | `safety_guard.cpp:139` | **路徑白名單外確認** — 寫入路徑超出 working directory / whitelist（非 strict mode） | `Type 'y' to confirm, anything else to cancel:` |
| 3 | `terminal_command_detector.cpp:260` | **終端指令低信心確認** — 偵測到可能的 shell 命令但信心不足 | `Execute directly? [y/N]:` |
| 4 | `user_reply.cpp:89` | **Tool 需要使用者回覆** — Tool 執行失敗或設定為需確認模式（exec/edit/always） | `Reply:` → y=重試, n=跳過 |

**`readline()` — 完整行輸入**

| # | 位置 | 情境 | Prompt |
|---|------|------|--------|
| 5 | `main.cpp:295` | **主迴圈** — 等待使用者輸入指令 | `You:[model]>` |
| 6 | `command_handlers.cpp:220` | **切換模型** — 選擇 LLM 編號 | `Select Model>` |
| 7 | `file_tool.cpp:1088` | **EditFilesTool 替換衝突** — `old_text` 出現多次，詢問跳過/替換單一/全部 | `Choose: [N] skip / [num] replace that one / [A] all:` |

### Multi-Agent 下的使用者確認問題

在 Multi-Agent 架構中，sub-agent 執行時可能需要使用者確認（例如危險操作、路徑檢查）。這產生以下問題：

```
主 Agent (Server) → SubAgentLLM (本地)     → 可直接使用 KeyWatcher
主 Agent (Server) → SubAgentNet (遠端)    → 無法直接存取 Server 的終端
SubAgentNet (Client) → 獨立執行           → 有自己的終端，可獨立確認
```

#### 情境分析

| 情境 | sub-agent 類型 | 使用者確認方式 |
|------|---------------|----------------|
| SubAgentLLM 在本機執行危險操作 | SubAgentLLM（本地） | 共用 Server 的 KeyWatcher，直接詢問終端 |
| SubAgentNet 接收遠端指令需要確認 | SubAgentNet（Client） | Client 有自己的終端，獨立處理確認 |
| Server 的 LLM 呼叫 sub-agent tool，tool 內部觸發安全檢查 | SubAgentLLM / SubAgentCLI | 共用 Server 的 KeyWatcher |

#### 設計決策

1. **SubAgentLLM（本地）**：與主 Agent 共享同一個終端和 `KeyWatcher`。使用者確認直接在當前終端進行，無需額外機制。

2. **SubAgentNet（遠端 Client）**：擁有獨立的終端和 `KeyWatcher`。使用者確認在 Client 自己的終端上處理，不影響 Server。

3. **主 Agent readline 阻塞問題**：當主 Agent 正在等待使用者輸入（`readline()`）時，sub-agent 的確認提示會與主迴圈的 prompt 衝突。**解決方式**：
   - sub-agent 透過 `execute(task)` 同步執行，不會在 `readline()` 期間觸發
   - 若未來需要非同步 sub-agent，需實作「確認訊息佇列」機制，將確認請求排入佇列，等主迴圈空閒時再顯示

4. **EditFilesTool 替換衝突**：`readline()` 用於選擇替換項目。在 sub-agent 情境下，若 `replace_text_mode=ask`，同樣會阻塞等待使用者輸入。

### 確認流程狀態機

```
┌─────────────┐
│ Agent 執行   │
│ Tool / Task  │
└──────┬──────┘
       │
       ▼
┌─────────────┐     是      ┌──────────────┐
│ 需要確認？   │────────────→│ 執行操作      │
└──────┬──────┘             └──────────────┘
       │ 否
       ▼
┌─────────────┐     y/yes    ┌──────────────┐
│ 顯示 Prompt  │────────────→│ 執行操作      │
│ read_key()   │              └──────────────┘
│ readline()   │
└──────┬──────┘
       │ n/no / 其他
       ▼
┌─────────────┐
│ 跳過 / 取消  │
│ 回傳錯誤     │
└─────────────┘
```

### 實作計畫

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
| 5 | run_loop() 迭代邏輯 | `agent_` run_stream |

### Phase 3: Config + Agent 整合

| # | 項目 | 內容 |
|---|------|------|
| 6 | Config 新增設定結構 | `[llm_agent]`、`[net_agent]`、`[multi_agent]` |
| 7 | Agent::load_config() 整合 | 根據 config 建立 MultiAgent、SubAgentLLM 實例，並註冊為 tools |

### Phase 4: SubAgentNet（WebSocket client）

| # | 項目 | 內容 |
|---|------|------|
| 8 | SubAgentNet WebSocket 連線 | 使用 httplib 的 WebSocket API，類似 Telegram Bot 的事件驅動模式 |

### Phase 5: Multi-Agent 使用者確認機制（待規劃）

| # | 項目 | 內容 |
|---|------|------|
| 9 | SubAgentLLM 共用 KeyWatcher | 本地 sub-agent 直接使用 Server 的 `KeyWatcher`，無需額外機制 |
| 10 | SubAgentNet 獨立確認 | Client 擁有自己的終端和 `KeyWatcher`，確認在 Client 側處理 |
| 11 | 非同步確認佇列（未來） | 若 sub-agent 需非同步執行，實作確認訊息佇列，避免與主迴圈 readline 衝突 |
