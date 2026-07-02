# Message App Protocol — Agent ↔ Chat Platform 通訊協議

- ZL Agent 與即時通訊平台Telegram之間的溝通協議，讓 Agent 能作為聊天機器人接收訊息、執行工具、回覆使用者。
- **技術基礎：** 基於 [httplib.h](include/httplib.h) 實作 HTTP Client（SSLClient），零額外依賴。
- 不使用webhook模式
- 訊息傳輸流程:
  Telegram -> Agent -> LLM -> Agent -> Telegram
- 回傳訊息由 event.h on_event 接收


---

## 規劃實作步驟

### 1. Telegram getUpdates（長輪詢拉取訊息）
- **使用 httplib::SSLClient** 定期呼叫 Telegram Bot API 的 `/getUpdates` 端點
- **解析 JSON 回應**，提取 `message` 物件中的 `chat_id`、`text`、`date` 等欄位
- **追蹤 update_id**：記錄最高 Seen 的 update_id（使用 mutex 保護）
- **過濾非授權聊天室**：根據 `allowed_chat_ids` 設定過濾訊息
- **透過 event broker 發送事件**：以 `telegram.incoming` 事件類型發送

### 2. Telegram sendMessage（發送回覆）
- **呼叫 `/sendMessage` 端點**，傳入 `chat_id`、`text` 等參數
- **處理可能的錯誤回應**（如超過字元限制、使用者已封鎖等）
- **文字截斷**：自動將訊息截斷至 Telegram 的 4096 字元限制

### 3. Polling Loop（長輪詢迴圈）
- **使用 `httplib::SSLClient`** 持續呼叫 `/getUpdates` 端點
- **追蹤 `last_update_id_`**：使用 mutex 保護，記錄最高 Seen 的 update_id
- **錯誤重試機制**：網路錯誤時等待 5 秒後重試，JSON 解析失敗時等待 1 秒
- **成功處理後的短暫 pause**：每次輪詢結束後 sleep 100ms，避免無更新時的密集迴圈
- **允許聊天室過濾**：根據 `cfg_.allowed_chat_ids` 過濾非授權訊息
- **事件發送**：透過 `send_event("telegram.incoming", ...)` 將訊息發送至 event broker

---
## httplib.h 角色映射

| httplib.h 功能 | 用途 |
|---------------|------|
| `httplib::SSLClient` | 呼叫 Telegram Bot API（外部 HTTPS Client） |
| `httplib::ClientImpl::StreamHandle` | SSE 串流回覆（Agent → Terminal 逐字輸出 LLM 結果，目前僅限終端機顯示） |
| `httplib::Server` + `.Post()` / `.Get()` | REST API 端點 |

---

## 傳輸層

### 通訊方式

Agent 與聊天平台之間的通訊採用長輪詢（Long Polling）模式：

- **長輪詢（Long Polling）** — Agent 主動拉取訊息（如 Telegram Bot API），無需外部 Webhook 端點。

### httplib::SSLClient 呼叫外部 API

```cpp
// Telegram Bot API — Long Polling
httplib::SSLClient client("api.telegram.org");
client.set_read_timeout(60, 0);
client.set_write_timeout(15, 0);
auto res = client.Get(api_path() + "/getUpdates?offset=" + std::to_string(last_update_id_ + 1) + "&timeout=30&limit=100");
```
### Telegram Bot API — getUpdates（長輪詢拉取訊息）

詳細的 API 定義請參閱 [telegram-bot-api.md](doc/ref/telegram-bot-api.md)。

**請求參數（Query String）：**

| 欄位 | 類型 | 說明 |
|------|------|------|
| `offset` | Integer | 可選。進入下一次輪詢時，填入「最後一次收到的 update_id + 1」，避免重複拉取 |
| `limit` | Integer | 可選。一次最多拉取的訊息筆數（1-100，預設 100） |
| `timeout` | Integer | 可選。長輪詢的等待時間（秒，預設 0），建議設定 30 或以上 |

**回應封包：**

```json
{
  "ok": true,
  "result": [
    {
      "update_id": 123456789,
      "message": {
        "message_id": 45,
        "from": {
          "id": 987654321,
          "is_bot": false,
          "first_name": "王",
          "last_name": "小明",
          "username": "xiaoming_wang"
        },
        "chat": {
          "id": 987654321,
          "type": "private"
        },
        "date": 1719665000,
        "text": "哈囉，機器人！"
      }
    }
]
```

**核心欄位：**

| 欄位 | 說明 |
|------|------|
| `update_id` | 事件的唯一識別碼（用於更新 offset） |
| `message.chat.id` | 聊天室 ID，回覆訊息時必須填入此 ID |
| `message.text` | 使用者實際輸入的文字內容 |
| `message.from.first_name` | 發送者暱稱（用於 event payload） |

### Telegram Bot API — sendMessage（發送訊息）

**請求參數：**

```json
{
  "chat_id": 987654321,
  "text": "收到！你剛剛說了：哈囉，機器人！"
}
```

| 欄位 | 類型 | 必填 | 說明 |
|------|------|------|------|
| `chat_id` | Integer/String | ✅ | 目標聊天室的 ID（從 getUpdates 取得） |
| `text` | String | ✅ | 要發送的文字內容（最大 4096 字元，超出自動截斷） |

**成功回應：**

```json
{
  "ok": true,
  "result": {
    "message_id": 46,
    "from": {
      "id": 110293847,
      "is_bot": true,
      "first_name": "我的自動化助手",
      "username": "my_helper_bot"
    },
    "chat": {
      "id": 987654321,
      "type": "private"
    },
    "date": 1719665002,
    "text": "收到！你剛剛說了：哈囉，機器人！"
  }
}
```

**錯誤回應：**

```json
{
  "ok": false,
  "error_code": 403,
  "description": "Forbidden: bot was blocked by the user"
}
```

| 欄位 | 說明 |
|------|------|
| `error_code` | HTTP 狀態碼（如 400 Bad Request, 401 Unauthorized, 403 Forbidden） |
| `description` | 錯誤原因的具體文字說明，極度有助於 C++ 除錯 |

---

## Bot 啟動流程 — getMe

Agent 啟動時會先呼叫 `/getMe` 端點驗證 bot token 有效性：

**請求：**

```
GET /bot{token}/getMe
```

**成功回應：**

```json
{
  "ok": true,
  "result": {
    "id": 110293847,
    "is_bot": true,
    "first_name": "我的自動化助手",
    "username": "my_helper_bot"
  }
}
```

| 欄位 | 說明 |
|------|------|
| `ok` | 請求是否成功 |
| `result.id` | Bot ID |
| `result.is_bot` | 是否為 bot（應為 true） |
| `result.first_name` | Bot 暱稱 |
| `result.username` | Bot 使用者名稱 |

**失敗回應：**

```json
{
  "ok": false,
  "error_code": 401,
  "description": "Unauthorized: invalid token"
}
```

若 getMe 失敗，Agent **不會啟動 poll thread**，並記錄錯誤日誌。

---

## Event Payload — telegram.incoming

Agent 收到 Telegram 訊息後，透過 event broker 發送 `telegram.incoming` 事件：

**Payload：**

```json
{
  "chat_id": 987654321,
  "text": "哈囉，機器人！",
  "from_name": "王小明",
  "update_id": 123456789
}
```

| 欄位 | 類型 | 說明 |
|------|------|------|
| `chat_id` | Integer | 聊天室 ID（用於回覆） |
| `text` | String | 使用者輸入的文字 |
| `from_name` | String | 發送者暱稱（first_name） |
| `update_id` | Integer | Telegram update ID（用於追蹤 offset） |

---

## SSE Client — 串流回覆

Agent 透過 `httplib::Client` + StreamHandle 接收外部 LLM API 的串流回覆：

- **用途：** Terminal output 逐字輸出 LLM 結果（目前僅限終端機顯示）
- **流程：** 
  1. Agent 以 HTTP POST 開啟長連線至 LLM API `/v1/chat/completions`
  2. LLM API 以 SSE 格式回傳 token（`data: {"content": "..."}`）
  3. Agent 逐行解析 SSE chunk，透過 `TokenCallback` 回調即時輸出
- **實作細節：** 
  - 使用 `httplib::ClientImpl::StreamHandle` 維持長連線
  - 每 4096 bytes 讀取一次 buffer，解析 `data:` 開頭的 SSE line
  - 支援 ESC 中斷：按下 ESC 時關閉 StreamHandle 通知 LLM API 停止生成

> **注意：** 目前串流回覆僅在 terminal output 顯示。
---


---

## 訊息處理流程

```
┌──────────┐     ┌─────────────┐     ┌──────────┐     ┌──────────┐
│ Telegram │────▶│ Agent       │────▶│ LLM API  │────▶│ Terminal │
│ Bot API  │     │ (httplib)   │     │          │     │ Output   │
└──────────┘     └─────────────┘     └──────────┘     └──────────┘
```

1. **接收訊息：** Agent 透過 Telegram Bot API（長輪詢）接收使用者訊息
2. **處理請求：** Agent 解析訊息、執行工具、呼叫 LLM API
3. **串流回覆：** LLM API 以 SSE 格式回傳 token，Agent 即時輸出至 Terminal
