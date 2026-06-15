#include <iostream>
#include <clocale>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>
#include <thread>
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

    std::cout << "========================================" << std::endl;
    std::cout << "  ZL Agent - Code Assistant" << std::endl;
    std::cout << "  LLM: " << cfg.llm.url << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    agent::Agent ag(cfg.llm.url, cfg.llm.model);

    // === Safety setup ===
    if (!cfg.safety.path_whitelist.empty()) {
        agent::SafetyGuard::set_path_whitelist(cfg.safety.path_whitelist);
        std::cout << "[Config] Path whitelist enabled: ";
        for (size_t i = 0; i < cfg.safety.path_whitelist.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << cfg.safety.path_whitelist[i];
        }
        std::cout << std::endl;
    } else {
        std::cout << "[Config] Path whitelist: disabled (no restriction)" << std::endl;
    }

    // Determine the effective language: auto-detect > config value.
    std::string effective_language = cfg.agent_.language;
    if (cfg.agent_.auto_detect_language) {
        std::string detected = agent::LanguageDetector::detect_directory(".");
        if (!detected.empty()) {
            effective_language = detected;
            std::cout << "[Config] Auto-detected language: " << detected
                      << " (overriding config value '" << cfg.agent_.language << "')" << std::endl;
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
            std::cout << "[Config] System prompt loaded from: " << cfg.agent_.prompt_file << std::endl;
        } else {
            std::cerr << "[Warning] Cannot open prompt file '" << cfg.agent_.prompt_file
                      << "', using built-in." << std::endl;
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

    // Register built-in tools
    std::cout << "Registering built-in tools..." << std::endl;
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

    std::cout << "\nLoading skills..." << std::endl;

    // 1. Load native skills from zlagent/skills/.
    auto native_skills = agent::SkillLoader::scan_directory("zlagent/skills", "native");
    for (auto& skill : native_skills) {
        skill_registry.register_skill(skill);
        std::cout << "  \u2713 " << skill->name
                  << " (" << skill->source_path << ")" << std::endl;
    }

    // 2. Auto-detect and import cross-agent skills.
    {
        std::map<std::string, agent::SkillPtr> existing;
        for (const auto& s : skill_registry.get_skills()) existing[s->name] = s;
        auto imported_skills = agent::SkillLoader::auto_detect_and_import(".", existing);
        for (auto& skill : imported_skills) {
            skill_registry.register_skill(skill);
            std::cout << "  + " << skill->name
                      << " [imported from " << skill->source_path << "]" << std::endl;
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
    std::cout << "\n" << enabled_count << " skills loaded"
              << (disabled_count > 0 ? ", " + std::to_string(disabled_count) + " disabled" : "")
              << "." << std::endl;

    // Load external plugins from configured directory.
    std::cout << "\nLoading external plugins..." << std::endl;
    agent::PluginLoader loader;
    auto plugins = loader.load_plugins(cfg.plugins.directory);
    for (auto& plugin : plugins) {
        ag.add_tool(std::move(plugin));
    }

    // Local tools are discovered lazily on first chat — no startup delay.

    // === RAG System ===
    if (cfg.rag.enabled) {
        std::cout << "\nInitializing RAG system..." << std::endl;

        agent::EmbeddingProvider* provider = nullptr;
        if (cfg.rag.embedding_backend == "lm_studio") {
            provider = new agent::LLMEmbeddingProvider(cfg.llm.url, cfg.rag.embedding_model);
            std::cout << "  Embedding backend: LM Studio (" << cfg.rag.embedding_model << ")" << std::endl;
        } else {
            provider = new agent::TfidfEmbeddingProvider();
            std::cout << "  Embedding backend: TF-IDF (local)" << std::endl;
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
            std::cout << "  Loading existing knowledge base from: " << cfg.rag.store_path << std::endl;
            rag_manager->load_store(cfg.rag.store_path);
        }

        // Ingest knowledge directories at startup.
        for (const auto& dir : cfg.rag.knowledge_dirs) {
            std::cout << "  Ingesting: " << dir << std::endl;
            rag_manager->add_directory(dir);
        }

        // Save store if persistence is configured.
        if (!cfg.rag.store_path.empty()) {
            rag_manager->save(cfg.rag.store_path);
            std::cout << "  Knowledge base saved to: " << cfg.rag.store_path << std::endl;
        }

        set_global_rag_manager(rag_manager.get());
        ag.add_tool(agent::create_search_knowledge_base_tool());

        std::cout << "  Total chunks indexed: " << rag_manager->total_chunks() << std::endl;
    }

    // === Long-Term Memory ===
    std::unique_ptr<agent::LongTermMemory> long_term_memory;
    if (cfg.memory.long_term_enabled) {
        std::cout << "\nInitializing long-term memory..." << std::endl;

        agent::LongTermMemory::Config ltm_cfg;
        ltm_cfg.store_dir = cfg.memory.store_dir;
        ltm_cfg.max_sessions = cfg.memory.max_sessions;
        ltm_cfg.inject_facts_to_prompt = cfg.memory.inject_facts_to_prompt;
        ltm_cfg.auto_extract_facts = cfg.memory.auto_extract_facts;

        long_term_memory = std::make_unique<agent::LongTermMemory>(ltm_cfg);

        // Load from disk.
        if (long_term_memory->load()) {
            std::cout << "  Loaded: " << long_term_memory->get_recent_sessions(10).size()
                      << " sessions, " << long_term_memory->get_facts().size() << " facts" << std::endl;
        } else {
            std::cout << "  No existing memory found (starting fresh)" << std::endl;
        }

        // Inject facts into system prompt.
        if (ltm_cfg.inject_facts_to_prompt) {
            std::string context = long_term_memory->build_context_string(5);
            if (!context.empty()) {
                system_prompt += "\n\n" + context;
                ag.set_system_prompt(system_prompt);
                std::cout << "  Injected semantic facts into system prompt" << std::endl;
            }
        }

        // Integrate with RAG if available.
        if (agent::get_global_rag_manager()) {
            long_term_memory->integrate_with_rag(agent::get_global_rag_manager());
            std::cout << "  Session summaries injected into RAG knowledge base" << std::endl;
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

    std::cout << "\nReady. Type your request (or '/help' for commands):\n" << std::endl;

    // Interactive loop with streaming output.
    std::string input;
    while (true) {
        std::cout << "You: [" << ag.get_llm().get_model() << "] ";
        if (!std::getline(std::cin, input)) break;

        if (input == "quit" || input == "exit") {
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
            std::cerr << u8"\n!  [Safety] Possible prompt injection detected. Input rejected." << std::endl;
            continue;
        }

        // --- Waiting spinner animation (rotating circle, single-threaded) ---
        const char* spinners = "\u2809\u281B\u281E\u2817\u2814\u281A\u281C\u2808";  // ⠋⠙⠹⠸⠼⠴⠦⠧
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

        bool in_reasoning = false;
        ag.run_stream(input, [&](const std::string& token, bool is_reasoning_flag) {
            // First reasoning token: show thinking indicator (dim)
            if (is_reasoning_flag && !in_reasoning) {
                in_reasoning = true;
                std::cout << "\033[2m";  // dim for thinking content
            }
            // Transition from reasoning to content: restore normal brightness
            else if (!is_reasoning_flag && in_reasoning) {
                in_reasoning = false;
                std::cout << "\033[0m";   // reset (end dim)
            }

            std::cout << token << std::flush;
            return true;  // keep streaming
        });
        // Ensure terminal is back to normal even if reasoning was the last output.
        if (in_reasoning) {
            in_reasoning = false;
            std::cout << "\033[0m";
        }
        std::cout << "\n\n";
    }

    return 0;
}
