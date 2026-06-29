# Message App Protocol — Agent ↔ Chat Platform 通訊協議

- ZL Agent 與即時通訊平台Telegram之間的溝通協議，讓 Agent 能作為聊天機器人接收訊息、執行工具、回覆使用者。
- **技術基礎：** 基於 [httplib.h](include/httplib.h) 實作 HTTP Server / WebSocket Server / SSE Client，零額外依賴。
- 不使用webhook模式
- 訊息傳輸流程:
  Telegram -> Agent -> LLM -> Agent -> Telegram
- 回傳訊息由 event.h on_event 接收


---

## 規劃實作步驟

### 1. Telegram getUpdates（長輪詢拉取訊息）
- **使用 httplib::Client** 定期呼叫 Telegram Bot API 的 `/getUpdates` 端點
- **解析 JSON 回應**，提取 `message` 物件中的 `chat_id`、`text`、`date` 等欄位
- **將訊息內容轉換為 ChatMessage**（角色設為 `"user"`）並加入 `memory_`

### 2. Telegram sendMessage（發送回覆）
- **呼叫 `/sendMessage` 端點**，傳入 `chat_id`、`text`、`parse_mode` 等參數
- **處理可能的錯誤回應**（如超過字元限制、使用者已封鎖等）
- **將送出訊息的動作記錄到 memory_**

---
## httplib.h 角色映射

| httplib.h 功能 | 用途 |
|---------------|------|
| `httplib::Client` / `SSLClient` | 呼叫 Telegram Bot API（外部 HTTP Client） |
| `httplib::ClientImpl::StreamHandle` | SSE 串流回覆（Agent → Terminal 逐字輸出 LLM 結果，目前僅限終端機顯示） |
| `httplib::Server` + `.Post()` / `.Get()` | REST API 端點 |

---

## 傳輸層

### 通訊方式

Agent 與聊天平台之間的通訊採用長輪詢（Long Polling）模式：

- **長輪詢（Long Polling）** — Agent 主動拉取訊息（如 Telegram Bot API），無需外部 Webhook 端點。
### httplib::Client 呼叫外部 API

```cpp
// Telegram Bot API — Long Polling
httplib::Client tg_client("https://api.telegram.org");
auto res = tg_client.Get(
    "/bot" + bot_token + "/getUpdates",
    { {"timeout", "30"} }
);
```
### Telegram Bot API — getUpdates（長輪詢拉取訊息）

詳細的 API 定義請參閱 [telegram-bot-api.md](doc/ref/telegram-bot-api.md)。

**請求封包：**

```json
{
  "offset": 123456789,
  "limit": 100,
  "timeout": 30,
  "allowed_updates": ["message", "callback_query"]
}
```

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

### Telegram Bot API — sendMessage（發送訊息）

**請求封包：**

```json
{
  "chat_id": 987654321,
  "text": "收到！你剛剛說了：哈囉，機器人！",
  "parse_mode": "MarkdownV2",
  "reply_parameters": {
    "message_id": 45
  }
}
```

| 欄位 | 類型 | 必填 | 說明 |
|------|------|------|------|
| `chat_id` | Integer/String | ✅ | 目標聊天室的 ID（從 getUpdates 取得） |
| `text` | String | ✅ | 要發送的文字內容（最大 4096 字元） |
| `parse_mode` | String | ❌ | 格式化文字模式，可填 "MarkdownV2" 或 "HTML" |
| `reply_parameters` | Object | ❌ | 若想「指定回覆」某特定訊息，帶入該訊息的 message_id |

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
