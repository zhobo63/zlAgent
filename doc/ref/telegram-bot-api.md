Telegram Bot API 在實作 Long Polling（長輪詢） 時最核心的 2 個 API 接口（Endpoint）以及它們對應的 HTTP 請求與回應封包定義（JSON 結構）。

1. 拉取訊息接口：getUpdates
此接口用於主動拉取用戶發送給機器人的訊息。

HTTP 方法：POST 或 GET（推薦使用 POST 並帶上 JSON 內容）

請求網址：[https://api.telegram.org/bot](https://api.telegram.org/bot)<YOUR_TOKEN>/getUpdates

Content-Type：application/json

📥 請求封包定義 (Request JSON)
JSON
{
  "offset": 123456789,
  "limit": 100,
  "timeout": 30,
  "allowed_updates": ["message", "callback_query"]
}
offset (Integer): 可選。進入下一次輪詢時，填入「最後一次收到的 update_id + 1」，用來向伺服器確認已讀，避免重複拉取。

limit (Integer): 可選。一次最多拉取的訊息筆數（1-100，預設 100）。

timeout (Integer): 可選。長輪詢的等待時間（單位：秒，預設 0）。強烈建議設定 30 或以上，讓連線掛機等待新訊息。

📤 回應封包定義 (Response JSON)
當有用戶傳送文字訊息時，Telegram 回傳的結構如下：

JSON
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
          "username": "xiaoming_wang",
          "language_code": "zh-hant"
        },
        "chat": {
          "id": 987654321,
          "type": "private",
          "first_name": "王",
          "last_name": "小明",
          "username": "xiaoming_wang"
        },
        "date": 1719665000,
        "text": "哈囉，機器人！"
      }
    }
  ]
}
💡 核心欄位解析：

update_id：事件的唯一識別碼（用於更新 offset）。

message.chat.id：聊天室 ID。回覆訊息時必須填入此 ID。

message.text：使用者實際輸入的文字內容。

2. 發送訊息接口：sendMessage
當程式處理完邏輯後，呼叫此接口將訊息回傳給使用者。

HTTP 方法：POST

請求網址：[https://api.telegram.org/bot](https://api.telegram.org/bot)<YOUR_TOKEN>/sendMessage

Content-Type：application/json

📥 請求封包定義 (Request JSON)
JSON
{
  "chat_id": 987654321,
  "text": "收到！你剛剛說了：哈囉，機器人！",
  "parse_mode": "MarkdownV2",
  "reply_parameters": {
    "message_id": 45
  }
}
chat_id (Integer/String): 必填。目標聊天室的 ID（從 getUpdates 取得）。

text (String): 必填。要發送的文字內容（最大 4096 個字元）。

parse_mode (String): 可選。格式化文字模式，可填 "MarkdownV2" 或 "HTML"，用來給文字加粗、斜體等。

reply_parameters (Object): 可選。若想「指定回覆」某特定訊息，可帶入該訊息的 message_id。

📤 回應封包定義 (Response JSON)
發送成功後，Telegram 會回傳該條「已送出訊息」的完整物件：

JSON
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
      "type": "private",
      "first_name": "王",
      "last_name": "小明",
      "username": "xiaoming_wang"
    },
    "date": 1719665002,
    "text": "收到！你剛剛說了：哈囉，機器人！"
  }
}
⚠️ 錯誤回應封包定義 (Error JSON)
無論呼叫哪一個接口，如果失敗（例如 Token 填錯、Chat ID 不存在、或者是被使用者封鎖），Telegram 都會回傳 ok: false 的制式錯誤封包：

JSON
{
  "ok": false,
  "error_code": 403,
  "description": "Forbidden: bot was blocked by the user"
}
error_code (Integer): HTTP 狀態碼（如 400 Bad Request, 401 Unauthorized, 403 Forbidden）。

description (String): 錯誤原因的具體文字說明，極度有助於 C++ 除錯。