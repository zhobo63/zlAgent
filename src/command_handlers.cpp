#include "pch.h"

#include "logger.h"
#include "command_handlers.h"
#include "config.h"
#include "agent.h"
#include "skill_system.h"
#include "rag_manager.h"
#include "long_term_memory.h"
#include "json.hpp"
#include "key_watcher.h"
#include "file_utils.h"
#include "project_summary/summary_tool.h"
#include <iostream>
#include <string>

namespace agent {

void register_command_handlers(
    CommandDispatcher& dispatcher,
    Agent* ag,
    SkillRegistry* skill_registry,
    RAGManager* rag_manager,
    LongTermMemory* long_term_memory) {

    KeyWatcher::add_keywords({
        "/h", "/help", "/status", "/config",
        "/skills", "/tools", "/tool",
        "/agent",
        "/task",
        "/model",
        "/facts", "/sessions", "/summary", "/new", "/save",
        "/search-kb", "/add-doc",
        "/view", "/outline", "/scan"
        "/reply",
        "/quit", "/exit"
    });

    // ── /help ────────────────────────────────────────
    auto show_help = [](const std::vector<std::string>&, std::string&) {
        LOG_INFO("Command", std::string{"\nAvailable commands:\n"
            "  /help              Show this help message\n"
            "  /status            Show agent status (tools, skills, memory)\n"
            "  /config            Show current configuration summary\n"
            "  /skills            List registered skills\n"
            "  /tools             List all internal tools\n"
            "  /tool <name>       Show details for a specific tool\n"
            "  /agent             List sub-agents (local, remote clients, net agent)\n"
            "  /view <path>       Overview project directory and generate code summary\n"
            "  /outline <file>    Show file outline (symbol names and line numbers)\n"
            "  /scan <path>       Scan project directory and generate code summary\n"
            "\nModel commands:\n"
            "  /model             List available models and switch interactively\n"
            "\nMemory commands:\n"
            "  /facts [prefix]    Query semantic facts (optional prefix filter)\n"
            "  /sessions [n]      List recent session summaries (default: 5)\n"
            "  /summary           Summarize current history and start a new conversation with the summary\n"
            "  /new               Start a new conversation (clear history)\n"
            "  /save              Save current session to long-term memory now\n"
            "\nRAG commands:\n"
            "  /search-kb query   Search knowledge base directly from CLI\n"
            "  /add-doc path      Add a file or directory to the knowledge base\n"
            "\nUser Reply (intervention):\n"
            "  /reply             Show current user reply mode\n"
            "  /reply off         Disable intervention (fully automatic)\n"
            "  /reply exec        Pause before terminal command calls\n"
            "  /reply edit        Pause before file edit/write calls\n"
            "  /reply always      Pause before every tool call\n"
            "\nTerminal command shortcut:\n"
            "  Shell commands are auto-detected and executed directly, bypassing the LLM.\n"
            "  Safe commands (ls, git status, etc.) run immediately; risky ones ask first.\n"
            "  Configure via [terminal_commands] in zlagent.ini.\n"
            "\nSession control:\n"
            "  /quit, /exit       Exit (same as typing 'quit')\n"});
    };
    dispatcher.register_command("h", show_help);
    dispatcher.register_command("help", show_help);

    // ── /status ────────────────────────────────────────
    dispatcher.register_command("status", [ag, long_term_memory, rag_manager](const std::vector<std::string>&, std::string&) {
        if (!ag) return;
        int tool_count = static_cast<int>(ag->get_tool_names().size());
        LOG_INFO("Command", "\n--- Agent Status ---\n  Tools registered: " + std::to_string(tool_count) + "\n");

        // Skills.
        auto sr = get_global_skill_registry();
        if (sr) {
            int enabled = 0, disabled = 0;
            for (const auto& s : sr->get_skills()) {
                if (s->enabled) ++enabled; else ++disabled;
            }
            LOG_INFO("Command", "  Skills: " + std::to_string(enabled) + " enabled" +
                      (disabled > 0 ? ", " + std::to_string(disabled) + " disabled" : "") + "\n");
        }

        // Long-term memory.
        if (long_term_memory) {
            LOG_INFO("Command", "  Long-term memory: " +
                      std::to_string(long_term_memory->get_recent_sessions(100).size()) + " sessions, " +
                      std::to_string(long_term_memory->get_facts().size()) + " facts\n");
        }

        // RAG.
        if (rag_manager) {
            LOG_INFO("Command", "  RAG chunks: " + std::to_string(rag_manager->total_chunks()) + "\n");
        }
    });

    // ── /config ────────────────────────────────────────
    dispatcher.register_command("config", [](const std::vector<std::string>&, std::string&) {
        LOG_INFO("Command", std::string{"\n--- Configuration ---\n"
            "  (Use 'zlagent.ini' to modify settings)\n"
            "  See zlagent.ini for all available options.\n"});
    });

    // ── /skills ────────────────────────────────────────
    dispatcher.register_command("skills", [](const std::vector<std::string>&, std::string&) {
        auto sr = get_global_skill_registry();
        if (!sr) {
            LOG_INFO("Command", "  No skill registry available.");
            return;
        }

        auto skills = sr->get_skills();
        if (skills.empty()) {
            LOG_INFO("Command", "  No skills registered.");
            return;
        }

        LOG_INFO("Command", "\n--- Registered Skills ---");
        for (const auto& s : skills) {
            std::string status = s->enabled ? "[OK]" : "[DISABLED]";
            std::string desc = s->description.substr(0, 80);
            if (s->description.size() > 80) desc += "...";
            LOG_INFO("Command", "  " + status + " **" + s->name + "** - " + desc);
        }
    });

    // ── /tools - list all internal tools ────────────────
    dispatcher.register_command("tools", [ag](const std::vector<std::string>&, std::string&) {
        if (!ag) return;

        auto tools = ag->get_tools();
        if (tools.empty()) {
            LOG_INFO("Command", "  No tools registered.");
            return;
        }

        LOG_INFO("Command", "\n--- Registered Tools ---");
        for (const auto& t : tools) {
            std::string desc = t->description();
            if (desc.size() > 100) desc = desc.substr(0, 97) + "...";
            LOG_INFO("Command", "  **" + t->name() + "** - " + desc);
        }
    });

    // ── /tool <name> - show tool details ────────────────
    dispatcher.register_command("tool", [ag](const std::vector<std::string>& args, std::string&) {
        if (!ag) return;

        if (args.empty()) {
            LOG_INFO("Command", "  Usage: /tool <name> - Show details for a specific tool.");
            return;
        }

        std::string name = args[0];
        auto tools = ag->get_tools();

        for (const auto& t : tools) {
            if (t->name() == name) {
                LOG_INFO("Command", "\n--- Tool Details ---");
                LOG_INFO("Command", "  Name:      **" + t->name() + "**");
                LOG_INFO("Command", "  Description: " + t->description());
                LOG_INFO("Command", "  Parameters:\n" + t->parameters_schema());
                return;
            }
        }

        LOG_INFO("Command", "  Tool '" + name + "' not found. Use /tools to list available tools.");
    });

    // ── /agent - list sub-agents ──────────────────────────
    dispatcher.register_command("agent", [ag](const std::vector<std::string>&, std::string&) {
        if (!ag) return;

        bool has_any = false;

        // Local sub-agents registered via MultiAgent server.
        auto ma = ag->get_multi_agent();
        if (ma && ma->is_enable()) {
            auto local_agents = ma->get_local_agents();
            if (!local_agents.empty()) {
                LOG_INFO("Command", "\n--- Local Sub-Agents ---");
                for (const auto& [name, desc] : local_agents) {
                    std::string d = desc;
                    if (d.size() > 80) d = d.substr(0, 77) + "...";
                    LOG_INFO("Command", "  **" + name + "** - " + d);
                }
                has_any = true;
            }

            auto remote_clients = ma->get_remote_clients();
            if (!remote_clients.empty()) {
                LOG_INFO("Command", "\n--- Remote Clients ---");
                for (const auto& rc : remote_clients) {
                    std::string d = rc.description;
                    if (d.size() > 80) d = d.substr(0, 77) + "...";
                    LOG_INFO("Command", "  **" + rc.name + "** [" + rc.chat_id + "] - " + d);
                }
                has_any = true;
            }
        }

        // SubAgentNet client (this instance connecting to a remote server).
        auto sub_agent = ag->get_sub_agent();
        if (sub_agent) {
            LOG_INFO("Command", "\n--- Net Agent (Client Mode) ---");
            std::string status = sub_agent->is_connected() ? "[Connected]" : "[Disconnected]";
            LOG_INFO("Command", "  **" + sub_agent->get_name() + "** " + status + " - " + sub_agent->description());
            has_any = true;
        }

        if (!has_any) {
            LOG_INFO("Command", "  No sub-agents registered.");
        }
    });

    // ── /task <name> <task> - send task to a specific sub-agent ────────────────
    dispatcher.register_command("task", [ag](const std::vector<std::string>& args, std::string& response) {
        if (!ag) return;

        // Need at least: /task <name> <task>
        if (args.size() < 2) {
            LOG_INFO("Command", "Usage: /task <agent_name> <task_description>");
            LOG_INFO("Command", "Example: /task reviewer Please review the latest changes");

            // List available agents for convenience.
            auto ma = ag->get_multi_agent();
            if (ma && ma->is_enable()) {
                auto local_agents = ma->get_local_agents();
                if (!local_agents.empty()) {
                    LOG_INFO("Command", "\nAvailable sub-agents:");
                    for (const auto& [name, desc] : local_agents) {
                        std::string d = desc;
                        if (d.size() > 60) d = d.substr(0, 57) + "...";
                        LOG_INFO("Command", "  - " + name + ": " + d);
                    }
                }
            }

            auto tools = ag->get_tools();
            if (!tools.empty()) {
                bool has_sub_agent_tool = false;
                for (const auto& tool : tools) {
                    // SubAgentTool names match registered sub-agents.
                    std::string name = tool->name();
                    if (ma && ma->is_enable()) {
                        auto local_agents = ma->get_local_agents();
                        for (const auto& [an, ad] : local_agents) {
                            if (an == name) { has_sub_agent_tool = true; break; }
                        }
                    }
                }
            }

            return;
        }

        std::string agent_name = args[0];
        // Reconstruct the task from remaining args.
        std::string task;
        for (size_t i = 1; i < args.size(); ++i) {
            if (!task.empty()) task += " ";
            task += args[i];
        }

        // Try to find the sub-agent tool by name.
        auto tool = ag->get_tool(agent_name);
        if (!tool) {
            LOG_WARN("Command", "Sub-agent not found: " + agent_name);
            return;
        }

        LOG_INFO("Command", "Sending task to **" + agent_name + "**: " + task);
        nlohmann::json args_json;
        args_json["task"] = task;
        std::string result = tool->execute(args_json.dump());

        // Display the result, truncating if too long.
        if (result.size() > 2000) {
            LOG_INFO("Command", "Result from **" + agent_name + "**:");
            LOG_INFO("Command", result.substr(0, 1997) + "...");
        } else {
            LOG_INFO("Command", "Result from **" + agent_name + "**:");
            LOG_INFO("Command", result);
        }
    });

    // ── /model - interactive model switcher ────────────────
    dispatcher.register_command("model", [ag](const std::vector<std::string>&, std::string&) {
        if (!ag) return;

        auto models = ag->get_llm().list_models();
        std::string current_model = ag->get_llm().get_model();

        if (models.empty()) {
            LOG_INFO("Command", "  Unable to query /v1/models API.\n  Current model: " + current_model);
            return;
        }

        // Sort models alphabetically by id.
        std::sort(models.begin(), models.end(), [](const auto& a, const auto& b) {
            return a.id < b.id;
        });

        // Helper: format context length for display.
        auto fmt_ctx = [](int ctx) -> std::string {
            if (ctx == 0) return "?";
            if (ctx >= 1000000)
                return std::to_string(ctx / 1000) + "K";
            if (ctx >= 1000)
                return std::to_string(ctx / 1000) + "K";
            return std::to_string(ctx);
        };

        LOG_INFO("Command", "\n--- Available Models ---");
        for (size_t i = 0; i < models.size(); ++i) {
            const auto& m = models[i];
            std::string marker = (m.id == current_model) ? " <-- CURRENT" : "";
            LOG_INFO("Command", "  [" + std::to_string(i + 1) + "] " + m.id +
                      " (ctx: " + fmt_ctx(m.context_length) +
                      ", owned_by: " + m.owned_by + ")"
                      + marker);
        }
        LOG_INFO("Command", "\nEnter model number to switch, or press Enter to keep current (" + current_model + "): ");

        std::string input = KeyWatcher::readline("Select Model>", nullptr);
        if (input.empty())
            return;

        // Trim.
        while (!input.empty() && std::isspace(static_cast<unsigned char>(input.front()))) input.erase(input.begin());
        while (!input.empty() && std::isspace(static_cast<unsigned char>(input.back())))  input.pop_back();

        if (input.empty()) {
            LOG_INFO("Command", "  Kept current model: " + current_model);
            return;
        }

        // Parse number.
        int num = -1;
        try { num = std::stoi(input); } catch (...) {
            LOG_ERROR("Command", "Failed to parse model number from input: '" + input + "'");
        }

        if (num < 1 || static_cast<size_t>(num) > models.size()) {
            LOG_INFO("Command", "  Invalid selection. Kept current model: " + current_model);
            return;
        }

        const auto& selected = models[num - 1];
        ag->set_llm_model(selected.id);

        // Auto-adjust max_tokens based on context length.
        // Use ~25% of context as a sensible default for output, capped at 8192.
        int new_max_tokens = 4096;  // fallback
        if (selected.context_length > 0) {
            new_max_tokens = std::min(selected.context_length / 4, 8192);
        }

        // Persist both model and max_tokens to zlagent.ini.
        agent::IniParser::update_key("zlagent.ini", "llm", "model", selected.id);
        agent::IniParser::update_key("zlagent.ini", "llm", "max_tokens", std::to_string(new_max_tokens));

        LOG_INFO("Command", "  Model switched to: " + selected.id + "\n" +
            "  Context length:    " + fmt_ctx(selected.context_length) + " tokens\n" +
            "  max_tokens set to: " + std::to_string(new_max_tokens) + " (auto-adjusted)");
    });

    // ── /facts [prefix] ────────────────────────────────
    dispatcher.register_command("facts", [long_term_memory](const std::vector<std::string>& args, std::string&) {
        auto ltm = long_term_memory;
        if (!ltm) {
            LOG_INFO("Command", "  Long-term memory not initialized.");
            return;
        }

        std::string prefix;
        if (!args.empty()) prefix = args[0];

        auto facts = ltm->get_facts(prefix);
        if (facts.empty()) {
            if (prefix.empty()) {
                LOG_INFO("Command", "  No facts stored.");
            } else {
                LOG_INFO("Command", "  No facts with prefix: \"" + prefix + "\"");
            }
            return;
        }

        LOG_INFO("Command", "\n--- Semantic Facts ---");
        for (const auto& f : facts) {
            std::string msg = "  **" + f.key + "** = " + f.value;
            if (!f.source_session.empty()) {
                msg += " [from " + f.source_session.substr(0, 10) + "]";
            }
            LOG_INFO("Command", msg);
        }
    });

    // ── /sessions [n] ────────────────────────────────
    dispatcher.register_command("sessions", [long_term_memory](const std::vector<std::string>& args, std::string&) {
        auto ltm = long_term_memory;
        if (!ltm) {
            LOG_INFO("Command", "  Long-term memory not initialized.");
            return;
        }

        int n = 5;
        if (!args.empty()) {
            try { n = std::stoi(args[0]); } catch (...) {
                LOG_WARN("Command", "Failed to parse session count from: '" + args[0] + "', using default (5)");
                n = 5;
            }
        }

        auto sessions = ltm->get_recent_sessions(n);
        if (sessions.empty()) {
            LOG_INFO("Command", "  No past sessions found.");
            return;
        }

        LOG_INFO("Command", "\n--- Recent Sessions ---");
        for (size_t i = 0; i < sessions.size(); ++i) {
            const auto& s = sessions[i];
            std::string date = s.timestamp.substr(0, 10);
            LOG_INFO("Command", "  " + std::to_string(i + 1) + ". [" + date + "] **" + s.topic + "**");
            // Indent summary lines.
            std::istringstream iss(s.summary);
            std::string line;
            while (std::getline(iss, line)) {
                LOG_INFO("Command", "     " + line);
            }
        }
    });

    // ── /new ───────────────────────────────────────────
    dispatcher.register_command("new", [ag](const std::vector<std::string>&, std::string&) {
        if (!ag) return;
        ag->new_session();
        LOG_INFO("Command", "  New conversation started.");
    });

    // ── /summary ───────────────────────────────────────
    dispatcher.register_command("summary", [ag](const std::vector<std::string>&, std::string&) {
        if (!ag) return;

        auto& memory = ag->get_memory();
        const auto& messages = memory.get_messages();

        // Need at least one non-system message to summarize.
        bool has_content = false;
        for (const auto& m : messages) {
            if (m.role != "system") { has_content = true; break; }
        }
        if (!has_content) {
            LOG_INFO("Command", "  Nothing to summarize.");
            return;
        }

        // Build summary prompt — exclude system messages.
        ChatMessage sys_msg{"system",
            "You are a summarizer. Summarize the following conversation into 3-5 bullet points. "
            "Preserve key facts, decisions, code changes, and important context. "
            "Be concise but complete."};

        std::vector<ChatMessage> prompt;
        prompt.push_back(sys_msg);
        for (const auto& m : messages) {
            if (m.role == "system") continue;
            prompt.push_back(ChatMessage{m.role, m.content, m.name});
        }

        LOG_INFO("Command", "  Summarizing conversation...");
        auto resp = ag->get_llm().chat(prompt);

        if (resp.content.empty()) {
            LOG_WARN("Command", "  Summarization failed.");
            return;
        }

        // Clear history and insert the summary as the start of a new conversation.
        // Use role "user" so that LM Studio's jinja prompt template sees a valid
        // system → user → assistant flow after re-injecting the system prompt.
        memory.clear();
        ag->reset_iteration_count();
        ag->reset_tokens_used();
        ChatMessage summary_msg{"user",
            "[Summary of previous conversation]\n" + resp.content};
        memory.add(summary_msg);

        LOG_INFO("Command", "  Conversation summarized. New session started with summary:\n\n" + resp.content);
    });

    // ── /save ────────────────────────────────────────
    dispatcher.register_command("save", [ag, long_term_memory](const std::vector<std::string>&, std::string&) {
        auto ltm = long_term_memory;
        if (!ltm) {
            LOG_INFO("Command", "  Long-term memory not initialized.");
            return;
        }
        if (!ag) {
            LOG_INFO("Command", "  Agent not available.");
            return;
        }

        LOG_INFO("Command", "  Saving session to long-term memory...");
        ltm->save_session(ag->get_memory());

        LOG_INFO("Command", "  Generating summary and extracting facts...");
        if (ltm->summarize_session(ag->get_memory(), ag->get_llm())) {
            LOG_INFO("Command", "  Session saved successfully.");
        } else {
            LOG_WARN("Command", "  Failed to summarize session.");
        }
    });

    // ── /search-kb query ────────────────────────────────
    dispatcher.register_command("search-kb", [rag_manager](const std::vector<std::string>& args, std::string&) {
        if (args.empty()) {
            LOG_INFO("Command", "  Usage: /search-kb <query>");
            return;
        }

        // Reconstruct the full query from all arguments.
        std::string query;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) query += " ";
            query += args[i];
        }

        auto rag = rag_manager;
        if (!rag) {
            LOG_INFO("Command", "  RAG knowledge base not initialized.");
            return;
        }

        auto results = rag->search(query);
        if (results.empty()) {
            LOG_INFO("Command", "  No relevant results for: \"" + query + "\"");
            return;
        }

        LOG_INFO("Command", "\n--- Knowledge Base Results ---\n  Query: \"" + query + "\"\n");
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            LOG_INFO("Command", "  Result " + std::to_string(i + 1) + " (score: " + std::to_string(static_cast<double>(r.score)) + ")");
            LOG_INFO("Command", "  Source: " + r.source);
            std::istringstream iss(r.content);
            std::string line;
            while (std::getline(iss, line)) {
                LOG_INFO("Command", "    " + line);
            }
        }
    });

    // ── /quit, /exit ────────────────────────────────
    dispatcher.register_command("quit", [](const std::vector<std::string>&, std::string&) {
        LOG_INFO("Command", "  Goodbye!");
        exit(0);
    });
    dispatcher.register_command("exit", [](const std::vector<std::string>&, std::string&) {
        LOG_INFO("Command", "  Goodbye!");
        exit(0);
    });

    // ── /add-doc path ────────────────────────────────
    dispatcher.register_command("add-doc", [rag_manager](const std::vector<std::string>& args, std::string&) {
        if (args.empty()) {
            LOG_INFO("Command", "  Usage: /add-doc <file_or_directory_path>");
            return;
        }

        auto rag = rag_manager;
        if (!rag) {
            LOG_INFO("Command", "  RAG knowledge base not initialized.");
            return;
        }

        std::string path = args[0];
        // Check if it's a directory or file.
        bool is_dir = false;
        try {
            is_dir = std::filesystem::is_directory(path);
        } catch (...) {
            LOG_ERROR("Command", "Failed to check if path is directory: '" + path + "'");
        }

        if (is_dir) {
            rag->add_directory(path);
            LOG_INFO("Command", "  Added directory: " + path);
        } else {
            rag->add_file(path);
            LOG_INFO("Command", "  Added file: " + path);
        }

        LOG_INFO("Command", "  Total chunks now: " + std::to_string(rag->total_chunks()));
    });

    // ── /scan [path] ────────────────────────────────────────
    dispatcher.register_command("view", [](const std::vector<std::string>& args, std::string&) {
        std::string path = ".";
        if (!args.empty()) {
            path = args[0];
            try {
                if (!std::filesystem::is_directory(path)) {
                    LOG_ERROR("Command", "Path is not a directory: '" + path + "'");
                    return;
                }
            } catch (...) {
                LOG_ERROR("Command", "Failed to check path: '" + path + "'");
                return;
            }
        }

        Agent *ag = get_global_agent();
        auto overview = ag->get_tool("project_overview");

        if (!overview) {
            LOG_ERROR("Command", "Tool 'project_overview' not found");
            return;
        }

        nlohmann::json args_json;
        args_json["directory"] = path;
        std::string result = overview->execute(args_json.dump());
        LOG_INFO("Command", result.c_str());


    });

    // ── /scan [path] ────────────────────────────────────────
    dispatcher.register_command("scan", [](const std::vector<std::string>& args, std::string&) {
        std::string path = ".";
        if (!args.empty()) {
            path = args[0];
            try {
                if (!std::filesystem::is_directory(path)) {
                    LOG_ERROR("Command", "Path is not a directory: '" + path + "'");
                    return;
                }
            } catch (...) {
                LOG_ERROR("Command", "Failed to check path: '" + path + "'");
                return;
            }
        }

        std::ostringstream out;
        generate_report(path, out);
        TOUT::markdown(out.str());
        //quick_summary(path);
    });

    // ── /outline <file> ─────────────────────────────────────
    dispatcher.register_command("outline", [](const std::vector<std::string>& args, std::string&) {
        if (args.empty()) {
            LOG_ERROR("Command", "Usage: /outline <file>");
            return;
        }

        std::string path = args[0];
        try {
            if (!std::filesystem::is_regular_file(path)) {
                LOG_ERROR("Command", "Not a regular file: '" + path + "'");
                return;
            }
        } catch (...) {
            LOG_ERROR("Command", "Failed to check path: '" + path + "'");
            return;
        }

        std::string outline = GenerateFileOutline(path);
        if (outline.empty()) {
            LOG_INFO("Command", "No symbols found in '" + path + "'.");
            return;
        }

        LOG_INFO("Command", outline.c_str());
    });

    // ── /reply [mode] ──────────────────────────────────────
    dispatcher.register_command("reply", [ag](const std::vector<std::string>& args, std::string&) {
        if (!ag) return;

        if (args.empty()) {
            // Show current mode.
            LOG_INFO("Command", "  Current user reply mode: " + std::string(reply_mode_to_string(ag->get_user_reply_mode())));
            return;
        }

        auto mode = parse_reply_mode(args[0]);
        ag->set_user_reply_mode(mode);
        LOG_INFO("Command", "  User reply mode set to: " + std::string(reply_mode_to_string(mode)));
    });

} // register_command_handlers

} // namespace agent
