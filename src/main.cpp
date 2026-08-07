#include "pch.h"

#include <atomic>
#include "key_watcher.h"
#include "llm_client.h"
#include "logger.h"
#include "config.h"
#include "safety_guard.h"
#include "language_detector.h"
#include "system_prompt.h"
#include "skill_system.h"
#include "rag_manager.h"
#include "embedding_provider.h"
#include "long_term_memory.h"
#include "command_dispatcher.h"
#include "command_handlers.h"
#include "terminal_command_detector.h"
#include "agent.h"
#include "tools.h"
#include "plugin_loader.h"
#include "local_tools.h"

#include "tui.h"
#include "user_reply.h"
#include "event.h"
#include "telegram_client.h"

// ── Input history ─────────
static const int MAX_HISTORY = 50;

// ── Status bar renderer (pure std::cout + ANSI via TUI) ───
static void print_status_bar(const agent::Agent& ag, const std::unique_ptr<agent::LongTermMemory>& ltm) {

    auto memory_count = static_cast<int>(ag.get_memory().get_messages().size());
	size_t facts_count = 0;
    if (ltm) {
        facts_count = ltm->get_facts().size();
    }
	auto current_iteration = ag.get_current_iteration();
	auto max_iterations = ag.get_max_iterations();
    auto task_planning = ag.task_planning_enabled();
    auto self_reflection = ag.self_reflection_enabled();
    auto multi_agent = ag.multi_agent_enabled();
    auto user_reply_mode = ag.get_user_reply_mode();
	auto tokens_used = ag.get_tokens_used();
	auto max_tokens = ag.get_max_token();

    // SafetyGuard state
    auto& sg = agent::SafetyGuard::get_instance();
    auto strict_mode = sg.get_strict_mode();
    auto whitelist_count = static_cast<int>(sg.path_whitelist_.size());

    // Token ratio color: <50% green → <80% yellow → ≥80% red
    double token_ratio = (max_tokens > 0) ? (double)tokens_used / max_tokens : 0;
    AnsiColor token_fg = token_ratio < 0.5 ? AnsiColor::Green : token_ratio < 0.8 ? AnsiColor::Yellow : AnsiColor::Red;

    // Iteration ratio color: ≥80% red, else cyan
    double iter_ratio = (max_iterations > 0) ? (double)current_iteration / max_iterations : 0;
    AnsiColor iter_fg = iter_ratio >= 0.8 ? AnsiColor::Red : AnsiColor::Cyan;

    // Build the bar content (single line)
    std::ostringstream bar;
    bar << u8"\n";
    //bar << TUI::color(u8"🤖 " + s.model_name, AnsiColor::Blue, true) << u8" │ ";
    //bar << TUI::color(u8"💸 " + std::to_string(tokens_used) + "/" + std::to_string(max_tokens), token_fg) << u8" │ ";
    bar << TUI::color(u8"💸 " + std::to_string(tokens_used), token_fg) << u8" │ ";
    bar << TUI::color(u8"🔁 " + std::to_string(current_iteration) + "/" + std::to_string(max_iterations), iter_fg) << u8" │ ";
    bar << TUI::check(u8"Plan", task_planning) << " ";
    bar << TUI::check(u8"Reflect", self_reflection) << " ";
    bar << TUI::check(u8"MultiAgent", multi_agent) << u8" │ ";

    // User reply mode display
    const char* reply_mode_icon = "";
    AnsiColor reply_fg = AnsiColor::BrightBlack;
    switch (user_reply_mode) {
        case agent::UserReplyMode::Off:     reply_mode_icon = u8"❌ off"; break;
        case agent::UserReplyMode::Exec:    reply_mode_icon = u8"🔧 exec"; reply_fg = AnsiColor::Yellow; break;
        case agent::UserReplyMode::Edit:    reply_mode_icon = u8"✏️ edit"; reply_fg = AnsiColor::Cyan; break;
        case agent::UserReplyMode::Always:  reply_mode_icon = u8"🔄 always"; reply_fg = AnsiColor::Magenta; break;
    }
    bar << u8"⛔: " << TUI::color(reply_mode_icon, reply_fg) << u8" │ ";

    bar << TUI::color(u8"💾 Msg:" + std::to_string(memory_count) + " Fact:" + std::to_string(facts_count), AnsiColor::Magenta);

    // SafetyGuard status
    const char* mode_icon = strict_mode ? u8"🔒" : u8"🔓";
    AnsiColor mode_fg = strict_mode ? AnsiColor::Red : AnsiColor::Green;
    bar << u8" │ " << TUI::color(mode_icon, mode_fg) << u8" 📄:" << std::to_string(whitelist_count);

    bar << u8"\n";

    std::cout << bar.str();
}

bool run_interactive(
    const std::string &input,
    agent::CommandDispatcher &dispatcher,
    agent::TerminalCommandDetector* terminal_detector,
    agent::Agent& ag, 
    std::string &response)
{

    if (input == "quit" || input == "exit" || input == "/quit" || input == "/exit") {
        // Save session to long-term memory before exiting.
        TUI::out(u8"\nGoodbye!\n");
        return false;
    }

    // Dispatch slash-commands before sending to LLM.
    if (dispatcher.dispatch(input, response)) 
        return true;

    // Detect and execute terminal commands directly, bypassing the LLM.
    if (terminal_detector) {
        if (terminal_detector->detect_and_execute(input, response))
            return true;
    }

    // Safety: input filter - detect prompt injection attempts.
    auto& cfg = ag.get_config();
    if (cfg.safety.input_filter && agent::SafetyGuard::is_prompt_injection(input)) {
        LOG_WARN("Safety", "Possible prompt injection detected. Input rejected.");
        return true;
    }

    // --- Waiting spinner animation (rotating circle, single-threaded) ---
    const char* spinners = u8"\u2809\u281B\u281E\u2817\u2814\u281A\u281C\u2808";  // ⠋⠙⠹⠸⠼⠴⠦⠧
    //const char* spinners = u8"⠋⠙⠹⠸⠼⠴⠦⠧";  // ⠋⠙⠹⠸⠼⠴⠦⠧
    const int spinner_len = 8;

    TUI::out("\nAgent: ");
    //for (int i = 0; i < 3; ++i) {  // spin a few times while waiting for first token
    //    if (i > 0)
    //        std::cout << "\b";    // each Braille char is 1 display cell, so just \b once
    //    std::cout << spinners[i % spinner_len] << std::flush;
    //    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    //}
    //// Erase the last spinner character (1 display cell)
    //std::cout << " \b";

    // Capture token usage from the LLM response
    agent::ChatResponse usage_info{};

    bool in_reasoning = false;
    response = ag.run_stream(input, [&](const std::string& token, bool is_reasoning_flag) {
        // First reasoning token: show thinking indicator (dim)
        if (is_reasoning_flag && !in_reasoning) {
            in_reasoning = true;
            TUI::printDim(u8"[🤔 thinking]");
        }
        // Transition from reasoning to content: restore normal brightness
        else if (!is_reasoning_flag && in_reasoning) {
            in_reasoning = false;
            TUI::reset();
        }

        TUI::out("%s", token.c_str());
        TUI::flush();
        return true;  // keep streaming
        }, &usage_info);

    // Ensure terminal is back to normal even if reasoning was the last output.
    if (in_reasoning) {
        in_reasoning = false;
        TUI::reset();
    }

    // Display token usage if available
    if (usage_info.total_tokens() > 0) {
        TUI::out(u8"\n\n⏱  Tokens: ");
        TUI::out("prompt=%d", usage_info.prompt_tokens);
        TUI::out(", completion=%d", usage_info.completion_tokens);
        if (usage_info.max_tokens > 0)
            TUI::out("/%d", usage_info.max_tokens);
        TUI::out(", total=%d\n", usage_info.total_tokens());
    }
    return true;
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // Set C runtime locale so std::cout handles multibyte (UTF-8) characters correctly.
    // setlocale(LC_ALL, "zh_TW.UTF-8");
    // Set console input/output code pages to UTF-8 so emoji and all Unicode display correctly.
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);
#endif
	bool cli_mode = false;
    std::string cli_model;
    std::string cli_prompt;

    // Parse CLI arguments: -m <model>  -p <prompt>
    {
        for (int i = 1; i < argc; ++i) {
            std::string arg(argv[i]);
            if (arg == "-h" || arg == "--help") {
                TUI::out("Usage: zlagent [options]\n"
                         "\nOptions:\n"
                         "  -m <model>    Override LLM model name (does not write to config)\n"
                         "  -p <prompt>   Run a single prompt and exit\n"
                         "  -h, --help    Show this help message\n");
                return 0;
            } else if (arg == "-m" && i + 1 < argc) {
                cli_model = argv[++i];
                cli_mode = true;
            } else if (arg == "-p" && i + 1 < argc) {
                cli_prompt = argv[++i];
                cli_mode = true;
            }
        }
    }

    TUI::out(u8"╭─────────────────────────────╮\n");
    TUI::out(u8"│  ZL Agent - Code Assistant  │\n");
    TUI::out(u8"╰─────────────────────────────╯\n");

    agent::Agent ag;
    set_global_agent(&ag);
    ag.load_config();

    LOG_DEBUG("Build:", __DATE__ " "  __TIME__);

    if (!cli_model.empty()) {
        ag.set_llm_model(cli_model);
    }

    ag.register_tools();
    ag.register_skills();
    ag.load_plugins();

    // Local tools are discovered lazily on first chat — no startup delay.

    auto& long_term_memory = ag.get_long_term_memory();
    auto& cfg = ag.get_config();
    auto& dispatcher = ag.get_dispatcher();
    auto terminal_detector = ag.get_terminal_detector();

    // ── Telegram Bot ───────────────────────────────────────
    std::unique_ptr<agent::TelegramClient>& telegram_client = ag.get_telegram_client();
    if (telegram_client) {
        // Register incoming message handler via event broker.
        agent::on_event("telegram.incoming", [&](const std::string& payload_json) {
            try {
                auto j = nlohmann::json::parse(payload_json);
                int64_t chat_id   = j.value("chat_id", 0LL);
                std::string text  = j.value("text", "");

                if (text.empty()) return;

                LOG_INFO("Telegram", "Processing message from chat " + std::to_string(chat_id));

				std::string response;
				run_interactive(text, dispatcher, terminal_detector, ag, response);

                // Send the Agent's reply back to Telegram.
                if (!response.empty()) {
                    telegram_client->send_message(chat_id, response);
                    LOG_INFO("Telegram", "Reply sent to chat " + std::to_string(chat_id));
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Telegram", "Error processing incoming message: " + std::string(e.what()));
            }
        });

        telegram_client->start();
        TUI::out(u8"\n🤖 Telegram bot connected. Listening for messages...\n");
    }


    // Print initial status bar
    TUI::out(u8"\nReady. Type your request (or '/help' '/h' for commands):\n");
    if (cfg.terminal_commands.enabled) {
        TUI::out(u8"  💡 Shell commands are auto-detected and executed directly.\n");
    }

    // If -p was provided, use it as the single prompt instead of reading interactively.
    std::string cli_input = cli_prompt;

    agent::KeyWatcher::start();
    // Interactive loop with streaming output.
	bool running = true;
    while (running) {
        print_status_bar(ag, long_term_memory);
        TUI::out("\n");
        
        std::string input;
        if (!cli_input.empty()) {
            input = cli_input;
            cli_input.clear();  // consume once
            running = false;    // Exit after one interaction in CLI mode
        }
        else {
            agent::KeyWatcher::init_keyboard();
            std::string prompt = "You:[" + ag.get_llm().get_model() + "]>";
            input = agent::KeyWatcher::readline(prompt.c_str(), [&](const agent::Key& k) {
                if (k == agent::Key::K_CTRL_C) {
                    running = false;
                    std::cout << std::endl;  // ensure newline after Ctrl-C
                }
                });
            // Stop background Ctrl-C watcher.
            agent::KeyWatcher::close_keyboard();

			std::cout << std::endl;  // ensure newline after input
        }
        if (input.empty()) {
            continue;  // just an empty Enter, stay in loop
        }

			std::string response;
        running = run_interactive(input, dispatcher, terminal_detector, ag, response);
    }
    agent::KeyWatcher::stop();

    // === Cleanup on exit ===
    ag.save_session();
    if (ag.get_rag_manager() && !cfg.rag.store_path.empty()) {
        ag.get_rag_manager()->save(cfg.rag.store_path);
        LOG_INFO("RAG", "Knowledge base saved.");
    }

    return 0;
}