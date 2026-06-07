#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include "config.h"
#include "language_detector.h"
#include "system_prompt.h"
#include "agent.h"
#include "tools.h"
#include "plugin_loader.h"
#include "local_tools.h"

int main() {
    // Load configuration from zlagent.ini (falls back to defaults if not found).
    auto cfg = agent::Config::load("zlagent.ini");

    std::cout << "========================================" << std::endl;
    std::cout << "  ZL Agent - C++ Code Assistant" << std::endl;
    std::cout << "  LLM: " << cfg.llm.url << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    agent::Agent ag(cfg.llm.url);

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

    // Load external plugins from configured directory.
    std::cout << "\nLoading external plugins..." << std::endl;
    agent::PluginLoader loader;
    auto plugins = loader.load_plugins(cfg.plugins.directory);
    for (auto& plugin : plugins) {
        ag.add_tool(std::move(plugin));
    }

    // Discover and register local tools if enabled.
    if (cfg.local_tools.enabled) {
        std::cout << "\nDiscovering local tools..." << std::endl;
        auto local_tools = agent::create_local_tools();
        for (auto& tool : local_tools) {
            ag.add_tool(std::move(tool));
        }
    }

    std::cout << "\nReady. Type your request (or 'quit' to exit):\n" << std::endl;

    // Interactive loop with streaming output.
    std::string input;
    while (true) {
        std::cout << "You: ";
        if (!std::getline(std::cin, input)) break;

        if (input == "quit" || input == "exit") {
            std::cout << "\nGoodbye!" << std::endl;
            break;
        }

        if (input.empty()) continue;

        std::cout << "\nAgent: ";
        ag.run_stream(input, [](const std::string& token) {
            std::cout << token << std::flush;
            return true;  // keep streaming
        });
        std::cout << "\n\n";
    }

    return 0;
}
