// ─── FTXUI TUI Version of ZL Agent ──────────────────────────────────────
// 狀態列即時反映 Agent 運行狀態：連線/模型/Token/迭代次數/功能開關

#include <atomic>
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>

#include "llm_client.h"
#include "config.h"
#include "agent.h"
#include "memory.h"
#include "long_term_memory.h"
#include "rag_manager.h"

// ─── FTXUI Headers (header-only, no installation needed) ─────────────────
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
using namespace ftxui;

// ─── 狀態數據模型 ────────────────────────────────────────────────────────
struct AgentState {
    bool connected = true;
    std::string model_name = "qwen2.5-coder-7b";
    int tokens_used = 0, max_tokens = 8192;
    int current_iteration = 0, max_iterations = 10;
    bool task_planning = true, self_reflection = true, multi_agent = false;
    int memory_count = 0, facts_count = 0;
    std::string current_phase = "Idle"; // Idle / Thinking / Executing / Reviewing
};

// ─── 全局狀態實例 ────────────────────────────────────────────────────────
static AgentState g_state;

// ─── 顏色輔助函數 ────────────────────────────────────────────────────────
Color status_color(const std::string& phase) {
    if (phase == "Thinking")  return Color::Yellow | Color::Bold;
    if (phase == "Executing") return Color::Cyan | Color::Bold;
    if (phase == "Reviewing") return Color::Magenta | Color::Bold;
    return Color::Gray;
}

Color phase_color(const std::string& phase) {
    if (phase == "Thinking")  return Color::Yellow | Color::Bold;
    if (phase == "Executing") return Color::Cyan | Color::Bold;
    if (phase == "Reviewing") return Color::Magenta | Color::Bold;
    return Color::Gray;
}

// ─── 狀態列渲染函數 ──────────────────────────────────────────────────────
Element RenderStatusBar(const AgentState& state) {
    // Token 用量 — 比例變色
    double ratio = (double)state.tokens_used / state.max_tokens;
    Color token_color = ratio < 0.5 ? Color::Green
                       : ratio < 0.8 ? Color::Yellow
                                    : Color::Red;

    return vbox({
        separator(),  // 分隔線
        hflow({
            colored(Color::Green, text("🔗 Connected")),   separatorH(2),
            colored(Color::Blue | Color::Bold, text("🤖 " + state.model_name)),  separatorH(2),
            colored(token_color, text("🧠 " + std::to_string(state.tokens_used) + "/" + std::to_string(state.max_tokens))),  separatorH(2),
            colored(Color::Cyan, text("⚡ " + std::to_string(state.current_iteration) + "/" + std::to_string(state.max_iterations))),  separatorH(2),

            // 功能開關
            colored(Color::Green, text("✓ Plan")),         separatorH(1),
            colored(Color::Green, text("✓ Reflect")),      separatorH(1),
            colored(Color::Gray, text("✗ MultiAgent")),    separatorH(2),

            colored(Color::Magenta, text("💾 Msg:" + std::to_string(state.memory_count))),  separatorH(2),
            colored(phase_color(state.current_phase), text("▶ " + state.current_phase)),  separatorH(2),

            // 右側快捷鍵提示
            |filler(),
            right_aligned(colored(Color::Gray,
                hflow({ text("/help"), separatorH(1), text("/model"), separatorH(1), text("Ctrl+C Quit") })
            )),
        }),
    });
}

// ─── 對話歷史渲染器 ──────────────────────────────────────────────────────
class ConversationRenderer {
public:
    explicit ConversationRenderer(const std::string& system_prompt)
        : system_prompt_(system_prompt), scroll_offset_(0) {}

    Element render() {
        if (messages_.empty()) {
            return vbox({
                text("等待輸入..."),
                separator(),
                text("輸入 /help 查看命令列表，或 /model 切換模型。"),
            });
        }

        auto content = vbox();
        
        // 系統提示詞（灰色，頂部）
        if (!system_prompt_.empty()) {
            content += colored(Color::Gray, text("--- System Prompt ---"));
            content += separator();
            for (const auto& line : split(system_prompt_, "\n")) {
                content += text(line);
            }
            content += separator();
        }

        // 消息歷史
        for (const auto& msg : messages_) {
            if (msg.role == "user") {
                content += hflow({
                    colored(Color::White | Color::Bold, text("User:")),
                    separatorH(1),
                    text(msg.content),
                });
            } else if (msg.role == "assistant") {
                // 助手回應：思考內容用灰色，普通內容用白色
                auto lines = split(msg.content, "\n");
                for (const auto& line : lines) {
                    Color color = Color::Gray;
                    if (!line.empty() && line[0] != '\033') { // 非 ANSI 控制碼
                        color = Color::White;
                    }
                    content += hflow({
                        colored(Color::Yellow | Color::Bold, text("Agent:")),
                        separatorH(1),
                        colored(color, text(line)),
                    });
                }
            } else if (msg.role == "tool") {
                content += hflow({
                    colored(Color::Cyan | Color::Bold, text("Tool:")),
                    separatorH(1),
                    text(msg.content),
                });
            }
        }

        // 滾動邏輯：如果最後一個消息超過視窗，自動滾動
        auto scroll_to_bottom = [&]() {
            int estimated_lines = content.height();
            if (scroll_offset_ < estimated_lines - 3) {
                scroll_offset_ = estimated_lines - 3;
            }
        };

        // 添加輸入提示
        content += hflow({ text("> "), input_box });

        return vbox({
            separator(),
            content,
            RenderStatusBar(g_state),
        }, flex_direction::column);
    }

    void add_message(const std::string& role, const std::string& content) {
        messages_.push_back({role, content});
    }

    void clear() {
        messages_.clear();
    }

private:
    struct Message {
        std::string role; // "user", "assistant", "tool"
        std::string content;
    };

    std::vector<Message> messages_;
    std::string system_prompt_;
    int scroll_offset_ = 0;
};

// ─── 主佈局渲染器 ────────────────────────────────────────────────────────
Element RenderMain(const AgentState& state, ConversationRenderer& conv) {
    return vbox({
        // 標題
        colored(Color::Bold | Color::Blue, text(" ZL Agent - FTXUI TUI ")),
        separator(),
        
        // 對話歷史區域（佔滿剩餘空間）
        |conv.render(),
        
        // 狀態列（固定在底部）
        RenderStatusBar(state),
    }, flex_direction::column);
}

// ─── 初始化 Agent 和配置 ─────────────────────────────────────────────────
void init_agent(const std::string& llm_url, const std::string& model) {
    g_state.connected = true;
    g_state.model_name = model;
    g_state.max_iterations = 10;
    g_state.max_tokens = 8192;
    
    // 創建 Agent 實例
    agent::Agent ag(llm_url, model);
    
    // 設置功能開關（從配置讀取）
    ag.set_task_planning(true);
    ag.set_self_reflection(true);
    ag.set_multi_agent(false);
    ag.set_max_reflection_retries(2);
}

// ─── 處理用戶輸入 ────────────────────────────────────────────────────────
void process_input(const std::string& input, agent::Agent& ag, ConversationRenderer& conv) {
    // 清空輸入框顯示
    conv.clear();
    
    // 重置狀態
    g_state.current_phase = "Thinking";
    g_state.tokens_used = 0;
    g_state.current_iteration = 0;
    
    // 添加用戶消息到對話歷史
    conv.add_message("user", input);
    
    // 運行流式推理
    agent::ChatResponse usage_info{};
    ag.run_stream(input, [&](const std::string& token, bool is_reasoning_flag) {
        g_state.tokens_used += static_cast<int>(token.length());
        
        // 更新狀態列中的 Token 用量
        double ratio = (double)g_state.tokens_used / g_state.max_tokens;
        Color token_color = ratio < 0.5 ? Color::Green
                           : ratio < 0.8 ? Color::Yellow
                                       : Color::Red;
        
        // 添加助手回應到對話歷史
        conv.add_message("assistant", token);
        
        return true;
    }, &usage_info);
    
    g_state.current_phase = "Idle";
}

// ─── 主函數 ──────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
#ifdef _WIN32
    // Windows: Set console code pages to UTF-8
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);
#endif

    // 創建 FTXUI 屏幕
    auto screen = Component::MakeScreen();
    
    // 初始化 Agent
    agent::Agent ag("http://127.0.0.1:1234", "qwen2.5-coder-7b");
    init_agent("http://127.0.0.1:1234", "qwen2.5-coder-7b");
    
    // 創建對話歷史渲染器（空系統提示詞）
    ConversationRenderer conv("");
    
    // 創建輸入框組件
    auto input_box = InputBox::Create();
    auto input_component = Component::Wrap(input_box);
    
    // 主佈局
    auto root = vbox({
        colored(Color::Bold | Color::Blue, text(" ZL Agent - FTXUI TUI ")),
        separator(),
        |conv.render(),
        hflow({ text("> "), input_component }),
        RenderStatusBar(g_state),
    }, flex_direction::column);
    
    // 渲染循環
    while (screen->loop().WaitOrBreak()) {
        render(root, screen->body());
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
    }
    
    return 0;
}
