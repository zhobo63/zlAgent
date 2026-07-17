# ZL Agent 優化清單


## [ ] llm供應介面

doc/llm_provider.md

## [ ] telegram User confirm

## [ ] KeyWatcher readline

- refresh speed
- 每次按键都重新计算候选项
- 即使前缀没有变化（比如移动光标），也会重新扫描目录和排序
- 完整重绘而非增量更新
- ANSI 字符串频繁分配

[X] 增量渲染（Incremental Redraw）
[ ] build_candidates() 重複掃描目錄
[ ] read_key_thread() 推送按鍵時，通知 condition_variable
[ ] `History::add()` 改用 deque | 歷史記錄操作從 O(n) → O(1) |
[X] LineBuffer::move_down move_up 錯誤

## [ ] file_utils.cpp

| # | 問題 | 影響 |
|---|------|------|
| 7 | `match_glob()` 缺少 `**` 遞迴萬用字元 | pattern `src/**/*.cpp` 無法正確匹配 |

| # | 問題 | 影響 |
|---|------|------|
| 9 | `Base64Decode()` 無效輸入靜默錯誤 | 非 Base64 字元被忽略，無警告 |
| 10 | `apply_blocks()` 排序不穩定 | 同位置 block 順序不確定 |

## [ ] use edit_files

tmp/ 測試生成一個100行左右的代碼 並使用 edit_files 工具`同時`修改 5處地方
測試完後 把你測試的步驟 包含生成的 原本內容 修改指令 寫到 tmp/test_edit_files.md 
tmp/test_multi_edit.cpp 測試使用 edit_files 工具`同時`修改 5處地方

## [ ] use read_files

## [ ] parse_cpp_outline

- file_utils.h

namespace agent {
class SubAgent {
public:
    SubAgent(const std::string& name, const std::string& description = "");

    const std::string& get_name() const { return name_; }
    const std::string& description() const {return description_; }

    // Execute a task and return the result string.
    std::string execute(const std::string& task);

    // Run a mini reasoning loop (max 5 iterations per sub-agent).
    virtual ChatResponse run_loop(const std::string& task) { return ChatResponse{}; }
protected:
    std::string name_;
    std::string description_;
};
class Agent;

class SubAgentLLM: public SubAgent {
public:
    SubAgentLLM(const std::string& name, const std::string& description = "");

    // Set the working directory. This creates an internal Agent that loads
    // zlagent.ini from the given directory and generates a project overview
    // as the tool description for LLM routing.
    void set_workdir(const std::string workdir);

    // Override the system prompt of the internal agent. If empty, the built-in
    // or config-based system prompt is used instead.
    void set_system_prompt(const std::string& prompt);

    ChatResponse run_loop(const std::string& task) override;
private:
    mutable std::unique_ptr<Agent> agent_;
};

};

# File outline for multi_agent.h

 14 namespace agent
 22  class SubAgent
 26  ├ get_name()
 27  ├ description()
 30  ├ execute()
 33  └ run_loop() [virtual]
 45  class SubAgentLLM : SubAgent
 52  ├ set_workdir()
 56  ├ set_system_prompt()
 58  └ run_loop() [override]
 67  class SubAgentNet : SubAgent
 69  ├ struct Config
 76  │ ├ load()
 78  │ └ save()
 84  ├ start()
 87  ├ stop()
 90  ├ is_connected()
 94  ├ ask_confirm()
104  ├ connection_loop() [private]
107  ├ heartbeat_loop() [private]
110  ├ handle_message() [private]
114  └ send_confirm_request() [private]
