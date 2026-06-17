#include "pch.h"

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
    dispatcher.register_command("help", [](const auto&) {
        std::cout << "\nAvailable commands:\n"
                  << "  /help              Show this help message\n"
                  << "  /status            Show agent status (tools, skills, memory)\n"
                  << "  /config            Show current configuration summary\n"
                  << "  /skills            List registered skills\n"
                  << "\nModel commands:\n"
                  << "  /model             List available models and switch interactively\n"
                  << "  /model-info        Show current LLM model info\n"
                  << "\nMemory commands:\n"
                  << "  /facts [prefix]    Query semantic facts (optional prefix filter)\n"
                  << "  /sessions [n]      List recent session summaries (default: 5)\n"
                  << "  /summary           Summarize current history and start a new conversation with the summary\n"
                  << "  /new               Start a new conversation (clear history)\n"
                  << "  /clear-memory      Clear current conversation history\n"
                  << "  /save-session      Save current session to long-term memory now\n"
                  << "\nRAG commands:\n"
                  << "  /search-kb query   Search knowledge base directly from CLI\n"
                  << "  /add-doc path      Add a file or directory to the knowledge base\n"
                  << "\nSession control:\n"
                  << "  /quit, /exit       Exit (same as typing 'quit')\n"
                  << std::endl;
    });

    // ── /status ────────────────────────────────────────
    dispatcher.register_command("status", [ag](const auto&) {
        if (!ag) return;
        int tool_count = static_cast<int>(ag->get_tool_names().size());
        std::cout << "\n--- Agent Status ---\n"
                  << "  Tools registered: " << tool_count << "\n";

        // Skills.
        auto sr = get_global_skill_registry();
        if (sr) {
            int enabled = 0, disabled = 0;
            for (const auto& s : sr->get_skills()) {
                if (s->enabled) ++enabled; else ++disabled;
            }
            std::cout << "  Skills: " << enabled << " enabled"
                      << (disabled > 0 ? ", " + std::to_string(disabled) + " disabled" : "")
                      << "\n";
        }

        // Long-term memory.
        auto ltm = get_global_long_term_memory();
        if (ltm) {
            auto sessions = ltm->get_recent_sessions(1);
            std::cout << "  Long-term memory: "
                      << ltm->get_recent_sessions(100).size() << " sessions, "
                      << ltm->get_facts().size() << " facts\n";
        }

        // RAG.
        auto rag = get_global_rag_manager();
        if (rag) {
            std::cout << "  RAG chunks: " << rag->total_chunks() << "\n";
        }

        std::cout << std::endl;
    });

    // ── /config ────────────────────────────────────────
    dispatcher.register_command("config", [](const auto&) {
        std::cout << "\n--- Configuration ---\n"
                  << "  (Use 'zlagent.ini' to modify settings)\n"
                  << "  See zlagent.ini for all available options.\n"
                  << std::endl;
    });

    // ── /skills ────────────────────────────────────────
    dispatcher.register_command("skills", [](const auto&) {
        auto sr = get_global_skill_registry();
        if (!sr) {
            std::cout << "  No skill registry available.\n";
            return;
        }

        auto skills = sr->get_skills();
        if (skills.empty()) {
            std::cout << "  No skills registered.\n";
            return;
        }

        std::cout << "\n--- Registered Skills ---\n";
        for (const auto& s : skills) {
            std::string status = s->enabled ? "[OK]" : "[DISABLED]";
            std::cout << "  " << status << " **" << s->name << "** - "
                      << s->description.substr(0, 80);
            if (s->description.size() > 80) std::cout << "...";
            std::cout << "\n";
        }
        std::cout << std::endl;
    });

    // ── /model - interactive model switcher ────────────────
    dispatcher.register_command("model", [ag](const auto&) {
        if (!ag) return;

        auto models = ag->get_llm().list_models();
        std::string current_model = ag->get_llm().get_model();

        if (models.empty()) {
            std::cout << "  Unable to query /v1/models API.\n"
                      << "  Current model: " << current_model << "\n";
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

        std::cout << "\n--- Available Models ---\n";
        for (size_t i = 0; i < models.size(); ++i) {
            const auto& m = models[i];
            std::string marker = (m.id == current_model) ? " <-- CURRENT" : "";
            std::cout << "  [" << (i + 1) << "] " << m.id
                      << " (ctx: " << fmt_ctx(m.context_length)
                      << ", owned_by: " << m.owned_by << ")"
                      << marker << "\n";
        }
        std::cout << "\nEnter model number to switch, or press Enter to keep current (" 
                  << current_model << "): ";

        std::string input;
        if (!std::getline(std::cin, input)) return;

        // Trim.
        while (!input.empty() && std::isspace(static_cast<unsigned char>(input.front()))) input.erase(input.begin());
        while (!input.empty() && std::isspace(static_cast<unsigned char>(input.back())))  input.pop_back();

        if (input.empty()) {
            std::cout << "  Kept current model: " << current_model << "\n";
            return;
        }

        // Parse number.
        int num = -1;
        try { num = std::stoi(input); } catch (...) {
            std::cerr << "[Command] Failed to parse model number from input: '" << input << "'" << std::endl;
        }

        if (num < 1 || static_cast<size_t>(num) > models.size()) {
            std::cout << "  Invalid selection. Kept current model: " << current_model << "\n";
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

        std::cout << "  Model switched to: " << selected.id << "\n"
                  << "  Context length:    " << fmt_ctx(selected.context_length) << " tokens\n"
                  << "  max_tokens set to: " << new_max_tokens << " (auto-adjusted)\n";
    });

    // ── /model-info ────────────────────────────────
    dispatcher.register_command("model-info", [ag](const auto&) {
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

        std::cout << "\n--- LLM Model Info ---\n"
                  << "  Current model: " << ag->get_llm().get_model() << "\n";

        // Also show available models from API.
        auto models = ag->get_llm().list_models();
        if (!models.empty()) {
            std::cout << "  Available on server: " << models.size() << " model(s)\n";
            for (const auto& m : models) {
                std::string marker = (m.id == ag->get_llm().get_model()) ? " <-- CURRENT" : "";
                std::cout << "    - " << m.id
                          << " (ctx: " << fmt_ctx(m.context_length)
                          << ", owned_by: " << m.owned_by << ")"
                          << marker << "\n";
            }
        } else {
            std::cout << "  Unable to query /v1/models API.\n";
        }

        std::cout << std::endl;
    });

    // ── /facts [prefix] ────────────────────────────────
    dispatcher.register_command("facts", [](const auto& args) {
        auto ltm = get_global_long_term_memory();
        if (!ltm) {
            std::cout << "  Long-term memory not initialized.\n";
            return;
        }

        std::string prefix;
        if (!args.empty()) prefix = args[0];

        auto facts = ltm->get_facts(prefix);
        if (facts.empty()) {
            if (prefix.empty()) {
                std::cout << "  No facts stored.\n";
            } else {
                std::cout << "  No facts with prefix: \"" << prefix << "\"\n";
            }
            return;
        }

        std::cout << "\n--- Semantic Facts ---\n";
        for (const auto& f : facts) {
            std::cout << "  **" << f.key << "** = " << f.value;
            if (!f.source_session.empty()) {
                std::cout << " [from " << f.source_session.substr(0, 10) << "]";
            }
            std::cout << "\n";
        }
        std::cout << std::endl;
    });

    // ── /sessions [n] ────────────────────────────────
    dispatcher.register_command("sessions", [](const auto& args) {
        auto ltm = get_global_long_term_memory();
        if (!ltm) {
            std::cout << "  Long-term memory not initialized.\n";
            return;
        }

        int n = 5;
        if (!args.empty()) {
            try { n = std::stoi(args[0]); } catch (...) {
                std::cerr << "[Command] Failed to parse session count from: '" << args[0] << "', using default (5)" << std::endl;
                n = 5;
            }
        }

        auto sessions = ltm->get_recent_sessions(n);
        if (sessions.empty()) {
            std::cout << "  No past sessions found.\n";
            return;
        }

        std::cout << "\n--- Recent Sessions ---\n";
        for (size_t i = 0; i < sessions.size(); ++i) {
            const auto& s = sessions[i];
            std::string date = s.timestamp.substr(0, 10);
            std::cout << "  " << (i + 1) << ". [" << date << "] **" << s.topic << "**\n";
            std::string preview = s.summary;
            if (preview.size() > 200) {
                preview.resize(200);
                preview += "...";
            }
            // Indent summary lines.
            std::istringstream iss(preview);
            std::string line;
            while (std::getline(iss, line)) {
                std::cout << "     " << line << "\n";
            }
        }
        std::cout << std::endl;
    });

    // ── /new ───────────────────────────────────────────
    dispatcher.register_command("new", [ag](const auto&) {
        if (!ag) return;
        ag->get_memory().clear();
        std::cout << "  New conversation started.\n";
    });

    // ── /summary ───────────────────────────────────────
    dispatcher.register_command("summary", [ag](const auto&) {
        if (!ag) return;

        auto& memory = ag->get_memory();
        auto messages = memory.get_messages();

        // Need at least one non-system message to summarize.
        bool has_content = false;
        for (const auto& m : messages) {
            if (m.role != "system") { has_content = true; break; }
        }
        if (!has_content) {
            std::cout << "  Nothing to summarize.\n";
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
            std::string content = m.content;
            if (content.size() > 2048) {
                content = content.substr(0, 2048) + "... [truncated]";
            }
            prompt.push_back(ChatMessage{m.role, content, m.name});
        }

        std::cout << "  Summarizing conversation...\n";
        auto resp = ag->get_llm().chat(prompt);

        if (resp.content.empty()) {
            std::cout << "  Summarization failed.\n";
            return;
        }

        // Clear history and insert the summary as the start of a new conversation.
        memory.clear();
        ChatMessage summary_msg{"assistant",
            "[Summary of previous conversation]\n" + resp.content};
        memory.add(summary_msg);

        std::cout << "  Conversation summarized. New session started with summary:\n\n"
                  << resp.content << "\n";
    });

    // ── /clear-memory ────────────────────────────────
    dispatcher.register_command("clear-memory", [ag](const auto&) {
        if (!ag) return;
        ag->get_memory().clear();
        std::cout << "  Conversation history cleared.\n";
    });

    // ── /save-session ────────────────────────────────
    dispatcher.register_command("save-session", [ag](const auto&) {
        auto ltm = get_global_long_term_memory();
        if (!ltm) {
            std::cout << "  Long-term memory not initialized.\n";
            return;
        }
        if (!ag) {
            std::cout << "  Agent not available.\n";
            return;
        }

        std::cout << "  Saving session to long-term memory...\n";
        ltm->save_session(ag->get_memory(), ag->get_llm());
        std::cout << "  Session saved successfully.\n";
    });

    // ── /search-kb query ────────────────────────────────
    dispatcher.register_command("search-kb", [](const auto& args) {
        if (args.empty()) {
            std::cout << "  Usage: /search-kb <query>\n";
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
            std::cout << "  RAG knowledge base not initialized.\n";
            return;
        }

        auto results = rag->search(query);
        if (results.empty()) {
            std::cout << "  No relevant results for: \"" << query << "\"\n";
            return;
        }

        std::cout << "\n--- Knowledge Base Results ---\n"
                  << "  Query: \"" << query << "\"\n\n";
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            std::cout << "  Result " << (i + 1) << " (score: " << static_cast<double>(r.score) << ")\n";
            std::cout << "  Source: " << r.source << "\n";
            std::string preview = r.content;
            if (preview.size() > 500) {
                preview.resize(500);
                preview += "...";
            }
            std::istringstream iss(preview);
            std::string line;
            while (std::getline(iss, line)) {
                std::cout << "    " << line << "\n";
            }
        }
        std::cout << std::endl;
    });

    // ── /add-doc path ────────────────────────────────
    dispatcher.register_command("add-doc", [](const auto& args) {
        if (args.empty()) {
            std::cout << "  Usage: /add-doc <file_or_directory_path>\n";
            return;
        }

        auto rag = get_global_rag_manager();
        if (!rag) {
            std::cout << "  RAG knowledge base not initialized.\n";
            return;
        }

        std::string path = args[0];
        // Check if it's a directory or file.
        bool is_dir = false;
        try {
            is_dir = std::filesystem::is_directory(path);
        } catch (...) {
            std::cerr << "[Command] Failed to check if path is directory: '" << path << "'" << std::endl;
        }

        if (is_dir) {
            rag->add_directory(path);
            std::cout << "  Added directory: " << path << "\n";
        } else {
            rag->add_file(path);
            std::cout << "  Added file: " << path << "\n";
        }

        std::cout << "  Total chunks now: " << rag->total_chunks() << "\n";
    });

} // register_command_handlers

} // namespace agent
