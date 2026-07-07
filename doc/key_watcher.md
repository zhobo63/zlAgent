# KeyWatcher — 全域鍵盤監控模組

## 簡介

`KeyWatcher` 是一個跨平台鍵盤輸入模組，提供兩種使用模式：
1. **背景監控模式**（`on_key / start / stop`）— 持續監聽按鍵事件並透過回呼通知呼叫端。
2. **readline 互動模式** — 阻塞式行編輯器，支援游標移動、歷史紀錄、自動完成選單與 hint 提示。

支援 Windows（`ReadConsoleInputW`）/ POSIX（raw mode + `select()`）跨平台。

## 公共 API

### KeyWatcher 基本操作（Original API）

| 方法 | 說明 |
|------|------|
| `static void on_key(KeyCallback cb)` | 註冊按鍵偵測回呼函式（`KeyCallback = std::function<void(int)>`，參數為虛擬鍵碼） |
| `static void start()` | 啟動監控背景執行緒（冪等：重複呼叫無效） |
| `static void stop()` | 停止監控並回收資源（join 執行緒、delete thread） |

### readline 輸入列（readline API）

| 方法 | 說明 |
|------|------|
| `static Key read_key()` | 從按鍵佇列讀取下一個按鍵，阻塞直到有輸入或 `s_running == false`（回傳 `K_ZERO`） |
| `static std::string readline(const char* prompt, ReadlineCallback cb)` | 阻塞式行編輯器，Enter 回傳輸入內容；Ctrl+C / ESC 回傳空字串 `""` |
| `static void init_keyboard()` | 初始化鍵盤 raw mode（Windows: 關閉 ENABLE_PROCESSED_INPUT；POSIX: 關閉 ICANON/ECHO） |
| `static void close_keyboard()` | readline 結束後恢復終端正常模式 |

### 自動完成 API（Completion API）

| 方法 | 說明 |
|------|------|
| `static void add_keywords(const std::vector<std::string>& keywords)` | 註冊自動完成關鍵字（全域共享，去重後追加） |

### 輔助函式

| 函式 | 說明 |
|------|------|
| `void send_enter()` | 向控制台輸入緩衝區注入 Enter 鍵，用於中斷阻塞的 `read_key_thread`（Windows: `WriteConsoleInputW`；POSIX: `ioctl(TIOCSTI)`） |

### KeyWatcher 靜態成員

| 成員 | 型別 | 說明 |
|------|------|------|
| `s_running` | `std::atomic<bool>` | 是否正在運行 |
| `s_callback` | `KeyCallback` | on_key 回呼函式 |
| `history` | `History` | 歷史紀錄實例（全域唯一） |
| `s_keywords` | `std::vector<std::string>` | 自動完成關鍵字池 |
| `s_read_thread` | `std::thread*` | readline 按鍵讀取執行緒 |
| `s_read_mutex` | `std::mutex` | readline 按鍵佇列的同步鎖 |
| `s_read_queue` | `std::vector<Key>` | readline 按鍵佇列（非同步寫入） |

---

## Key 結構體

```cpp
struct Key {
    union {
        unsigned char code[4];   // UTF-8 位元組（size > 0）或 Unicode code point（Windows）
        uint32_t ch;             // Windows 下的 code point
    };
    int size;                    // >0 = 有效位元組數；<0 = 特殊按鍵代碼
    int char_width;              // UTF-8 字元的顯示寬度（ASCII=1, CJK=2）

    constexpr Key(uint32_t c = 0, int s = 0) : ch(c), size(s) {}

    static Key from_codepoint(ucs4_t cp);
};
```

**說明：**
- `code[4]`：儲存 UTF-8 位元組（最多 4 個位元組）
- `ch`：Windows 上的 Unicode code point
- `size`：正數表示有效位元組數，負數表示特殊鍵
- `char_width`：字元的顯示寬度（控制字元=0, ASCII=1, CJK/全形=2）

### 支援的按鍵代碼

| 成員 | code | size | 說明 |
|------|------|------|------|
| `K_ZERO` | 0 | 0 | 無效/空按鍵 |
| `K_ESC` | 27 | -1 | ESC 鍵 |
| `K_UP` | 38 | -2 | 上箭頭 |
| `K_DOWN` | 40 | -3 | 下箭頭 |
| `K_LEFT` | 37 | -4 | 左箭頭 |
| `K_RIGHT` | 39 | -5 | 右箭頭 |
| `K_TAB` | 9 | -6 | Tab 鍵 |
| `K_ENTER` | 13 | -7 | Enter 鍵 |
| `K_BACKSPACE` | 8 | -8 | Backspace 鍵 |
| `K_DELETE` | 46 | -9 | Delete 鍵 |
| `K_PGUP` | 33 | -10 | Page Up |
| `K_PGDOWN` | 34 | -11 | Page Down |
| `K_HOME` | 36 | -12 | Home 鍵 |
| `K_END` | 35 | -13 | End 鍵 |
| `K_CTRL_V` | 22 | -14 | Ctrl+V（貼上） |
| `K_ALT_ENTER` | 13 | -15 | Alt+Enter（插入換行） |
| `K_CTRL_C` | 3 | -16 | Ctrl+C（中斷） |
| `K_CTRL_ENTER` | 13 | -17 | Ctrl+Enter（插入 `\n` + 啟用多行顯示） |
| `K_SHIFT_ENTER` | 13 | -18 | Shift+Enter（插入換行 + 啟用多行顯示） |
| `K_SPACE` | 32 | 1 | 空白字元 |

---

## 背景監控模式（on_key / start / stop）

### 工作流程

1. `start()` 建立背景執行緒，呼叫 `read_key_thread()`。
2. **Windows**：`ReadConsoleInputW()` 阻塞等待輸入事件；**POSIX**：`select()` + 10ms timeout 輪詢 stdin。
3. 偵測到按鍵時呼叫使用者註冊的回呼函式（傳遞虛擬鍵碼 `int`），並將解析後的 `Key` 推入佇列 `s_read_queue`。
4. `stop()` 將 `s_running` 設為 false，join 執行緒並 delete thread。

### 使用範例

```cpp
agent::KeyWatcher::on_key([](int vk) {
    if (vk == 3) std::cout << "Ctrl-C detected\n";
    else if (vk == 27) std::cout << "ESC detected\n";
});
agent::KeyWatcher::start();

// ... 主程式邏輯 ...

agent::KeyWatcher::stop();
```

---

## readline 模式（主要互動介面）

### 函式簽章

```cpp
using ReadlineCallback = std::function<void(const Key& k)>;
std::string KeyWatcher::readline(const char* prompt, ReadlineCallback cb);
```

- `prompt`：提示字串（例如 `"> "`、`"git commit: "`）
- `cb`：每個按鍵都會觸發的回呼，可用來即時通知上層
- 回傳值：**Enter** → 目前輸入內容；**Ctrl+C / ESC** → 空字串 `""`

### 按鍵行為總覽

| 按鍵 | 行為 |
|------|------|
| `Enter` | 回傳當前輸入文字，並加入歷史紀錄 |
| `Ctrl+C` | 清空輸入文字，回傳空字串 `""` |
| `ESC` | 回傳空字串 `""` |
| `Alt+Enter` / `Ctrl+Enter` / `Shift+Enter` | 插入換行 `\n`；若超過終端高度則自動滾動提示列 |
| `←→↑↓` | 游標移動（多行緩衝區內自由移動） |
| `Home` / `End` | 移到行首 / 行末 |
| `Backspace` | 刪除游標前字元；若有 hint 則先清除 hint |
| `Delete` | 刪除游標後字元 |
| `Tab` | 觸發自動完成（0 候選：無動作；1 候選：直接填入；≥2 候選：彈出選單） |
| `Ctrl+V` | 貼上剪貼簿文字（Windows 實作，POSIX 未實作） |

### UTF-8 支援

使用專案內建的 `include/utf8.h`（`utf8_mbtowc` / `Key::from_codepoint`）處理 UTF-8 編碼。
- Windows：`ReadConsoleInputW()` 直接取得 Unicode code point，含代理對（surrogate pairs）支援。
- POSIX：手動解析多位元組 UTF-8 序列。
- CJK / 全形字元顯示寬度為 2，ASCII 為 1，控制字元為 0。

---

## 自動完成

觸發方式：**Tab**

### 候選來源（混合在同一池中，不區分大小寫）

| 來源 | 說明 |
|------|------|
| **關鍵字** | 透過 `KeyWatcher::add_keywords()` 註冊的全域共享關鍵字 |
| **檔案/目錄名** | 根據輸入的字串動態掃描路徑下的檔案和目錄（`std::filesystem::directory_iterator`） |

### 路徑感知規則

系統會根據游標前的文字自動判斷補全來源：

| 輸入內容 | 行為 |
|----------|------|
| `/h` 或 `/help` | **指令模式**：以 `/` 開頭且無第二個 `/`，僅匹配關鍵字池 |
| `include/ke` | **路徑模式**：偵測到路徑分隔符 `/`，掃描 `include/` 下的檔案補全（相對路徑會接上 `current_path()`） |
| `inc` | **混合模式**：無路徑分隔符，從整個候選池中匹配（關鍵字 + 目前目錄的檔案/目錄名） |

### 候選排序規則

所有候選項目按**字母順序**排列（A→Z），不區分大小寫；長度相同時較短者優先。

### Tab 觸發流程

| 候選數 | 行為 |
|--------|------|
| **0** | 不做任何動作 |
| **1** | **直接填入**：將唯一候選插入緩衝區（`insert_completion()`） |
| **≥2** | 彈出自動完成選單讓使用者選擇 |

### 選單觸發時機

- **Tab**：彈出選單（≥2 候選）或直接填入（1 候選）
- **輸入字元時**：若候選池非空，自動更新 hint；若已開啟選單則更新候選清單 |

### Hint 提示（灰色補全文字）

當輸入字元後候選池非空且游標前有文字時，自動顯示第一個候選的剩餘部分作為 hint：

```
輸入: "inc" → hint: [lude/]
輸入: "incl" → hint: [ude/]
輸入: "include/k" → hint 持續跟隨匹配
```

| 項目 | 說明 |
|------|------|
| **觸發條件** | 輸入 ASCII 字元後，候選池非空且 prefix 不為空 |
| **顯示位置** | 游標後方，以淡化/灰色文字渲染（ANSI `\x1b[2m`） |
| **自動更新** | 繼續輸入時 hint 跟隨匹配；不匹配則消失 |
| **字元匹配** | 若輸入的字元與 hint 首字元相同，從 hint 消耗該字元到緩衝區 |
| **Backspace** | 先清除 hint，然後正常刪除字元 |

> **注意：** Hint 僅在輸入 ASCII 字元時自動更新。Unicode 字元（`size >= 2`）會清除 hint。

### 選單行為

- 每頁最多顯示 **9 項**候選（`MAX_DISPLAYED = 9`）
- 選項超過 9 項時，`PgUp` / `PgDown` / `←→` 切換頁面
- `↑↓` 移動游標選擇項目
- `Enter` / `Tab` 確認選用當前選中項目
- `1~9` 數字鍵直接選取對應項目（1-based）
- `ESC` / `Delete` 取消選單
- 選單關閉時**不清除螢幕內容**（僅設定旗標 `is_completion_active = false`，等待下次渲染覆蓋）
- 輸入普通 ASCII 字元會退出選單並由主循環處理

### 選單分頁邏輯

| 按鍵 | 行為 |
|------|------|
| `↓` | 向下移動一項；若到達當前頁面底部且還有更多候選，則翻到下一頁並將 selected 重置為 0 |
| `↑` | 向上移動一項；若到達頂部且還有前一頁，則翻到上一頁並將 selected 設為最後一項 |
| `PgDown` / `→` | 翻到下一頁（selected 保持在當前位置，若超出新頁面範圍則 clamp） |
| `PgUp` / `←` | 翻到上一頁（同上） |

---

## 歷史紀錄

| 按鍵 | 行為 |
|------|------|
| `↑` (Up) | 若游標不在行首且未瀏覽歷史：在多行緩衝區內向上移動一行；否則往舊歷史移動，游標停在行末 |
| `↓` (Down) | 若正在瀏覽歷史：往新歷史移動；回到最新時恢復原始文字 |

- **去重規則**：新增時移除所有同內容的舊項（非僅最新一筆）
- **上限**：500 筆
- **不支援持久化**：歷史紀錄僅存在記憶體中，程式重啟後消失
- **全域唯一**：`history` 是 `KeyWatcher` 的靜態成員，多個 `readline()` 呼叫共用同一份歷史
- 每次進入 `readline()` 時會 `reset()` 瀏覽狀態

### History API

| 方法 | 說明 |
|------|------|
| `void add(const std::string& entry)` | 新增一筆紀錄（自動去重，上限 500） |
| `bool prev()` | 往上游一筆（索引遞增），回傳是否移動成功 |
| `bool next()` | 往下游一筆（索引遞減），回傳是否移動成功 |
| `const std::string* get_current() const` | 取得目前瀏覽的項目指標（若未瀏覽則回傳 `nullptr`） |
| `bool is_browsing() const` | 檢查是否正在瀏覽歷史 |
| `void reset()` | 重置瀏覽狀態（`current_idx = -1`） |

**注意：** 歷史紀錄以 **newest first** 排序，索引 0 = 最近一筆。

---

## Ctrl+V 貼上

- **Windows**：透過 `GetClipboardData(CF_UNICODETEXT)` 從剪貼簿讀取文字並插入游標處（逐字元轉為 Key）。
- **POSIX (Linux / macOS)**：未實作，函式回傳空字串。

---

## 跨平台實作差異

| 項目 | Windows (`_WIN32`) | POSIX (Linux / macOS) |
|------|---------------------|------------------------|
| **鍵盤讀取** | `ReadConsoleInputW()` — 直接取得 Unicode code point，支援 UTF-8 和代理對（surrogate pairs） | `select()` + `read(STDIN_FILENO)` — raw mode，手動解析 escape sequence 和 UTF-8 位元組序列 |
| **鍵盤初始化** | 關閉 `ENABLE_PROCESSED_INPUT`（使 Ctrl+C 作為普通 KEY_EVENT 而非 SEH exception），儲存原始模式供恢復 | `tcsetattr()` 關閉 `ICANON \| ECHO`，儲存原始 termios 供恢復 |
| **按鍵佇列機制** | `ReadConsoleInputW()` 阻塞等待 → 解析後推入 `s_read_queue` | `select()` + 10ms timeout → drain 所有可用位元組（256 byte buffer）→ 逐字節解析並推入佇列 |
| **貼上功能** | ✅ 完整實作（`OpenClipboard` / `GetClipboardData`） | ❌ 留空函式，回傳空字串 |
| **Alt+Enter** | `VK_RETURN + Alt` → `K_ALT_ENTER` | `ESC + '\r'` → `K_ALT_ENTER` |

### Windows 按鍵解析細節

```
ReadConsoleInputW()
  → KEY_EVENT + bKeyDown
    → wVirtualKeyCode == 27          → K_ESC
    → 'V' + Ctrl                      → K_CTRL_V
    → VK_RETURN + Alt                 → K_ALT_ENTER
    → VK_RETURN + Ctrl                → K_CTRL_ENTER
    → VK_RETURN + Shift               → K_SHIFT_ENTER
    → 'C' + Ctrl                      → K_CTRL_C
    → VK_UP/DOWN/LEFT/RIGHT           → K_UP/DOWN/LEFT/RIGHT
    → VK_TAB                          → K_TAB
    → VK_RETURN                       → K_ENTER
    → VK_BACK                         → K_BACKSPACE
    → VK_DELETE (AsciiChar == 0)      → K_DELETE
    → VK_PRIOR                        → K_PGUP
    → VK_NEXT                         → K_PGDOWN
    → VK_HOME                         → K_HOME
    → VK_END                          → K_END
    → UnicodeChar (含代理對處理)       → UTF-8 編碼的 Key (from_codepoint)
```

### POSIX 按鍵解析細節

```
read(STDIN_FILENO) — raw mode, drain all available bytes
  → buf[0] == ESC (27)
    → '[' + key                        → K_UP/DOWN/LEFT/RIGHT/HOME/END/PgUP/PgDOWN
    → 'O' + key                         → K_END/HOME/PgUP/PgDOWN (xterm 相容)
    → '\r'                              → K_ALT_ENTER
    → other                             → K_ESC
  → buf[0] == TAB                       → K_TAB
  → buf[0] == 127/8                     → K_BACKSPACE
  → buf[0] == 22                        → K_CTRL_V
  → buf[0] == 3                         → K_CTRL_C
  → buf[0] == '\r'                      → K_ENTER (Ctrl+Enter 無法區分)
  → buf[0] >= 0x20 && < 0x7F            → ASCII Key (size=1)
  → UTF-8 多位元組序列                    → 多位元組 Key
```

> **注意：** POSIX 上 `Ctrl+Enter` 與普通 `Enter` 都產生 `\r`，無法區分，統一視為 `K_ENTER`。

---

## LineBuffer 結構

`LineBuffer` 是 `readline()` 的內部資料結構，封裝了輸入緩衝區、游標位置、提示字串和選單狀態。

### 成員變數

| 成員 | 型別 | 說明 |
|------|------|------|
| `prompt` | `std::string` | 固定的提示字串（無法刪除） |
| `text` | `std::vector<Key>` | 使用者輸入的字元（每個 `Key` 是一個 UTF-8 字元） |
| `pos` | `size_t` | 游標位置（字元級偏移，0 ~ text.size()） |
| `row` | `int` | 游標的顯示行座標（**1-based**，相對於 prompt_row） |
| `col` | `int` | 游標的顯示欄位座標（**1-based**） |
| `hint` | `std::string` | 補完提示文字（灰色渲染） |
| `hint_candidates` | `std::string` | 完整自動完成文字（可能比 hint 長，用於 `apply_hint()`） |
| `prompt_len` | `size_t` | 提示字串的位元組長度 |
| `cached_prompt_col` | `int` | 提示字串的顯示寬度（含 CJK 計算） |
| `is_completion_active` | `bool` | 是否正在使用自動完成選單 |
| `candidates` | `std::vector<std::string>` | 目前候選清單 |
| `selected` | `int` | 選單游標位置（0-based，相對於 page_offset） |
| `page_offset` | `size_t` | 分頁偏移量（當前頁面起始索引） |
| `input_col` | `int` | 進入自動完成模式時的游標欄位（用於恢復位置） |
| `prompt_row` | `int` | 提示列起始行座標（絕對值，用於計算滾動和清除範圍） |
| `is_display_dirty` | `bool` | 顯示是否髒污（true = 需要完整重繪；false = 僅移動游標） |

### LineBuffer 主要方法一覽

| 方法 | 說明 |
|------|------|
| `set_prompt(const std::string& p)` | 設定提示字串並計算顯示寬度 |
| `recompute()` | 根據目前游標位置重新計算 row/col（考慮 CJK 寬度和換行） |
| `insert_char(const Key& k)` | 在游標處插入一個字元，pos++ |
| `backspace()` | 刪除游標前的字元（不會刪到 prompt），回傳是否成功 |
| `move_left()` / `move_right()` | 左右移動一列 |
| `move_up(int term_width)` / `move_down(int term_width)` | 上下移動一行（跨行，考慮終端寬度換行） |
| `input() const` | 回傳使用者輸入的字串（不含 prompt，將 Key 轉為 UTF-8） |
| `get_prefix() const` | 回傳游標前的字串（從 prefix_start 到 pos） |
| `suffix() const` | 回傳游標後的字串 |
| `print_hint()` | 以灰色渲染 hint 文字（ANSI dim） |
| `clear_hint()` | 清除 hint 和 hint_candidates |
| `apply_hint()` | 將 hint_candidates 套用到 text 中（替換 prefix_start 之後的內容） |
| `set_text(const std::string& _text)` | 設定文字內容（UTF-8 → Keys），游標移至末尾 |
| `prefix_start() const` | 從游標往回找單詞邊界（遇到空白或 `@` 停止） |
| `show_completion_menu(_candidates)` | 顯示自動完成選單，處理終端滾動，回傳輸入列的 row |
| `hide_completion_menu(current_input_row)` | 隱藏選單（設定旗標，不清除螢幕內容） |
| `insert_completion(completion)` | 插入選中的自動完成到緩衝區（替換 prefix_start 之後的內容） |
| `draw_completion_menu(current_input_row)` | 繪製（或重繪）選單於螢幕上（最多 MAX_DISPLAYED=9 項，含分頁資訊） |
| `clear_prompt()` | 從 prompt_row 開始清除所有行並恢復游標到起始位置 |

---

## 已知問題與限制

- **貼上速度慢**：POSIX 端的貼上功能尚未實作（v1 回傳空字串）。
- **自動完成大小寫**：候選排序使用不區分大小寫比較，但填入時會使用候選的原始大小寫。
- **歷史紀錄未持久化**：程式重啟後歷史紀錄會消失。
- **POSIX Ctrl+Enter 無法區分**：`Ctrl+Enter` 與普通 `Enter` 都產生 `\r`，統一視為 `K_ENTER`。

---

## 實作步驟清單

### Phase 1 — 基礎輸入與鍵盤處理 ✅

- [x] 1.1 建立 `readline()` 基本框架：raw mode 切換、Enter/Ctrl+C 回傳
- [x] 1.2 實作 UTF-8 字元讀取（Windows: `ReadConsoleInput`；POSIX: raw mode）
- [x] 1.3 實作游標移動：`←→↑↓`，支援多行緩衝區內自由移動
- [x] 1.4 實作 `Alt+Enter` 插入換行、多行顯示
- [x] 1.5 實作 Ctrl+V 貼上（Windows: `GetClipboardData`；POSIX: 留空提示）

### Phase 2 — 歷史紀錄 ✅

- [x] 2.1 建立歷史紀錄資料結構（vector，**newest first**，上限 500）
- [x] 2.2 實作 `↑↓` 翻查：往上游標停行末、往下游標停緩衝區末端
- [x] 2.3 新增時移除所有同內容舊項（去重）

### Phase 3 — 自動完成 ✅

- [x] 3.1 建立候選池機制：`add_keywords()` API + 檔案掃描函式
- [x] 3.2 實作路徑感知邏輯：偵測 `/` 分隔符，決定補全來源
- [x] 3.3 實作 Tab 觸發流程：0/1/≥2 候選的分支處理
- [x] 3.4 實作暗示（hint）顯示：唯一匹配時在游標後方顯示灰色提示文字
- [x] 3.5 實作最長公共字首補全
- [x] 3.6 實作選單渲染：9 項上限、分頁切換
- [x] 3.7 實作選單互動：`↑↓←→` 移動、`Enter` 確認、`1~9` 直接選取
- [x] 3.8 實作 `PgUp/PgDown` 分頁切換

### Phase 4 — 整合與測試 ⬜

- [ ] 4.1 整合 KeyWatcher 中斷回呼：readline 執行期間仍可偵測 Ctrl+C/ESC
- [ ] 4.2 跨平台編譯測試（Windows + Linux）
- [ ] 4.3 UTF-8 邊界測試：CJK 字元、多行換行、貼上含換行的文字
- [ ] 4.4 自動完成邊界測試：空目錄、大量候選、路徑包含空格

---

## 程式碼結構一覽

```
src/key_watcher.cpp          
├── Key static members       ← K_ZERO ~ K_SPACE 靜態實例
├── init_keyboard / close_keyboard   → Windows: SetConsoleMode / POSIX: tcsetattr
├── kbhit / getch              → Windows: _kbhit/_getch / POSIX: select/getchar (POSIX only)
├── send_enter                 → 注入 Enter 中斷輪詢
├── on_key / start / stop      → 背景監控執行緒
├── utf8 helpers               → utf8_char_width() / Key::from_codepoint() / utf8_to_keys()
├── compute_prompt_width       → 提示字串寬度計算（含 CJK）
├── LineBuffer methods         → 緩衝區操作（插入、刪除、游標移動）
│   ├── basic operations       → insert_char / backspace / move_left/right/up/down
│   ├── text queries           → input() / get_prefix() / suffix() / prefix_start()
│   ├── hint management        → print_hint() / apply_hint() / clear_hint()
│   └── completion operations  → insert_completion / draw/clear/show/hide menu / clear_prompt
├── History class              → 歷史紀錄（新增、翻閱、去重，上限 500）
├── Completion helpers         → ci_starts_with() / normalize_path() / get_path() / scan_directory() / build_candidates()
├── get_clipboard_text         → Windows: GetClipboardData(CF_UNICODETEXT) / POSIX: 空字串
├── read_key / push_key_queue  → readline 按鍵佇列機制（非同步寫入，mutex 保護）
├── read_key_thread            → readline 背景執行緒（Windows/POSIX 按鍵讀取與解析）
└── readline()                 → 主循環：渲染 → 讀取按鍵 → 處理特殊鍵/輸入/自動完成

include/key_watcher.h          
├── Key struct                 ← 按鍵結構體 + 靜態成員宣告
│   ├── from_codepoint()       → 從 Unicode code point 建立 Key（UTF-8 編碼）
│   └── K_ZERO ~ K_SPACE       → 所有支援的按鍵代碼
├── KeyWatcher class           ← 主要類別介面
│   ├── Original API           → on_key / start / stop
│   ├── readline API           → read_key() / readline() / init_keyboard() / close_keyboard()
│   ├── Completion API         → add_keywords()
│   ├── History                → 歷史紀錄巢狀結構（newest first, max 500）
│   └── LineBuffer             → 輸入緩衝區巢狀結構 + 選單方法
├── Private static members     → s_running / s_callback / history / s_keywords / s_read_thread / s_read_mutex / s_read_queue
└── Completion helpers         → ci_starts_with() / normalize_path() / get_path() / scan_directory() / build_candidates()
```
