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
	agent::Config& cfg,
    agent::CommandDispatcher &dispatcher,
    agent::TerminalCommandDetector* terminal_detector,
    agent::Agent& ag, 
    std::unique_ptr<agent::LongTermMemory>& long_term_memory,
    std::string &response)
{

    if (input == "quit" || input == "exit" || input == "/quit" || input == "/exit") {
        // Save session to long-term memory before exiting.
        if (long_term_memory) {
            TUI::out("\nSaving session to long-term memory...\n");
            long_term_memory->save_session(ag.get_memory(), ag.get_llm());
        }
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
    setlocale(LC_ALL, "zh_TW.UTF-8");
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



    // Load configuration from zlagent.ini (falls back to defaults if not found).
    auto cfg = agent::Config::load("zlagent.ini");

    // Set log level early so all subsequent LOG_* calls respect it.
    agent::set_log_level(agent::parse_log_level(cfg.logging.level));

    TUI::out(u8"╭─────────────────────────────╮\n");
    TUI::out(u8"│  ZL Agent - Code Assistant  │\n");
    TUI::out(u8"╰─────────────────────────────╯\n");

    // Use CLI model override if provided; otherwise fall back to config.
    std::string effective_model = cli_model.empty() ? cfg.llm.model : cli_model;

    LOG_INFO("LLM", cfg.llm.url);
    LOG_DEBUG("Main", "Log level set to: " + agent::log_level_to_string(agent::parse_log_level(cfg.logging.level)));
    agent::Agent ag(cfg.llm.url, effective_model);
    set_global_agent(&ag);

    // === Safety setup ===
    if (!cfg.safety.path_whitelist.empty()) {
        agent::SafetyGuard::get_instance().set_path_whitelist(cfg.safety.path_whitelist);
        {
            std::string paths;
            for (size_t i = 0; i < cfg.safety.path_whitelist.size(); ++i) {
                if (i > 0) paths += ", ";
                paths += cfg.safety.path_whitelist[i];
            }
            LOG_INFO("Config", "Path whitelist enabled: " + paths);
        }
    } else {
        LOG_INFO("Config", "Path whitelist: disabled (no restriction)");
    }

    if (!cfg.safety.working_directory.empty()) {
        agent::SafetyGuard::get_instance().set_working_directory(cfg.safety.working_directory);
        LOG_INFO("Config", "Working directory set to: " + cfg.safety.working_directory);
    } else {
        // Default to current working directory
        std::string cwd = agent::SafetyGuard::normalize_path(std::filesystem::current_path().string());
        agent::SafetyGuard::get_instance().set_working_directory(cwd);
        LOG_INFO("Config", "Working directory defaulted to: " + cwd);
    }

    agent::SafetyGuard::get_instance().set_strict_mode(cfg.safety.strict_mode);
    LOG_INFO("Config", "Strict mode: " + std::string(cfg.safety.strict_mode ? "enabled (reject out-of-scope paths)" : "disabled (confirm out-of-scope paths)"));

    // Determine the effective language: auto-detect > config value.
    //std::string effective_language = cfg.agent_.language;
    //if (cfg.agent_.auto_detect_language) {
    //    std::string detected = agent::LanguageDetector::detect_directory(".");
    //    if (!detected.empty()) {
    //        effective_language = detected;
    //        LOG_INFO("Config", "Auto-detected language: " + detected + " (overriding config value '" + cfg.agent_.language + "')");
    //    }
    //}

    // System prompt: external file > built-in language-specific > multi-language default.
    std::string system_prompt;
    if (!cfg.agent_.prompt_file.empty()) {
        std::ifstream pf(cfg.agent_.prompt_file);
        if (pf.is_open()) {
            std::ostringstream oss;
            oss << pf.rdbuf();
            system_prompt = oss.str();
            LOG_INFO("Config", "System prompt loaded from: " + cfg.agent_.prompt_file);
        } else {
            LOG_WARN("Config", "Cannot open prompt file '" + cfg.agent_.prompt_file + "', using built-in.");
            system_prompt = agent::SystemPromptProvider::get();
        }
    } else {
        system_prompt = agent::SystemPromptProvider::get();
    }
    ag.set_system_prompt(system_prompt);

    // Apply feature toggles from config.
    ag.set_task_planning(cfg.features.task_planning);
    ag.set_self_reflection(cfg.features.self_reflection);
    ag.set_multi_agent(cfg.features.multi_agent);
    ag.set_max_iterations(cfg.agent_.max_iterations);
    ag.set_max_reflection_retries(cfg.features.max_reflection_retries);
    ag.set_local_tools_enabled(cfg.local_tools.enabled);

    // User Reply mode: allow user intervention during reasoning loop.
    ag.set_user_reply_mode(agent::parse_reply_mode(cfg.agent_.user_reply_mode));
    LOG_INFO("Config", "User reply mode: " + std::string(agent::reply_mode_to_string(ag.get_user_reply_mode())));

    // Register built-in tools
    LOG_INFO("Main", "Registering built-in tools...");
    //ag.add_tool(agent::create_read_file_tool());
    ag.add_tool(agent::create_read_files_tool());
    //ag.add_tool(agent::create_read_file_lines_tool());
    //ag.add_tool(agent::create_write_file_tool());
    ag.add_tool(agent::create_write_files_tool());
    ag.add_tool(agent::create_append_file_tool());
    ag.add_tool(agent::create_insert_file_content_tool());
    ag.add_tool(agent::create_edit_file_tool());
    //ag.add_tool(agent::create_edit_files_tool());
    ag.add_tool(agent::create_list_directory_tool());
    ag.add_tool(agent::create_terminal_tool());
    ag.add_tool(agent::create_code_search_tool());
    ag.add_tool(agent::create_create_directory_tool());
    ag.add_tool(agent::create_delete_path_tool());
    ag.add_tool(agent::create_delete_files_tool());
    ag.add_tool(agent::create_copy_path_tool());
    ag.add_tool(agent::create_move_path_tool());
    ag.add_tool(agent::create_find_files_tool());
    ag.add_tool(agent::create_get_file_outline_tool());
    ag.add_tool(agent::create_grep_with_context_tool());
    ag.add_tool(agent::create_run_build_tool());
    // Batch file tools (read/delete multiple files)
    ag.add_tool(agent::create_delete_files_tool());
    //ag.add_tool(agent::create_git_status_tool());
    //ag.add_tool(agent::create_git_diff_tool());
    ag.add_tool(agent::create_fetch_url_tool());
    ag.add_tool(agent::create_project_overview_tool());

    // === Skill System ===
    agent::SkillRegistry skill_registry;
    agent::set_global_skill_registry(&skill_registry);

    LOG_INFO("Main", "\nLoading skills...");

    // 1. Load native skills from zlagent/skills/.
    auto native_skills = agent::SkillLoader::scan_directory("zlagent/skills", "native");
    for (auto& skill : native_skills) {
        skill_registry.register_skill(skill);
        LOG_INFO("Skill", u8"  \u2713 " + skill->name + " (" + skill->source_path + ")");
    }

    // 2. Auto-detect and import cross-agent skills.
    {
        std::map<std::string, agent::SkillPtr> existing;
        for (const auto& s : skill_registry.get_skills()) existing[s->name] = s;
        auto imported_skills = agent::SkillLoader::auto_detect_and_import(".", existing);
        for (auto& skill : imported_skills) {
            skill_registry.register_skill(skill);
            LOG_INFO("Skill", "  + " + skill->name + " [imported from " + skill->source_path + "]");
        }
    }

    // 2.5 Validate skill dependencies against available tools.
    {
        std::vector<std::string> tool_names = ag.get_tool_names();
        for (auto& skill : skill_registry.get_skills()) {
            agent::SkillLoader::validate_dependencies(skill, tool_names);
        }
    }

    // 3. Inject skill summary into system prompt so the LLM knows available skills.
    {
        std::string skill_summary = skill_registry.build_skill_summary();
        if (!skill_summary.empty()) {
            system_prompt += "\n\n" + skill_summary;
            ag.set_system_prompt(system_prompt);
        }
    }

    // 4. Register create_skill and delete_skill tools.
    ag.add_tool(agent::create_create_skill_tool());
    ag.add_tool(agent::create_delete_skill_tool());
    ag.add_tool(agent::create_reload_skills_tool());

    // Log summary.
    int enabled_count = 0, disabled_count = 0;
    for (const auto& skill : skill_registry.get_skills()) {
        if (skill->enabled) ++enabled_count; else ++disabled_count;
    }
    LOG_INFO("Main", std::to_string(enabled_count) + " skills loaded" + (disabled_count > 0 ? ", " + std::to_string(disabled_count) + " disabled" : "") + ".");

    // Load external plugins from configured directory.
    LOG_INFO("Main", "\nLoading external plugins...");
    agent::PluginLoader loader;
    auto plugins = loader.load_plugins(cfg.plugins.directory);
    for (auto& plugin : plugins) {
        ag.add_tool(std::move(plugin));
    }

    // Local tools are discovered lazily on first chat — no startup delay.

    // === RAG System ===
    if (cfg.rag.enabled) {
        LOG_INFO("RAG", "\nInitializing RAG system...");

        agent::EmbeddingProvider* provider = nullptr;
        if (cfg.rag.embedding_backend == "lm_studio") {
            provider = new agent::LLMEmbeddingProvider(cfg.llm.url, cfg.rag.embedding_model);
            LOG_INFO("RAG", "  Embedding backend: LM Studio (" + cfg.rag.embedding_model + ")");
        } else {
            provider = new agent::TfidfEmbeddingProvider();
            LOG_INFO("RAG", "  Embedding backend: TF-IDF (local)");
        }

        // Build RAG config.
        agent::RAGManager::Config rag_cfg;
        rag_cfg.top_k = cfg.rag.top_k;
        rag_cfg.min_score = cfg.rag.min_score;
        rag_cfg.store_path = cfg.rag.store_path;

        std::unique_ptr<agent::RAGManager> rag_manager;

        // Create RAG manager.
        rag_manager = std::make_unique<agent::RAGManager>(provider, rag_cfg);

        // Load existing store if available.
        if (!cfg.rag.store_path.empty() && std::filesystem::exists(cfg.rag.store_path)) {
            LOG_INFO("RAG", "  Loading existing knowledge base from: " + cfg.rag.store_path);
            rag_manager->load_store(cfg.rag.store_path);
        }

        // Ingest knowledge directories at startup.
        for (const auto& dir : cfg.rag.knowledge_dirs) {
            LOG_INFO("RAG", "  Ingesting: " + dir);
            rag_manager->add_directory(dir);
        }

        // Save store if persistence is configured.
        if (!cfg.rag.store_path.empty()) {
            rag_manager->save(cfg.rag.store_path);
            LOG_INFO("RAG", "  Knowledge base saved to: " + cfg.rag.store_path);
        }

        set_global_rag_manager(rag_manager.get());
        ag.add_tool(agent::create_search_knowledge_base_tool());

        LOG_INFO("RAG", "  Total chunks indexed: " + std::to_string(rag_manager->total_chunks()));
    }

    // === Long-Term Memory ===
    std::unique_ptr<agent::LongTermMemory> long_term_memory;
    if (cfg.memory.long_term_enabled) {
        LOG_INFO("Memory", "\nInitializing long-term memory...");

        agent::LongTermMemory::Config ltm_cfg;
        ltm_cfg.store_dir = cfg.memory.store_dir;
        ltm_cfg.max_sessions = cfg.memory.max_sessions;
        ltm_cfg.inject_facts_to_prompt = cfg.memory.inject_facts_to_prompt;
        ltm_cfg.auto_extract_facts = cfg.memory.auto_extract_facts;

        long_term_memory = std::make_unique<agent::LongTermMemory>(ltm_cfg);

        // Load from disk.
        if (long_term_memory->load()) {
            LOG_INFO("Memory", "  Loaded: " + std::to_string(long_term_memory->get_recent_sessions(10).size()) + " sessions, " + std::to_string(long_term_memory->get_facts().size()) + " facts");
        } else {
            LOG_INFO("Memory", "  No existing memory found (starting fresh)");
        }

        // Inject facts into system prompt.
        if (ltm_cfg.inject_facts_to_prompt) {
            std::string context = long_term_memory->build_context_string(5);
            if (!context.empty()) {
                system_prompt += "\n\n" + context;
                ag.set_system_prompt(system_prompt);
                LOG_INFO("Memory", "  Injected semantic facts into system prompt");
            }
        }

        // Integrate with RAG if available.
        if (agent::get_global_rag_manager()) {
            long_term_memory->integrate_with_rag(agent::get_global_rag_manager());
            LOG_INFO("Memory", "  Session summaries injected into RAG knowledge base");
        }

        set_global_long_term_memory(long_term_memory.get());
        ag.add_tool(agent::create_search_memories_tool());
        ag.add_tool(agent::create_recall_facts_tool());
    }

    // === CLI Command Dispatcher ===
    agent::CommandDispatcher dispatcher;
    register_command_handlers(
        dispatcher,
        &ag,
        agent::get_global_skill_registry(),
        agent::get_global_rag_manager(),
        agent::get_global_long_term_memory());

    // Register the /reply-mode command for user intervention control.
    register_reply_mode_command(dispatcher, &ag);

    // Terminal command detector — intercept shell commands before LLM.
    agent::TerminalCommandDetector* terminal_detector = nullptr;
    if (cfg.terminal_commands.enabled) {
        terminal_detector = agent::TerminalCommandDetector::create(
            cfg.terminal_commands.direct_commands,
            cfg.terminal_commands.confirm_commands);
    }

    // ── Telegram Bot ───────────────────────────────────────
    std::unique_ptr<agent::TelegramClient> telegram_client;
    if (cfg.telegram.enabled && !cfg.telegram.bot_token.empty()) {
        agent::TelegramClient::Config tg_cfg;
        tg_cfg.enabled            = true;
        tg_cfg.bot_token          = cfg.telegram.bot_token;
        tg_cfg.poll_timeout_sec   = cfg.telegram.poll_timeout_sec;
        tg_cfg.max_updates_per_poll = cfg.telegram.max_updates_per_poll;
        tg_cfg.allowed_chat_ids   = cfg.telegram.allowed_chat_ids;

        telegram_client = std::make_unique<agent::TelegramClient>(tg_cfg);

        // Register incoming message handler via event broker.
        agent::on_event("telegram.incoming", [&](const std::string& payload_json) {
            try {
                auto j = nlohmann::json::parse(payload_json);
                int64_t chat_id   = j.value("chat_id", 0LL);
                std::string text  = j.value("text", "");

                if (text.empty()) return;

                LOG_INFO("Telegram", "Processing message from chat " + std::to_string(chat_id));

				std::string response;
				run_interactive(text, cfg, dispatcher, terminal_detector, ag, long_term_memory, response);

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
    } else if (cfg.telegram.enabled) {
        LOG_WARN("Telegram", "Telegram is enabled but bot_token is empty — skipping.");
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
        running = run_interactive(input, cfg, dispatcher, terminal_detector, ag, long_term_memory, response);
    }
    agent::KeyWatcher::stop();

    // === Cleanup on exit ===
    if (long_term_memory) {
        long_term_memory->save();
        LOG_INFO("Memory", "Long-term memory saved.");
    }
    if (agent::get_global_rag_manager() && !cfg.rag.store_path.empty()) {
        agent::get_global_rag_manager()->save(cfg.rag.store_path);
        LOG_INFO("RAG", "Knowledge base saved.");
    }

    return 0;
}