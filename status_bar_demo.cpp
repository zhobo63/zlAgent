// FTXUI Status Bar Demo for ZL Agent
// Compile: g++ -std=c++17 status_bar.cpp -o status_bar -lftxui-component -lftxui-dom -lftxui-screen

#include <ftxui/dom/dom.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/mouse.hpp>
#include <thread>
#include <chrono>
#include <atomic>

using namespace ftxui;

// ─── 狀態列數據模型 ────────────────────────────────
struct AgentState {
    // LLM 連線狀態
    std::string connection_status = "Connected";
    bool connected = true;

    // 當前模型
    std::string model_name = "qwen2.5-coder-7b";

    // Token 使用量
    int tokens_used = 0;
    int max_tokens = 8192;

    // 迭代次數
    int current_iteration = 0;
    int max_iterations = 10;

    // 功能開關
    bool task_planning = true;
    bool self_reflection = true;
    bool multi_agent = false;

    // 記憶統計
    int memory_count = 0;
    int facts_count = 0;

    // 當前階段
    std::string current_phase = "Idle";
};

// ─── 狀態列渲染函數 ────────────────────────────────
Element RenderStatusBar(const AgentState& state) {
    // 連線狀態 — 用顏色區分
    auto connection_icon = state.connected ? text("🔗") : text("❌");
    auto connection_color = state.connected ? Color::Green : Color::Red;
    Element conn_status = colored(connection_color,
        hflow({
            connection_icon,
            text(" " + state.connection_status),
        })
    );

    // 模型名稱 — 藍色標記
    Element model_info = colored(Color::Blue | Color::Bold,
        text("🤖 " + state.model_name)
    );

    // Token 使用量 — 根據比例變色
    double ratio = static_cast<double>(state.tokens_used) / state.max_tokens;
    Color token_color;
    if (ratio < 0.5)       token_color = Color::Green;
    else if (ratio < 0.8)  token_color = Color::Yellow;
    else                   token_color = Color::Red;

    Element token_info = colored(token_color,
        text("🧠 " + std::to_string(state.tokens_used) + "/" +
             std::to_string(state.max_tokens / 1024) + "K")
    );

    // 迭代次數 — 接近上限時變紅
    Color iter_color = (state.current_iteration >= state.max_iterations - 2)
        ? Color::Red : Color::Cyan;

    Element iter_info = colored(iter_color,
        text("⚡ " + std::to_string(state.current_iteration) + "/" +
             std::to_string(state.max_iterations))
    );

    // 功能開關 — 啟用=綠色勾，關閉=灰色叉
    auto feature_label = [](const std::string& name, bool enabled) {
        if (enabled)
            return colored(Color::Green, text("✓ " + name));
        else
            return colored(Color::Gray, text("✗ " + name));
    };

    Element features = hflow({
        feature_label("Plan", state.task_planning),
        separatorH(1),
        feature_label("Reflect", state.self_reflection),
        separatorH(1),
        feature_label("MultiAgent", state.multi_agent),
    });

    // 記憶統計
    Element memory_info = colored(Color::Magenta,
        text("💾 Msg:" + std::to_string(state.memory_count) +
             " Fact:" + std::to_string(state.facts_count))
    );

    // 當前階段 — 動態變色
    Color phase_color;
    if (state.current_phase == "Idle")       phase_color = Color::Gray;
    else if (state.current_phase == "Thinking") phase_color = Color::Yellow | Color::Bold;
    else if (state.current_phase == "Executing") phase_color = Color::Cyan | Color::Bold;
    else if (state.current_phase == "Reviewing") phase_color = Color::Magenta | Color::Bold;
    else                                         phase_color = Color::White;

    Element phase_info = colored(phase_color,
        text("▶ " + state.current_phase)
    );

    // 快捷鍵提示 — 右側固定
    Element shortcuts = right_aligned(
        colored(Color::Gray,
            hflow({
                text("/help"), separatorH(1),
                text("/model"), separatorH(1),
                text("/status"), separatorH(1),
                text("Ctrl+C Quit"),
            })
        )
    );

    // 組合所有區塊，用分隔符號連接
    return vbox({
        separator(),
        hflow({
            conn_status,   separatorH(2),
            model_info,    separatorH(2),
            token_info,    separatorH(2),
            iter_info,     separatorH(2),
            features,      separatorH(2),
            memory_info,   separatorH(2),
            phase_info,    |filler(),
            shortcuts,
        }),
    });
}

// ─── 主界面佈局 ─────────────────────────────────────
int main() {
    AgentState state;
    std::string input = "";

    // 模擬的對話歷史
    auto conversation = vbox({
        text("You: 幫我寫一個 C++ 快速排序並編譯測試"),
        text(""),
        colored(Color::Cyan, text("[Planner] Generating task plan...")),
        colored(Color::Green, text("[Tool] write_file → OK")),
        colored(Color::Magenta, text("[Reflection] Step passed quality check.")),
        colored(Color::Yellow, text("[Building] g++ -o quicksort quicksort.cpp ...")),
    });

    // 輸入框
    auto input_box = Input(&input, &option);

    // 主佈局：標題 + 內容 + 狀態列
    auto root = vbox({
        // 標題列
        colored(Color::Bold | Color::Blue,
            text(" ZL Agent — Multi-Language AI Coding Agent ")
        ),
        separator(),

        // 主體區域（佔滿剩餘空間）
        |conversation,

        // 輸入行
        hflow({ text("> "), input_box }),

        // 狀態列（固定在底部）
        RenderStatusBar(state),
    });

    auto screen = Screen::Create(Dimension::Full());
    auto render = Render(screen);

    // ─── 模擬動態更新 ──────────────────────────────
    std::vector<std::function<void()>> updates = {
        [&]() { state.tokens_used = 1247; state.current_iteration = 1; state.current_phase = "Thinking"; },
        [&]() { state.tokens_used = 3500; state.current_iteration = 3; state.current_phase = "Executing"; },
        [&]() { state.tokens_used = 5800; state.current_iteration = 6; state.current_phase = "Reviewing"; },
        [&]() { state.tokens_used = 7200; state.current_iteration = 9; state.current_phase = "Executing"; },
        [&]() { state.tokens_used = 4100; state.current_iteration = 2; state.current_phase = "Idle";
                state.memory_count = 15; state.facts_count = 3; },
    };

    int update_idx = 0;

    // 每 2 秒更新一次狀態，模擬 Agent 運行過程
    auto timer = Task::Every(std::chrono::milliseconds(2000), [&]() {
        if (update_idx < updates.size()) {
            updates[update_idx++]();
        } else {
            update_idx = 0; // 循環演示
        }
    });

    // 渲染循環
    while (true) {
        render(root);
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60fps
    }

    return 0;
}
