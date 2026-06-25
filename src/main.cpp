#include "pch.h"

#include <atomic>
#include "llm_client.h"
#include "logger.h"
#include "config.h"
#include "safety_guard.h"
#include "language_detector.h"
#include "system_prompt.h"
#include "skill_system.h"
#include "rag_manager.h"
#include "encoding.h"
#include "embedding_provider.h"
#include "long_term_memory.h"
#include "command_dispatcher.h"
#include "command_handlers.h"
#include "terminal_command_detector.h"
#include "agent.h"
#include "tools.h"
#include "plugin_loader.h"
#include "local_tools.h"
#include <isocline.h>

// ── Input history (managed by isocline) ─────────
static const int MAX_HISTORY = 50;

// ── ANSI color helpers ───────────────────────────
static std::string ansi_color(int fg, bool bold = false) {
    return "\033[" + std::to_string(bold ? 1 : 0) + ";" + std::to_string(30 + fg) + "m";
}
static const char* ANSI_RESET   = "\033[0m";
// Black=0, Red=1, Green=2, Yellow=3, Blue=4, Magenta=5, Cyan=6, White=7

// ── Agent runtime state (mirrors ftxui.md spec) ───
struct AgentState {
    bool connected = true;
    std::string model_name;
    int tokens_used = 0;
    int max_tokens = 8192;
    int current_iteration = 0;
    int max_iterations = 10;
    bool task_planning = false;
    bool self_reflection = false;
    bool multi_agent = false;
    int memory_count = 0;
    int facts_count = 0;
    std::string current_phase = "Idle"; // Idle / Thinking / Executing / Reviewing
};
static AgentState g_state;

// ── Helper to update TUI state from agent ────────────────────────
static void update_tui_state(AgentState& s, const agent::Agent& ag,
                             const std::unique_ptr<agent::LongTermMemory>& ltm) {
    s.memory_count = static_cast<int>(ag.get_memory().get_messages().size());
    if (ltm) {
        auto facts = ltm->get_facts();
        s.facts_count = static_cast<int>(facts.size());
    }
}

// ── Status bar renderer (pure std::cout + ANSI) ───
static void print_status_bar(const AgentState& s, bool newline_after = true) {
    // Token ratio color: <50% green → <80% yellow → ≥80% red
    double token_ratio = (s.max_tokens > 0) ? (double)s.tokens_used / s.max_tokens : 0;
    int token_fg = token_ratio < 0.5 ? 2 : token_ratio < 0.8 ? 3 : 1; // green/yellow/red

    // Iteration ratio color: ≥80% red, else cyan
    double iter_ratio = (s.max_iterations > 0) ? (double)s.current_iteration / s.max_iterations : 0;
    int iter_fg = iter_ratio >= 0.8 ? 1 : 6; // red or cyan

    // Phase color + bold
    struct PhaseInfo { const char* name; int fg; };
    auto phase_info = [&]() -> PhaseInfo {
        if (s.current_phase == "Thinking")  return {"Thinking", 3};
        if (s.current_phase == "Executing") return {"Executing", 6};
        if (s.current_phase == "Reviewing") return {"Reviewing", 5};
        return {"Idle", 8}; // gray
    }();

    // Feature toggles: enabled=green check, disabled=gray cross
    auto feat = [](const char* label, bool on) -> std::string {
        return on ? ansi_color(2) + u8"✓ " + label : ansi_color(8) + u8"✗ " + label;
    };

    // Build the bar content (single line)
    std::ostringstream bar;
    bar << u8"├─";
    bar << ansi_color(4, true) << u8"🤖 " << s.model_name << ANSI_RESET << u8" │ ";
    bar << ansi_color(token_fg) << u8"🧠 " << s.tokens_used << "/" << s.max_tokens << ANSI_RESET << u8" │ ";
    bar << ansi_color(iter_fg) << u8"⚡ " << s.current_iteration << "/" << s.max_iterations << ANSI_RESET << u8" │ ";
    bar << feat("Plan", s.task_planning) << ANSI_RESET;
    bar << feat("Reflect", s.self_reflection) << ANSI_RESET;
    bar << feat("MultiAgent", s.multi_agent) << ANSI_RESET << u8" │ ";
    bar << ansi_color(5) << u8"💾 Msg:" << s.memory_count << " Fact:" << s.facts_count << ANSI_RESET << u8" │ ";
    bar << ansi_color(phase_info.fg, true) << u8"▶ " << phase_info.name << ANSI_RESET;
    bar << u8" ─┤";

    std::cout << bar.str();
    if (newline_after) std::cout << std::endl;
}

// ── Print status bar inline (no newline, for dynamic updates) ────────────
static void print_status_bar_inline(const AgentState& s) {
    // Move cursor to bottom of screen and clear line
    std::cout << "\033[1;H" << "\033[K";
    print_status_bar(s, false);
    std::cout << std::flush;
}

int main() {
#ifdef _WIN32
    // Set C runtime locale so std::cout handles multibyte (UTF-8) characters correctly.
    setlocale(LC_ALL, "zh_TW.UTF-8");
    // Set console input/output code pages to UTF-8 so emoji and all Unicode display correctly.
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);
#endif

    // Initialize isocline for rich console input (handles terminal setup on all platforms)
    ic_init(false);
    ic_set_history(nullptr, MAX_HISTORY);
    // Load configuration from zlagent.ini (falls back to defaults if not found).
    auto cfg = agent::Config::load("zlagent.ini");

    // Set log level early so all subsequent LOG_* calls respect it.
    agent::set_log_level(agent::parse_log_level(cfg.logging.level));

    std::cout << u8"╭─────────────────────────────╮" << std::endl;
    std::cout << u8"│  ZL Agent - Code Assistant  │" << std::endl;
    std::cout << u8"╰─────────────────────────────╯" << std::endl;
    std::cout << std::endl;

    LOG_INFO("LLM", cfg.llm.url);
    LOG_DEBUG("Main", "Log level set to: " + agent::log_level_to_string(agent::parse_log_level(cfg.logging.level)));
    agent::Agent ag(cfg.llm.url, cfg.llm.model);

    // === Safety setup ===
    if (!cfg.safety.path_whitelist.empty()) {
        agent::SafetyGuard::set_path_whitelist(cfg.safety.path_whitelist);
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

    // Determine the effective language: auto-detect > config value.
    std::string effective_language = cfg.agent_.language;
    if (cfg.agent_.auto_detect_language) {
        std::string detected = agent::LanguageDetector::detect_directory(".");
        if (!detected.empty()) {
            effective_language = detected;
            LOG_INFO("Config", "Auto-detected language: " + detected + " (overriding config value '" + cfg.agent_.language + "')");
        }
    }

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
            system_prompt = agent::SystemPromptProvider::get(effective_language);
        }
    } else {
        system_prompt = agent::SystemPromptProvider::get(effective_language);
    }
    ag.set_system_prompt(system_prompt);

    // Apply feature toggles from config.
    ag.set_task_planning(cfg.features.task_planning);
    ag.set_self_reflection(cfg.features.self_reflection);
    ag.set_multi_agent(cfg.features.multi_agent);
    ag.set_max_reflection_retries(cfg.features.max_reflection_retries);

    // ── Initialize TUI state (mirrors ftxui.md spec) ─────────
    g_state.model_name       = cfg.llm.model;
    g_state.max_tokens       = cfg.llm.max_tokens > 0 ? cfg.llm.max_tokens : 8192;
    g_state.max_iterations   = cfg.agent_.max_iterations;
    g_state.task_planning    = ag.task_planning_enabled();
    g_state.self_reflection  = ag.self_reflection_enabled();
    g_state.multi_agent      = ag.multi_agent_enabled();
    ag.set_local_tools_enabled(cfg.local_tools.enabled);

    // User Reply mode: allow user intervention during reasoning loop.
    ag.set_user_reply_mode(agent::parse_reply_mode(cfg.agent_.user_reply_mode));
    LOG_INFO("Config", "User reply mode: " + std::string(agent::reply_mode_to_string(ag.get_user_reply_mode())));

    // Register built-in tools
    LOG_INFO("Main", "Registering built-in tools...");
    ag.add_tool(agent::create_read_file_tool());
    ag.add_tool(agent::create_read_file_lines_tool());
    ag.add_tool(agent::create_write_file_tool());
    ag.add_tool(agent::create_edit_file_tool());
    ag.add_tool(agent::create_list_directory_tool());
    ag.add_tool(agent::create_terminal_tool());
    ag.add_tool(agent::create_code_search_tool());
    ag.add_tool(agent::create_create_directory_tool());
    ag.add_tool(agent::create_delete_path_tool());
    ag.add_tool(agent::create_copy_path_tool());
    ag.add_tool(agent::create_move_path_tool());
    ag.add_tool(agent::create_find_files_tool());
    ag.add_tool(agent::create_get_file_outline_tool());
    ag.add_tool(agent::create_grep_with_context_tool());
    ag.add_tool(agent::create_run_build_tool());
    ag.add_tool(agent::create_git_status_tool());
    ag.add_tool(agent::create_git_diff_tool());
    ag.add_tool(agent::create_fetch_url_tool());

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

    // Update memory stats before first prompt
    g_state.memory_count = static_cast<int>(ag.get_memory().get_messages().size());
    if (long_term_memory) {
        auto facts = long_term_memory->get_facts();
        g_state.facts_count = static_cast<int>(facts.size());
    }

    // Print initial status bar
    print_status_bar(g_state);
    std::cout << u8"\nReady. Type your request (or '/help' for commands):" << std::endl;
    if (cfg.terminal_commands.enabled) {
        std::cout << u8"  💡 Shell commands are auto-detected and executed directly." << std::endl;
    }

    // Interactive loop with streaming output.
    while (true) {
        char* raw = ic_readline(("You: (" + ag.get_llm().get_model() + ")").c_str());
        if (!raw) break;  // Ctrl-C / Ctrl-D
        std::string input(raw);
        ic_free(raw);
        if (input.empty()) continue;

        if (input == "quit" || input == "exit" || input == "/quit" || input == "/exit") {
            // Save session to long-term memory before exiting.
            if (long_term_memory) {
                std::cout << "\nSaving session to long-term memory..." << std::endl;
                long_term_memory->save_session(ag.get_memory(), ag.get_llm());
            }
            g_state.current_phase = "Idle";
            print_status_bar(g_state);
            std::cout << u8"\nGoodbye!" << std::endl;
            break;
        }

        if (input.empty()) continue;

        // Dispatch slash-commands before sending to LLM.
        if (dispatcher.dispatch(input)) continue;

        // Detect and execute terminal commands directly, bypassing the LLM.
        if (terminal_detector) {
            switch (terminal_detector->detect(input)) {
                case agent::CommandConfidence::High:
                    // High confidence — execute immediately.
                    agent::TerminalCommandDetector::execute_directly(input);
                    continue;

                case agent::CommandConfidence::Low:
                    // Low confidence — ask user to confirm.
                    std::cout << u8"\u26A0 Detected possible terminal command: "
                              << input << "\n"
                              << u8"   Execute directly? [y/N]: ";
                    {
                        std::string resp;
                        if (std::getline(std::cin, resp)) {
                            auto trim = [](std::string& s) {
                                s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](char c){ return !std::isspace(c); }));
                                s.erase(std::find_if(s.rbegin(), s.rend(), [](char c){ return !std::isspace(c); }).base(), s.end());
                            };
                            trim(resp);
                            std::string lower = resp;
                            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                            if (lower == "y" || lower == "yes") {
                                agent::TerminalCommandDetector::execute_directly(input);
                                continue;
                            }
                        }
                    }
                    // User declined — fall through to LLM.
                    break;

                case agent::CommandConfidence::NotACommand:
                    break;  // Not a command, send to LLM as usual.
            }
        }

        // Safety: input filter - detect prompt injection attempts.
        if (cfg.safety.input_filter && agent::SafetyGuard::is_prompt_injection(input)) {
            LOG_WARN("Safety", "Possible prompt injection detected. Input rejected.");
            continue;
        }

        // --- Waiting spinner animation (rotating circle, single-threaded) ---
        const char* spinners = u8"\u2809\u281B\u281E\u2817\u2814\u281A\u281C\u2808";  // ⠋⠙⠹⠸⠼⠴⠦⠧
        //const char* spinners = u8"⠋⠙⠹⠸⠼⠴⠦⠧";  // ⠋⠙⠹⠸⠼⠴⠦⠧
        const int spinner_len = 8;

        std::cout << "\nAgent: ";
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
        ag.run_stream(input, [&](const std::string& token, bool is_reasoning_flag) {
            // Update TUI state: phase + tokens
            g_state.tokens_used += static_cast<int>(token.length());
            if (is_reasoning_flag)
                g_state.current_phase = "Thinking";
            else
                g_state.current_phase = "Executing";

            // First reasoning token: show thinking indicator (dim)
            if (is_reasoning_flag && !in_reasoning) {
                in_reasoning = true;
                std::cout << "\033[2m";  // dim for thinking content
                std::cout << u8"\n[🤔 thinking]" << std::endl;
            }
            // Transition from reasoning to content: restore normal brightness
            else if (!is_reasoning_flag && in_reasoning) {
                in_reasoning = false;
                std::cout << "\033[0m";   // reset (end dim)
            }

            std::cout << token << std::flush;
            return true;  // keep streaming
        }, &usage_info);

        // Ensure terminal is back to normal even if reasoning was the last output.
        if (in_reasoning) {
            in_reasoning = false;
            std::cout << "\033[0m";
        }

        // Update TUI state: memory stats + reset phase
        g_state.memory_count = static_cast<int>(ag.get_memory().get_messages().size());
        if (long_term_memory) {
            auto facts = long_term_memory->get_facts();
            g_state.facts_count = static_cast<int>(facts.size());
        }
        g_state.current_phase = "Idle";

        // Display token usage if available
        if (usage_info.total_tokens() > 0) {
            std::cout << u8"\n\n⏱  Tokens: ";
            std::cout << "prompt=" << usage_info.prompt_tokens;
            std::cout << ", completion=" << usage_info.completion_tokens;
            if (usage_info.max_tokens > 0)
                std::cout << "/" << usage_info.max_tokens;
            std::cout << ", total=" << usage_info.total_tokens() << std::endl;
        }

        // Print status bar after response
        print_status_bar(g_state);
        std::cout << "\n";
    }

    return 0;
}
