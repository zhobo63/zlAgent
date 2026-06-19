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
#include "agent.h"
#include "tools.h"
#include "plugin_loader.h"
#include "local_tools.h"
#include "wide_string.h"

// ── Input history: up/down arrow to browse previous commands ─────────
static const int MAX_HISTORY = 50;
static std::vector<std::string> g_input_history;   // submitted commands (oldest first)

/// Helper: convert wresult back to UTF-8 string
static std::string wide_to_utf8(const std::wstring& ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, &out[0], len - 1, nullptr, nullptr);
    return out;
}

/// Helper: clear the current line and redraw from a UTF-8 string
static void redraw_line(const std::string& utf8_str) {
    std::cout << "\r\x1b[K";  // clear to end of line
    for (wchar_t wc : agent::utf8_to_wide(utf8_str))
        std::wcout << wc;
}

/// Read multi-line input from the console.
/// - Enter alone: submit the current buffer
/// - Shift+Enter: insert a newline (multi-line mode)
/// - Up/Down arrow: browse command history
// Calculate the display width of a wide string.
// CJK characters (and other fullwidth chars) occupy 2 cells; ASCII and halfwidth occupy 1.
static int get_display_width(const std::wstring& s) {
    int w = 0;
    for (wchar_t c : s) {
        if (c >= 0x1100 &&
            ((c <= 0x115F) || (c == 0x2329) || (c == 0x232A) ||
             (c >= 0x2E80 && c <= 0xA4CF && c != 0x303F) ||
             (c >= 0xAC00 && c <= 0xD7A3) ||
             (c >= 0xF900 && c <= 0xFAD9) ||
             (c >= 0xFE10 && c <= 0xFE19) ||
             (c >= 0xFE30 && c <= 0xFE6F) ||
             (c >= 0xFF00 && c <= 0xFF60) ||
             (c >= 0xFFE0 && c <= 0xFFE6) ||
             (c >= 0x20000 && c <= 0x2FFFD) ||
             (c >= 0x30000 && c <= 0x3FFFD))) {
            w += 2;
        } else {
            w += 1;
        }
    }
    return w;
}

static std::string read_multiline_input() {
    std::wstring wresult;
#ifdef _WIN32
    // hist_offset == 0 means we are at the "working" position (current typed text).
    // hist_offset > 0 means we've navigated up by that many steps into history.
    // The actual index in g_input_history is: size - hist_offset.
    static int hist_offset = 0;          // how far back from the end
    static std::string working_buf;      // what the user was typing before pressing up

    while (true) {
        wchar_t ch = _getwch();

        // Enter key
        if (ch == L'\r') {
            if (GetKeyState(VK_SHIFT) < 0) {
                // Shift+Enter: insert newline
                wresult += L'\n';
                std::cout << "\n";
            } else {
                // Enter alone: submit — save to history and break
                std::string submitted = wide_to_utf8(wresult);
                if (!submitted.empty()) {
                    g_input_history.push_back(submitted);
                    if (static_cast<int>(g_input_history.size()) > MAX_HISTORY)
                        g_input_history.erase(g_input_history.begin());
                }
                hist_offset = 0;
                working_buf.clear();
                break;
            }
        }
        // Backspace — erase the correct number of display cells for the last character
        else if (ch == 8) {
            if (!wresult.empty()) {
                int before = get_display_width(wresult);
                wresult.pop_back();
                int after = get_display_width(wresult);
                int erased = before - after;  // 1 for ASCII, 2 for CJK
                for (int i = 0; i < erased; ++i)
                    std::cout << "\b \b";
            }
        }
        // Extended key (arrow keys etc.) — prefix is 0 or 224
        else if (ch == 0 || ch == 224) {
            wchar_t next = _getwch();

            if (next == 72) {  // Up arrow
                if (hist_offset == 0) {
                    // Save current typed text as working buffer before navigating
                    working_buf = wide_to_utf8(wresult);
                }
                int max_offset = static_cast<int>(g_input_history.size());
                if (hist_offset < max_offset)
                    hist_offset++;

                // Erase old input by backing up, then clear to end of line and redraw
                int display_w = get_display_width(wresult);
                for (int i = 0; i < display_w; ++i)
                    std::cout << "\b";
                std::cout << "\x1b[K";  // clear rest of line (handles multi-column CJK)

                std::string display_str;
                if (hist_offset <= max_offset) {
                    size_t idx = static_cast<size_t>(max_offset - hist_offset);
                    display_str = g_input_history[idx];
                }
                wresult.clear();
                for (wchar_t wc : agent::utf8_to_wide(display_str)) {
                    wresult += wc;
                    std::wcout << wc;
                }
            }
            else if (next == 80) {  // Down arrow
                if (hist_offset > 0)
                    hist_offset--;

                int max_offset = static_cast<int>(g_input_history.size());
                std::string display_str;
                if (hist_offset <= max_offset && hist_offset > 0) {
                    size_t idx = static_cast<size_t>(max_offset - hist_offset);
                    display_str = g_input_history[idx];
                } else {
                    // At working position — restore what user was typing
                    display_str = working_buf;
                }

                // Erase old input by backing up, then clear to end of line and redraw
                int display_w = get_display_width(wresult);
                for (int i = 0; i < display_w; ++i)
                    std::cout << "\b";
                std::cout << "\x1b[K";

                wresult.clear();
                for (wchar_t wc : agent::utf8_to_wide(display_str)) {
                    wresult += wc;
                    std::wcout << wc;
                }
            }
        }
        // Printable character (including CJK via IME)
        else {
            wresult += ch;
            std::wcout << ch;
        }
    }

    // Convert wide string to UTF-8 narrow string
    int len = WideCharToMultiByte(CP_UTF8, 0, wresult.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wresult.c_str(), -1, &result[0], len - 1, nullptr, nullptr);
    return result;
#else
    // Fallback: single-line for non-Windows
    std::string fallback;
    std::getline(std::cin, fallback);
    return fallback;
#endif
}

int main() {
#ifdef _WIN32
    // Set C runtime locale so std::cout handles multibyte (UTF-8) characters correctly.
    setlocale(LC_ALL, "zh_TW.UTF-8");

    // Set console input/output code pages to UTF-8 so emoji and all Unicode display correctly.
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);

    // Enable VT processing for ANSI escape codes (e.g., dim text for thinking output).
    {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode))
            SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
    // Load configuration from zlagent.ini (falls back to defaults if not found).
    auto cfg = agent::Config::load("zlagent.ini");

    // Set log level early so all subsequent LOG_* calls respect it.
    agent::set_log_level(agent::parse_log_level(cfg.logging.level));
    LOG_INFO("Main", "Log level set to: " + agent::log_level_to_string(agent::parse_log_level(cfg.logging.level)));

    std::cout << "========================================" << std::endl;
    std::cout << "  ZL Agent - Code Assistant" << std::endl;
    std::cout << "  LLM: " << cfg.llm.url << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

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
    ag.set_local_tools_enabled(cfg.local_tools.enabled);

    // User Reply mode: allow user intervention during reasoning loop.
    ag.set_user_reply_mode(agent::parse_reply_mode(cfg.agent_.user_reply_mode));
    LOG_INFO("Config", "User reply mode: " + std::string(agent::reply_mode_to_string(ag.get_user_reply_mode())));

    // Register built-in tools
    LOG_INFO("Main", "Registering built-in tools...");
    ag.add_tool(agent::create_read_file_tool());
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

    std::cout << "\nReady. Type your request (or '/help' for commands):\n" << std::endl;

    // Interactive loop with streaming output.
    std::string input;
    while (true) {
        std::cout << "You: [" << ag.get_llm().get_model() << "] ";
        input = read_multiline_input();
        if (input.empty()) continue;

        if (input == "quit" || input == "exit" || input == "/quit" || input == "/exit") {
            // Save session to long-term memory before exiting.
            if (long_term_memory) {
                std::cout << "\nSaving session to long-term memory..." << std::endl;
                long_term_memory->save_session(ag.get_memory(), ag.get_llm());
            }
            std::cout << "Goodbye!" << std::endl;
            break;
        }

        if (input.empty()) continue;

        // Dispatch slash-commands before sending to LLM.
        if (dispatcher.dispatch(input)) continue;

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

        // Display token usage if available
        if (usage_info.total_tokens() > 0) {
            std::cout << u8"\n\n⏱  Tokens: ";
            std::cout << "prompt=" << usage_info.prompt_tokens;
            std::cout << ", completion=" << usage_info.completion_tokens;
            if (usage_info.max_tokens > 0)
                std::cout << "/" << usage_info.max_tokens;
            std::cout << ", total=" << usage_info.total_tokens() << std::endl;
        }

        std::cout << "\n";
    }

    return 0;
}
