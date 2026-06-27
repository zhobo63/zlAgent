#include "pch.h"

#include "logger.h"
#include "command_handlers.h"
#include "config.h"
#include "agent.h"
#include "skill_system.h"
#include "rag_manager.h"
#include "long_term_memory.h"

namespace agent {

void register_command_handlers(
    CommandDispatcher& dispatcher,
    Agent* ag,
    SkillRegistry* skill_registry,
    RAGManager* rag_manager,
    LongTermMemory* long_term_memory) {

    // ── /help ────────────────────────────────────────
    dispatcher.register_command("help", [](const std::vector<std::string>&) {
        LOG_INFO("Command", std::string{"\nAvailable commands:\n"
            "  /help              Show this help message\n"
            "  /status            Show agent status (tools, skills, memory)\n"
            "  /config            Show current configuration summary\n"
            "  /skills            List registered skills\n"
            "\nModel commands:\n"
            "  /model             List available models and switch interactively\n"
            "  /model-info        Show current LLM model info\n"
            "\nMemory commands:\n"
            "  /facts [prefix]    Query semantic facts (optional prefix filter)\n"
            "  /sessions [n]      List recent session summaries (default: 5)\n"
            "  /summary           Summarize current history and start a new conversation with the summary\n"
            "  /new               Start a new conversation (clear history)\n"
            "  /clear-memory      Clear current conversation history\n"
            "  /save-session      Save current session to long-term memory now\n"
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
    });

    // ── /status ────────────────────────────────────────
    dispatcher.register_command("status", [ag](const std::vector<std::string>&) {
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
        auto ltm = get_global_long_term_memory();
        if (ltm) {
            auto sessions = ltm->get_recent_sessions(1);
            LOG_INFO("Command", "  Long-term memory: " +
                      std::to_string(ltm->get_recent_sessions(100).size()) + " sessions, " +
                      std::to_string(ltm->get_facts().size()) + " facts\n");
        }

        // RAG.
        auto rag = get_global_rag_manager();
        if (rag) {
            LOG_INFO("Command", "  RAG chunks: " + std::to_string(rag->total_chunks()) + "\n");
        }
    });

    // ── /config ────────────────────────────────────────
    dispatcher.register_command("config", [](const std::vector<std::string>&) {
        LOG_INFO("Command", std::string{"\n--- Configuration ---\n"
            "  (Use 'zlagent.ini' to modify settings)\n"
            "  See zlagent.ini for all available options.\n"});
    });

    // ── /skills ────────────────────────────────────────
    dispatcher.register_command("skills", [](const std::vector<std::string>&) {
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

    // ── /model - interactive model switcher ────────────────
    dispatcher.register_command("model", [ag](const std::vector<std::string>&) {
        if (!ag) return;

        auto models = ag->get_llm().list_models();
        std::string current_model = ag->get_llm().get_model();

        if (models.empty()) {
            LOG_INFO("Command", "  Unable to query /v1/models API.\n  Current model: " + current_model);
            return;
        }

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

        std::string input;
        if (!std::getline(std::cin, input)) return;

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

    // ── /model-info ────────────────────────────────
    dispatcher.register_command("model-info", [ag](const std::vector<std::string>&) {
        if (!ag) return;

        // Helper: format context length for display.
        auto fmt_ctx = [](int ctx) -> std::string {
            if (ctx == 0) return "?";
            if (ctx >= 1000000)
                return std::to_string(ctx / 1000) + "K";
            if (ctx >= 1000)
                return std::to_string(ctx / 1000) + "K";
            return std::to_string(ctx);
        };

        LOG_INFO("Command", std::string{"\n--- LLM Model Info ---\n"
                  "  Current model: "} + ag->get_llm().get_model());

        // Also show available models from API.
        auto models = ag->get_llm().list_models();
        if (!models.empty()) {
            LOG_INFO("Command", "  Available on server: " + std::to_string(models.size()) + " model(s)");
            for (const auto& m : models) {
                std::string marker = (m.id == ag->get_llm().get_model()) ? " <-- CURRENT" : "";
                LOG_INFO("Command", "    - " + m.id +
                          " (ctx: " + fmt_ctx(m.context_length) +
                          ", owned_by: " + m.owned_by + ")"
                          + marker);
            }
        } else {
            LOG_INFO("Command", "  Unable to query /v1/models API.");
        }
    });

    // ── /facts [prefix] ────────────────────────────────
    dispatcher.register_command("facts", [](const std::vector<std::string>& args) {
        auto ltm = get_global_long_term_memory();
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
    dispatcher.register_command("sessions", [](const std::vector<std::string>& args) {
        auto ltm = get_global_long_term_memory();
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
    dispatcher.register_command("new", [ag](const std::vector<std::string>&) {
        if (!ag) return;
        ag->get_memory().clear();
        LOG_INFO("Command", "  New conversation started.");
    });

    // ── /summary ───────────────────────────────────────
    dispatcher.register_command("summary", [ag](const std::vector<std::string>&) {
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
        memory.clear();
        ChatMessage summary_msg{"assistant",
            "[Summary of previous conversation]\n" + resp.content};
        memory.add(summary_msg);

        LOG_INFO("Command", "  Conversation summarized. New session started with summary:\n\n" + resp.content);
    });

    // ── /clear-memory ────────────────────────────────
    dispatcher.register_command("clear-memory", [ag](const std::vector<std::string>&) {
        if (!ag) return;
        ag->get_memory().clear();
        LOG_INFO("Command", "  Conversation history cleared.");
    });

    // ── /save-session ────────────────────────────────
    dispatcher.register_command("save-session", [ag](const std::vector<std::string>&) {
        auto ltm = get_global_long_term_memory();
        if (!ltm) {
            LOG_INFO("Command", "  Long-term memory not initialized.");
            return;
        }
        if (!ag) {
            LOG_INFO("Command", "  Agent not available.");
            return;
        }

        LOG_INFO("Command", "  Saving session to long-term memory...");
        ltm->save_session(ag->get_memory(), ag->get_llm());
        LOG_INFO("Command", "  Session saved successfully.");
    });

    // ── /search-kb query ────────────────────────────────
    dispatcher.register_command("search-kb", [](const std::vector<std::string>& args) {
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

        auto rag = get_global_rag_manager();
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
    dispatcher.register_command("quit", [](const std::vector<std::string>&) {
        LOG_INFO("Command", "  Goodbye!");
        exit(0);
    });
    dispatcher.register_command("exit", [](const std::vector<std::string>&) {
        LOG_INFO("Command", "  Goodbye!");
        exit(0);
    });

    // ── /add-doc path ────────────────────────────────
    dispatcher.register_command("add-doc", [](const std::vector<std::string>& args) {
        if (args.empty()) {
            LOG_INFO("Command", "  Usage: /add-doc <file_or_directory_path>");
            return;
        }

        auto rag = get_global_rag_manager();
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

    // ── /reply [mode] ──────────────────────────────────────
    dispatcher.register_command("reply", [ag](const std::vector<std::string>& args) {
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
