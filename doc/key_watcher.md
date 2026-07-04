# KeyWatcher — 全域鍵盤監控模組

## 簡介

`KeyWatcher` 是一個背景執行緒，持續監聽使用者輸入的按鍵事件（特別是 `Ctrl-C` 和 `ESC`），並透過回呼函式通知呼叫端。支援 Windows / POSIX 跨平台。

## 公共 API

### KeyWatcher 基本操作

| 方法 | 說明 |
|------|------|
| `static void on_key(KeyCallback cb)` | 註冊按鍵偵測回呼函式（`ch`: 3=Ctrl-C, 27=ESC） |
| `static void clear_callback()` | 清除回呼函式 |
| `static void start()` | 啟動監控背景執行緒（安全、冪等） |
| `static void stop()` | 停止監控並回收資源 |

### readline 輸入列

| 方法 | 說明 |
|------|------|
| `static std::string readline(const char* prompt, ReadlineCallback cb = nullptr)` | 阻塞式行編輯器，回傳使用者輸入的字串（Enter 回傳、Ctrl+C 回傳空字串） |
| `static void add_keywords(const std::vector<std::string>& keywords)` | 註冊自動完成關鍵字（全域共享） |

### KeyWatcher 靜態成員

| 成員 | 說明 |
|------|------|
| `static History history` | 歷史紀錄實例（全域唯一） |
| `static std::vector<Key> K_ZERO ~ K_SHIFT_ENTER` | 所有支援的按鍵代碼 |

---

## Key 結構體

```cpp
struct Key {
    union {
        unsigned char code[4];   // UTF-8 位元組（size > 0）或 Unicode code point（Windows）
        uint32_t ch;             // Windows 下的 code point
    };
    int size;                    // >0 = 有效位元組數；<0 = 特殊按鍵代碼

    constexpr Key(uint32_t c = 0, int s = 0) : ch(c), size(s) {}
};
```

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

1. `start()` 建立背景執行緒，每 50ms 輪詢一次鍵盤狀態。
2. 偵測到按鍵時呼叫使用者註冊的回呼函式。
3. `stop()` join 執行緒並回收資源。

### 使用範例

```cpp
agent::KeyWatcher::on_key([](int ch) {
    if (ch == 3) std::cout << "Ctrl-C detected\n";
    else if (ch == 27) std::cout << "ESC detected\n";
});
agent::KeyWatcher::start();

// ... 主程式邏輯...

if (/* interrupted */) { /* 處理中斷 */ }

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
- `cb`：每個按鍵都會觸發的回呼，可用來即時通知上層（例如設定旗標中斷 readline）
- 回傳值：**Enter** → 目前輸入內容；**Ctrl+C / ESC** → 空字串 `""`

### 按鍵行為總覽

| 按鍵 | 行為 |
|------|------|
| `Enter` | 回傳當前輸入文字（多行模式下 Enter 也是回傳，非插入換行） |
| `Ctrl+C` | 清空輸入文字，回傳空字串 `""` |
| `ESC` | 回傳空字串 `""` |
| `Alt+Enter` / `Ctrl+Enter` / `Shift+Enter` | 插入換行 `\n`，啟用多行顯示模式 |

### UTF-8 支援

使用專案內建的 `include/utf8.h` 處理 UTF-8 編碼。游標移動（`↑↓←→`）可在整個多行緩衝區內自由移動（非僅當前行）。

---

## 自動完成

觸發方式：**Tab**

### 候選來源（混合在同一池中，不區分大小寫）

| 來源 | 說明 |
|------|------|
| **關鍵字** | 透過 `KeyWatcher::add_keywords()` 註冊的全域共享關鍵字 |
| **檔案/目錄名** | 根據輸入的字串動態掃描路徑下的檔案和目錄 |

### 路徑感知規則

系統會根據游標前的文字自動判斷補全來源：

| 輸入內容 | 行為 |
|----------|------|
| `inc` | 從整個候選池中匹配（關鍵字 + 目前目錄的檔案/目錄名） |
| `include/ke` | 偵測到路徑分隔符 `/`，僅掃描 `include/` 下的檔案補全 |
| `hel` | 無路徑分隔符，從整個候選池中匹配（關鍵字 + 目前目錄的檔案/目錄名） |

### 候選排序規則

所有候選項目按**字母順序**排列（A→Z），不區分大小寫。

### Tab 觸發流程

| 候選數 | 行為 |
|--------|------|
| **0** | 不做任何動作 |
| **1** | 顯示暗示文字（hint），游標後方以淡化/灰色顯示剩餘補全文字 |
| **≥2** | 先套用「最長公共字首」自動填入，然後彈出選單讓使用者選擇 |

### 暗示文字（Hint）

當 Tab 後只有 **1 個候選**時，將剩餘補全文字以淡化/灰色顯示在游標後方：

```
提示: inc|                    ← | = 游標位置，無 hint
提示: inc[lude/]              ← [lude/] = hint（灰色渲染）
提示: include/k               ← 使用者輸入 k，hint 自動更新
提示: include/ke[y_watcher.md]← hint 持續跟隨匹配
```

| 項目 | 說明 |
|------|------|
| **觸發條件** | Tab 後只有 1 個候選時 |
| **顯示位置** | 游標後方，以淡化/灰色文字渲染（ANSI `\x1b[2m`） |
| **Tab 填入** | 按 Tab → hint 文字填入緩衝區，游標移至末尾 |
| **自動更新** | 繼續輸入時 hint 跟隨匹配；不匹配則消失 |
| **→（右箭頭）** | 從 hint 消耗一個字元到緩衝區 |
| **←（左箭頭）** | 把緩衝區最後一個字元移回 hint |
| **Backspace** | 清除 hint，然後正常刪除字元 |

### 選單行為

- 每頁最多顯示 **9 項**候選
- 選項超過 9 項時，`PgUp` / `PgDown` 切換頁面（不展開全部候選）
- `↑↓←→` 跨欄移動游標，`Enter` 確認選用
- `1~9` 數字鍵直接選取對應項目

### 最長公共字首（Longest Common Prefix）

在所有候選項中，從開頭開始逐字元比較，找到所有候選項都共有的前綴部分。

**範例：**
```
輸入: "inc"
候選: ["include/", "index.html", "init.c"]
最長公共字首: "in"  ← 三個候選都以 "in" 開頭，第三個字元不同（c/x/t）
填入結果: "in" + 彈出選單
```

**行為：** 先將最長公共字首自動填入緩衝區，然後再彈出選單讓使用者選擇剩餘差異部分。

---

## 歷史紀錄

| 按鍵 | 行為 |
|------|------|
| `↑` (Up) | 往舊歷史移動；游標停在行末 |
| `↓` (Down) | 往新歷史移動；游標停在緩衝區末端 |

- **去重規則**：新增時移除所有同內容的舊項（非僅最新一筆）
- **不支援持久化**：歷史紀錄僅存在記憶體中，程式重啟後消失
- **全域唯一**：`history` 是 `KeyWatcher` 的靜態成員，多個 `readline()` 呼叫共用同一份歷史

### History API

| 方法 | 說明 |
|------|------|
| `void add(const std::string& entry)` | 新增一筆紀錄（自動去重） |
| `bool prev()` | 往上游一筆，回傳是否移動成功 |
| `bool next()` | 往下游一筆，回傳是否移動成功 |
| `const std::string* get_current() const` | 取得目前瀏覽的項目指標（若未瀏覽則回傳 `nullptr`） |
| `bool is_browsing() const` | 檢查是否正在瀏覽歷史 |
| `void reset()` | 重置瀏覽狀態 |

---

## Ctrl+V 貼上

- **Windows**：透過 `GetClipboardData(CF_UNICODETEXT)` 從剪貼簿讀取文字並插入游標處。
- **POSIX (Linux / macOS)**：v1 未實作（函式回傳空字串）。

---

## 跨平台實作差異

| 項目 | Windows (`_WIN32`) | POSIX (Linux / macOS) |
|------|---------------------|------------------------|
| **鍵盤讀取** | `ReadConsoleInputW()` — 直接取得 Unicode code point，支援 UTF-8 和代理對（surrogate pairs） | `read(STDIN_FILENO)` + raw mode — 手動解析 escape sequence 和 UTF-8 位元組序列 |
| **鍵盤初始化** | 儲存並設定 `ENABLE_WINDOW_INPUT \| ENABLE_MOUSE_INPUT` 模式 | `tcsetattr()` 關閉 `ICANON \| ECHO` |
| **游標位置擷取** | `GetConsoleScreenBufferInfo()` — 立即回傳，不需 I/O | DSR (`\x1b[6n`) 同步請求 + `select()` 非同步讀取回應 |
| **終端尺寸** | `GetConsoleScreenBufferInfo()` 的 `srWindow` | `ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)` |
| **貼上功能** | ✅ 完整實作（`OpenClipboard` / `GetClipboardData`） | ❌ 留空函式，回傳空字串 |

### Windows 按鍵解析細節

```
ReadConsoleInputW()
  → KEY_EVENT + bKeyDown
    → wVirtualKeyCode == 27          → K_ESC
    → wVirtualKeyCode == 'V' + Ctrl  → K_CTRL_V
    → VK_RETURN + Alt                → K_ALT_ENTER
    → VK_RETURN + Ctrl               → K_CTRL_ENTER
    → VK_RETURN + Shift              → K_SHIFT_ENTER
    → VK_RETURN                      → K_ENTER
    → VK_CONTROL + C                 → K_CTRL_C
    → 方向鍵                         → K_UP/DOWN/LEFT/RIGHT
    → UnicodeChar (含代理對)          → UTF-8 編碼的 Key
```

### POSIX 按鍵解析細節

```
read(STDIN_FILENO) — raw mode
  → buf[0] == ESC
    → '[' + key                        → K_UP/DOWN/LEFT/RIGHT/HOME/END/PgUP/PgDOWN
    → 'O' + key                         → K_END/HOME/PgUP/PgDOWN (xterm 相容)
    → '\r'                              → K_ENTER / K_CTRL_ENTER
  → buf[0] == TAB                       → K_TAB
  → buf[0] == 127/8                     → K_BACKSPACE
  → buf[0] == 22                        → K_CTRL_V
  → buf[0] == 3                         → K_CTRL_C
  → buf[0] >= 0x20 && < 0x7F            → ASCII Key (size=1)
  → UTF-8 序列                           → 多位元組 Key
```

---

## LineBuffer 結構

`LineBuffer` 是 `readline()` 的內部資料結構，封裝了輸入緩衝區、游標位置、提示字串和選單狀態。

| 成員 | 型別 | 說明 |
|------|------|------|
| `prompt` | `std::string` | 固定的提示字串（無法刪除） |
| `text` | `std::vector<Key>` | 使用者輸入的字元（每個 `Key` 是一個 UTF-8 字元） |
| `pos` | `size_t` | 游標位置（字元級偏移，0 ~ text.size()） |
| `row` / `col` | `int` | 游標的顯示座標（1-based） |
| `hint` | `std::string` | 補完提示文字（灰色渲染） |
| `candidates` | `std::vector<std::string>` | 目前候選清單 |
| `selected` / `page_offset` | `int` / `size_t` | 選單游標和分頁偏移量 |

### LineBuffer 主要方法一覽

| 方法 | 說明 |
|------|------|
| `recompute()` | 根據目前游標位置重新計算 row/col |
| `insert_char(const Key& k)` | 在游標處插入一個字元 |
| `backspace()` | 刪除游標前的字元（不會刪到 prompt） |
| `move_left()` / `move_right()` | 左右移動一列 |
| `move_up(int term_width)` / `move_down(int term_width)` | 上下移動一行（跨行） |
| `input()` | 回傳使用者輸入的字串（不含 prompt） |
| `get_prefix()` | 回傳游標前的字串（不含 hint） |
| `suffix()` | 回傳游標後的字串 |
| `display_text()` | 回傳完整顯示內容（prompt + text） |
| `resize(size_t n)` | 調整 text 大小並修正 pos |
| `prefix_start()` | 從游標往回找單詞邊界（遇到空白或 `@` 停止） |
| `apply_hint()` | 將 hint 文字套用到 text 中 |
| `print_hint()` / `clear_hint()` | 顯示/清除灰色提示文字 |

---

## 已知問題與限制

- **貼上速度慢**：POSIX 端的貼上功能尚未實作（v1 回傳空字串）。
- **自動完成大小寫**：候選排序使用不區分大小寫比較，但填入時會使用候選的原始大小寫。
- **歷史紀錄未持久化**：程式重啟後歷史紀錄會消失。

---

## 實作步驟清單

### Phase 1 — 基礎輸入與鍵盤處理 ✅

- [x] 1.1 建立 `readline()` 基本框架：raw mode 切換、Enter/Ctrl+C 回傳
- [x] 1.2 實作 UTF-8 字元讀取（Windows: `ReadConsoleInput`；POSIX: raw mode）
- [x] 1.3 實作游標移動：`←→↑↓`，支援多行緩衝區內自由移動
- [x] 1.4 實作 `Alt+Enter` 插入換行、多行顯示
- [x] 1.5 實作 Ctrl+V 貼上（Windows: `GetClipboardData`；POSIX: 留空提示）

### Phase 2 — 歷史紀錄 ✅

- [x] 2.1 建立歷史紀錄資料結構（vector，上限可設）
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
src/key_watcher.cpp          (1436 行)
├── Key static members       (L18-37)        ← K_ZERO ~ K_SPACE 靜態實例
├── init_keyboard / close_keyboard   (L52-91) → Windows: SetConsoleMode / POSIX: tcsetattr
├── kbhit / getch              (L92-105)     → Windows: _kbhit/_getch / POSIX: select/getchar
├── send_enter                 (L124-137)    → 注入 Enter 中斷輪詢
├── on_key / start / stop      (L139-168)    → 背景監控執行緒
├── term namespace             (L175-283)    → ANSI 控制碼（游標、顏色、終端尺寸）
├── utf8 helpers               (L289-418)    → UTF-8 編解碼、顯示寬度計算
├── LineBuffer methods         (L425-603)    → 緩衝區操作（插入、刪除、游標移動）
├── History class              (L610-648)    → 歷史紀錄（新增、翻閱、去重）
└── readline()                 (L1130-1434)  → 主循環：渲染 → 讀取按鍵 → 處理特殊鍵/輸入/自動完成

include/key_watcher.h          (232 行)
├── Key struct                 (L13-49)      ← 按鍵結構體 + 靜態成員宣告
├── KeyWatcher class           (L55-224)     ← 主要類別介面
│   ├── on_key / start / stop              → 背景監控 API
│   ├── readline()                         → 行編輯器入口
│   ├── add_keywords()                     → 註冊自動完成關鍵字
│   ├── History                            → 歷史紀錄巢狀結構
│   └── LineBuffer                         → 輸入緩衝區巢狀結構
└── Key::read_key()            (L83-87)      → 單鍵讀取（跨平台）
```
