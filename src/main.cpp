#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include "config.h"
#include "safety_guard.h"
#include "language_detector.h"
#include "system_prompt.h"
#include "skill_system.h"
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

    // ── Safety setup ───────────────────────────────────────
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

    // ── Skill System ───────────────────────────────────────
    agent::SkillRegistry skill_registry;
    agent::set_global_skill_registry(&skill_registry);

    std::cout << "\nLoading skills..." << std::endl;

    // 1. Load native skills from zlagent/skills/.
    auto native_skills = agent::SkillLoader::scan_directory("zlagent/skills", "native");
    for (auto& skill : native_skills) {
        skill_registry.register_skill(skill);
        std::cout << "  ✓ " << skill->name
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

        // Safety: input filter — detect prompt injection attempts.
        if (cfg.safety.input_filter && agent::SafetyGuard::is_prompt_injection(input)) {
            std::cerr << "\n⚠️  [Safety] Possible prompt injection detected. Input rejected." << std::endl;
            continue;
        }

        std::cout << "\nAgent: ";
        ag.run_stream(input, [](const std::string& token) {
            std::cout << token << std::flush;
            return true;  // keep streaming
        });
        std::cout << "\n\n";
    }

    return 0;
}
