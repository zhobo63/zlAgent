#include "pch.h"

#include "agent.h"
#include "local_tools.h"
#include "task_planner.h"
#include "self_reflector.h"
#include "logger.h"
#include "tools.h"
#include "plugin_loader.h"
#include "file_utils.h"
#include "safety_guard.h"
#include "system_prompt.h"
#include "rag_manager.h"
#include "command_handlers.h"
#include <regex>

namespace agent {

using json = nlohmann::json;

static Agent* g_agent = nullptr;

Agent* get_global_agent() { return g_agent; }
void set_global_agent(Agent* ag) { g_agent = ag; }

Agent::Agent() {}

Agent::~Agent()
{
    if (multi_agent_) {
        multi_agent_->stop();
    }
}

void Agent::load_config(const std::string& name)
{
    config_ = Config::load(name);
    auto& cfg = config_;

    // Set log level early so all subsequent LOG_* calls respect it.
    set_log_level(parse_log_level(cfg.logging.level));

    LOG_INFO("LLM", cfg.llm.url);

    llm_.set_base_url(cfg.llm.url);
    llm_.set_model(cfg.llm.model);
    LOG_DEBUG("Main", "Log level set to: " + agent::log_level_to_string(agent::parse_log_level(cfg.logging.level)));

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
    }
    else {
        LOG_INFO("Config", "Path whitelist: disabled (no restriction)");
    }

    if (!cfg.safety.working_directory.empty()) {
        agent::SafetyGuard::get_instance().set_working_directory(cfg.safety.working_directory);
        LOG_INFO("Config", "Working directory set to: " + cfg.safety.working_directory);
    }
    else {
        // Default to current working directory
        std::string cwd = agent::SafetyGuard::normalize_path(std::filesystem::current_path().string());
        agent::SafetyGuard::get_instance().set_working_directory(cwd);
        LOG_INFO("Config", "Working directory defaulted to: " + cwd);
    }

    agent::SafetyGuard::get_instance().set_strict_mode(cfg.safety.strict_mode);
    LOG_INFO("Config", "Strict mode: " + std::string(cfg.safety.strict_mode ? "enabled (reject out-of-scope paths)" : "disabled (confirm out-of-scope paths)"));

    // System prompt: external file > built-in language-specific > multi-language default.
    std::string& system_prompt = system_prompt_;
    if (!cfg.agent_.prompt_file.empty()) {
        std::ifstream pf(cfg.agent_.prompt_file);
        if (pf.is_open()) {
            std::ostringstream oss;
            oss << pf.rdbuf();
            system_prompt = oss.str();
            LOG_INFO("Config", "System prompt loaded from: " + cfg.agent_.prompt_file);
        }
        else {
            LOG_WARN("Config", "Cannot open prompt file '" + cfg.agent_.prompt_file + "', using built-in.");
            system_prompt = agent::SystemPromptProvider::get();
        }
    }
    else {
        system_prompt = agent::SystemPromptProvider::get();
    }
    set_system_prompt(system_prompt);

    // Apply feature toggles from config.
    set_task_planning(cfg.features.task_planning);
    set_self_reflection(cfg.features.self_reflection);
    set_max_iterations(cfg.agent_.max_iterations);
    set_max_reflection_retries(cfg.features.max_reflection_retries);
    set_local_tools_enabled(cfg.local_tools.enabled);

    // User Reply mode: allow user intervention during reasoning loop.
    set_user_reply_mode(agent::parse_reply_mode(cfg.agent_.user_reply_mode));
    LOG_INFO("Config", "User reply mode: " + std::string(agent::reply_mode_to_string(get_user_reply_mode())));

    // === RAG System ===
    if (cfg.rag.enabled) {
        LOG_INFO("RAG", "\nInitializing RAG system...");

        agent::EmbeddingProvider* provider = nullptr;
        if (cfg.rag.embedding_backend == "lm_studio") {
            provider = new agent::LLMEmbeddingProvider(cfg.llm.url, cfg.rag.embedding_model);
            LOG_INFO("RAG", "  Embedding backend: LM Studio (" + cfg.rag.embedding_model + ")");
        }
        else {
            provider = new agent::TfidfEmbeddingProvider();
            LOG_INFO("RAG", "  Embedding backend: TF-IDF (local)");
        }

        // Build RAG config.
        agent::RAGManager::Config rag_cfg;
        rag_cfg.top_k = cfg.rag.top_k;
        rag_cfg.min_score = cfg.rag.min_score;
        rag_cfg.store_path = cfg.rag.store_path;

        // Create RAG manager — stored as Agent member to avoid dangling pointer.
        rag_manager_ = std::make_unique<agent::RAGManager>(provider, rag_cfg);

        // Load existing store if available.
        if (!cfg.rag.store_path.empty() && std::filesystem::exists(cfg.rag.store_path)) {
            LOG_INFO("RAG", "  Loading existing knowledge base from: " + cfg.rag.store_path);
            rag_manager_->load_store(cfg.rag.store_path);
        }

        // Ingest knowledge directories at startup.
        for (const auto& dir : cfg.rag.knowledge_dirs) {
            LOG_INFO("RAG", "  Ingesting: " + dir);
            rag_manager_->add_directory(dir);
        }

        // Save store if persistence is configured.
        if (!cfg.rag.store_path.empty()) {
            rag_manager_->save(cfg.rag.store_path);
            LOG_INFO("RAG", "  Knowledge base saved to: " + cfg.rag.store_path);
        }

        add_tool(agent::create_search_knowledge_base_tool(rag_manager_.get()));

        LOG_INFO("RAG", "  Total chunks indexed: " + std::to_string(rag_manager_->total_chunks()));
    }

    // === Long-Term Memory ===

    if (cfg.memory.long_term_enabled) {
        LOG_INFO("Memory", "\nInitializing long-term memory...");

        agent::LongTermMemory::Config ltm_cfg;
        ltm_cfg.store_dir = cfg.memory.store_dir;
        ltm_cfg.max_sessions = cfg.memory.max_sessions;
        ltm_cfg.inject_facts_to_prompt = cfg.memory.inject_facts_to_prompt;
        ltm_cfg.auto_extract_facts = cfg.memory.auto_extract_facts;

        long_term_memory_ = std::make_unique<agent::LongTermMemory>(ltm_cfg);
        std::unique_ptr<agent::LongTermMemory> &long_term_memory = long_term_memory_;

        // Load from disk.
        if (long_term_memory->load()) {
            LOG_INFO("Memory", "  Loaded: " + std::to_string(long_term_memory->get_recent_sessions(10).size()) + " sessions, " + std::to_string(long_term_memory->get_facts().size()) + " facts");
        }
        else {
            LOG_INFO("Memory", "  No existing memory found (starting fresh)");
        }

        // Inject facts into system prompt.
        if (ltm_cfg.inject_facts_to_prompt) {
            std::string context = long_term_memory->build_context_string(5);
            if (!context.empty()) {
                system_prompt += "\n\n" + context;
                set_system_prompt(system_prompt);
                LOG_INFO("Memory", "  Injected semantic facts into system prompt");
            }
        }

        // Integrate with RAG if available.
        if (rag_manager_) {
            long_term_memory->integrate_with_rag(rag_manager_.get());
            LOG_INFO("Memory", "  Session summaries injected into RAG knowledge base");
        }

        add_tool(agent::create_search_memories_tool(long_term_memory.get()));
        add_tool(agent::create_recall_facts_tool(long_term_memory.get()));
    }



    // === CLI Command Dispatcher ===
    agent::CommandDispatcher& dispatcher = dispatcher_;
    register_command_handlers(
        dispatcher,
        this,
        agent::get_global_skill_registry(),
        rag_manager_.get(),
        long_term_memory_.get());

    // Register the /reply-mode command for user intervention control.
    register_reply_mode_command(dispatcher, this);

    // Terminal command detector — intercept shell commands before LLM.
    if (cfg.terminal_commands.enabled) {
        terminal_detector_ = agent::TerminalCommandDetector::create(
            cfg.terminal_commands.direct_commands,
            cfg.terminal_commands.confirm_commands);
    }

    if (cfg.telegram.enabled && !cfg.telegram.bot_token.empty()) {
        agent::TelegramClient::Config tg_cfg;
        tg_cfg.enabled = true;
        tg_cfg.bot_token = cfg.telegram.bot_token;
        tg_cfg.poll_timeout_sec = cfg.telegram.poll_timeout_sec;
        tg_cfg.max_updates_per_poll = cfg.telegram.max_updates_per_poll;
        tg_cfg.allowed_chat_ids = cfg.telegram.allowed_chat_ids;

        telegram_client_ = std::make_unique<agent::TelegramClient>(tg_cfg);
    }
    else if (cfg.telegram.enabled) {
        LOG_WARN("Telegram", "Telegram is enabled but bot_token is empty — skipping.");
    }

    // === Multi-Agent System ===

    // Initialize MultiAgent server if configured.
    if (cfg.multi_agent_config.enabled && cfg.multi_agent_config.listen_port > 0) {
        LOG_INFO("MultiAgent", "Initializing MultiAgent server on port " + std::to_string(cfg.multi_agent_config.listen_port));
        multi_agent_ = std::make_shared<agent::MultiAgent>(registry_);
        multi_agent_->start(cfg.multi_agent_config.listen_port);
    }

    // Initialize SubAgentNet client if configured.
    if (cfg.net_agent.enabled && !cfg.net_agent.url.empty()) {
        LOG_INFO("NetAgent", "Initializing WebSocket client: " + cfg.net_agent.url);
        std::string agent_name = cfg.net_agent.name.empty() ? "net_agent" : cfg.net_agent.name;
        std::string agent_desc = cfg.net_agent.description.empty() ? "Remote agent connected via WebSocket" : cfg.net_agent.description;
        sub_agent_ = std::make_shared<agent::SubAgentNet>(agent_name, agent_desc);
        SubAgentNet::Config net_cfg;
        net_cfg.enabled = true;
        net_cfg.url = cfg.net_agent.url;
        net_cfg.confirm_mode = cfg.net_agent.confirm_mode;
        sub_agent_->start(net_cfg);
    }

    // Initialize SubAgentLLM instances for each llm_agent entry.
    if (cfg.llm_agent.enabled) {
        LOG_INFO("LlmAgent", "Initializing LLM sub-agents");
        for (const auto& entry : cfg.llm_agent.agents) {
            if (!entry.enabled) continue;

            std::string agent_name = entry.name.empty() ? "llm_agent_" + std::filesystem::path(entry.workdir).filename().string() : entry.name;
            LOG_INFO("LlmAgent", "Creating sub-agent: " + agent_name + " (workdir: " + entry.workdir + ")");

            auto sub_agent = std::make_shared<agent::SubAgentLLM>(agent_name, entry.description);
            sub_agent->set_workdir(entry.workdir);
            if (!entry.system_prompt.empty()) {
                sub_agent->set_system_prompt(entry.system_prompt);
            }

            // Register with MultiAgent if available, otherwise register directly.
            if (multi_agent_) {
                multi_agent_->register_agent(sub_agent);
            }
            else {
                auto tool = std::make_shared<agent::SubAgentTool>(sub_agent);
                registry_.register_tool(tool);
            }
        }
    }
}

void Agent::register_tools()
{
    // Register built-in tools
    LOG_INFO("Main", "Registering built-in tools...");
    //ag.add_tool(agent::create_read_file_tool());
    add_tool(agent::create_read_files_tool());
    //ag.add_tool(agent::create_read_file_lines_tool());
    //ag.add_tool(agent::create_write_file_tool());
    add_tool(agent::create_write_files_tool());
    //ag.add_tool(agent::create_append_file_tool());
    //ag.add_tool(agent::create_insert_file_content_tool());
    //ag.add_tool(agent::create_edit_file_tool());
    add_tool(agent::create_edit_files_tool());
    add_tool(agent::create_list_directory_tool());
    add_tool(agent::create_terminal_tool());
    add_tool(agent::create_code_search_tool());
    add_tool(agent::create_create_directory_tool());
    //ag.add_tool(agent::create_delete_path_tool());
    add_tool(agent::create_delete_files_tool());
    add_tool(agent::create_copy_path_tool());
    add_tool(agent::create_move_path_tool());
    add_tool(agent::create_find_files_tool());
    add_tool(agent::create_get_file_outline_tool());
    add_tool(agent::create_grep_with_context_tool());
    add_tool(agent::create_run_build_tool());
    // Batch file tools (read/delete multiple files)
    add_tool(agent::create_delete_files_tool());
    //ag.add_tool(agent::create_git_status_tool());
    //ag.add_tool(agent::create_git_diff_tool());
    add_tool(agent::create_fetch_url_tool());
    add_tool(agent::create_project_overview_tool());
}

void Agent::register_skills()
{
    // === Skill System ===
    agent::SkillRegistry& skill_registry = skill_registry_;
    agent::set_global_skill_registry(&skill_registry);

    LOG_INFO("Agent", "\nLoading skills...");

    // 1. Load native skills from .zlagent/skills/.
    auto native_skills = agent::SkillLoader::scan_directory(DEFAULT_SKILL_DIR, "native");
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
        std::vector<std::string> tool_names = get_tool_names();
        for (auto& skill : skill_registry.get_skills()) {
            agent::SkillLoader::validate_dependencies(skill, tool_names);
        }
    }

    // 3. Inject skill summary into system prompt so the LLM knows available skills.
    {
        std::string& system_prompt = system_prompt_;

        std::string skill_summary = skill_registry.build_skill_summary();
        if (!skill_summary.empty()) {
            system_prompt += "\n\n" + skill_summary;
            set_system_prompt(system_prompt);
            LOG_DEBUG("skill_summary", skill_summary);
        }
    }

    // 4. Register skill management tools.
    add_tool(agent::create_get_skill_tool());
    add_tool(agent::create_create_skill_tool());
    add_tool(agent::create_delete_skill_tool());
    add_tool(agent::create_reload_skills_tool());

    // Log summary.
    int enabled_count = 0, disabled_count = 0;
    for (const auto& skill : skill_registry.get_skills()) {
        if (skill->enabled) ++enabled_count; else ++disabled_count;
    }
    LOG_INFO("Main", std::to_string(enabled_count) + " skills loaded" + (disabled_count > 0 ? ", " + std::to_string(disabled_count) + " disabled" : "") + ".");
}

void Agent::reload_skills()
{
    agent::SkillRegistry& skill_registry = skill_registry_;
    // 1. Hot-reload native skills from disk.
    std::string result = skill_registry_.reload_skills();

    // 2. Re-validate dependencies against available tools.
    {
        std::vector<std::string> tool_names = get_tool_names();
        for (auto& skill : skill_registry_.get_skills()) {
            agent::SkillLoader::validate_dependencies(skill, tool_names);
        }
    }

    // 2.5 Validate skill dependencies against available tools.
    {
        std::vector<std::string> tool_names = get_tool_names();
        for (auto& skill : skill_registry.get_skills()) {
            agent::SkillLoader::validate_dependencies(skill, tool_names);
        }
    }

    // 3. Re-inject skill summary into system prompt.
    {
        std::string skill_summary = skill_registry_.build_skill_summary();
        if (!skill_summary.empty()) {
            // Remove old skill summary from system prompt and append new one.
            auto pos = system_prompt_.rfind("\n\nAvailable skills:");
            if (pos != std::string::npos) {
                system_prompt_ = system_prompt_.substr(0, pos);
            }
            system_prompt_ += "\n\n" + skill_summary;
            set_system_prompt(system_prompt_);
        }
    }

    LOG_INFO("Agent", "Skill reload: " + result);
}

void Agent::load_plugins()
{
    // Load external plugins from configured directory.
    LOG_INFO("Main", "\nLoading external plugins...");
    agent::PluginLoader loader;
    auto plugins = loader.load_plugins(config_.plugins.directory);
    for (auto& plugin : plugins) {
        add_tool(std::move(plugin));
    }
}

void Agent::add_tool(ToolPtr tool) {
    registry_.register_tool(std::move(tool));
}

ToolPtr Agent::get_tool(const std::string& name) {
    return registry_.find_tool(name);
}

std::vector<std::string> Agent::get_tool_names() const {
    std::vector<std::string> names;
    for (const auto& tool : registry_.get_tools()) {
        names.push_back(tool->name());
    }
    return names;
}

std::vector<ToolPtr> Agent::get_tools() const {
    return registry_.get_tools();
}

void Agent::set_system_prompt(const std::string& prompt) {
    memory_.set_system_prompt(prompt);
}

// ── Lazy local tool discovery ──────────────────────────────

void Agent::discover_local_tools() {
    if (!local_tools_enabled_ || !lazy_local_tools_ || local_tools_discovered_) return;
    local_tools_discovered_ = true;

    LOG_INFO("Lazy", "\nDiscovering local tools...");
    auto local_tools = create_local_tools();
    for (auto& tool : local_tools) {
        registry_.register_tool(std::move(tool));
    }
    LOG_INFO("Lazy", "Local tools discovered and registered.");
}

void Agent::discover_local_tools_from_overview(const std::string& overview) {
    if (!local_tools_enabled_ || local_tools_discovered_) return;
    local_tools_discovered_ = true;

    LOG_INFO("Lazy", "\nDiscovering local tools from project overview...");
    auto new_tools = create_local_tools(overview);

    // Collect already-registered tool names to exclude duplicates.
    std::set<std::string> registered_names;
    for (const auto& t : registry_.get_tools()) {
        registered_names.insert(t->name());
    }

    int added = 0, skipped = 0;
    for (auto& tool : new_tools) {
        if (registered_names.count(tool->name())) {
            skipped++;
        } else {
            registry_.register_tool(std::move(tool));
            added++;
        }
    }

    LOG_INFO("Lazy", "Local tools from overview: " + std::to_string(added) + " registered, " + std::to_string(skipped) + " skipped (already registered)." );
}

// ── File reference preprocessing ───────────────────────────
// Parses user_input for patterns like "file.cpp L1-20", "file.cpp (1:10)", etc.
// and appends the corresponding file content after each reference.

struct LineRange {
    int start = 0;
    int end   = 0;
};

static std::vector<LineRange> parse_line_ranges(const std::string& spec) {
    std::vector<LineRange> ranges;
    // Split by ':' for multi-range, e.g. "1-10:50-60"
    size_t pos = 0;
    while (pos < spec.size()) {
        size_t colon = spec.find(':', pos);
        std::string segment = (colon == std::string::npos) ? spec.substr(pos) : spec.substr(pos, colon - pos);

        // Strip leading 'L'
        if (!segment.empty() && segment[0] == 'L')
            segment = segment.substr(1);

        size_t dash = segment.find('-');
        if (dash != std::string::npos) {
            try {
                LineRange r;
                r.start = std::stoi(segment.substr(0, dash));
                r.end   = std::stoi(segment.substr(dash + 1));
                ranges.push_back(r);
            } catch (...) {}
        } else {
            // Single line
            try {
                int val = std::stoi(segment);
                ranges.push_back({val, val});
            } catch (...) {}
        }

        pos = (colon == std::string::npos) ? spec.size() : colon + 1;
    }
    return ranges;
}

void Agent::new_session()
{
    get_memory().clear();
    reset_iteration_count();
    reset_tokens_used();
    processed_file_keys_.clear();
}

void Agent::save_session()
{
    if (long_term_memory_) {
        // Fast save — no LLM calls. Just stores timestamp and message count.
        TUI::out("\nSaving session to long-term memory...\n");
        long_term_memory_->save_session(memory_);
    }

}

std::string Agent::preprocess_file_references(const std::string& user_input) {
    // Regex: match "filepath line_spec" where line_spec is optional.
    // Filepath: at least one char of [a-zA-Z0-9_./\-] ending with .ext
    // Line spec (optional): whitespace then L?digits, possibly with - or : for ranges,
    // optionally wrapped in parentheses.
    // Regex for file with line spec
    static const std::regex file_ref_with_lines_re(
        R"((([a-zA-Z0-9_./\\-]+\.[a-zA-Z0-9]+))\s+(L?\d+(?:-\d+)?(?::L?\d+-\d+)*|\(L?\d+(?:-\d+)?(?::L?\d+-\d+)*\)))"
    );
    // Regex for bare file path (no line spec)
    static const std::regex file_ref_bare_re(
        R"(([a-zA-Z0-9_./\\-]+\.[a-zA-Z0-9]+))"
    );
    // Regex for directory paths: must contain / or \ to avoid matching plain words
    static const std::regex dir_ref_re(
        R"(([a-zA-Z0-9_.-]+[/\\](?:[a-zA-Z0-9_.-]+[/\\]*)*))"
    );

    std::string result = user_input;
    // We process matches from the original string and build a new result,
    // inserting content after each match.
    auto begin = std::sregex_iterator(user_input.begin(), user_input.end(), file_ref_with_lines_re);
    auto end   = std::sregex_iterator();

    // Collect all insertions with their positions (in the original string)
    std::vector<std::string> insertions;

    for (auto it = begin; it != end; ++it) {
        std::smatch m = *it;
        std::string filepath = m[2].str();
        std::string line_spec_raw = m[3].str();

        // Check file exists
        namespace fs = std::filesystem;
        if (!fs::exists(filepath)) {
            continue; // silently skip non-existent files
        }

        // Strip parentheses from line spec
        std::string line_spec = line_spec_raw;
        if (line_spec.front() == '(' && line_spec.back() == ')')
            line_spec = line_spec.substr(1, line_spec.size() - 2);

        auto ranges = parse_line_ranges(line_spec);
        if (ranges.empty()) continue;

        std::ostringstream content_oss;
        for (const auto& r : ranges) {
            if (r.start <= 0 || r.end < r.start) continue;
            // Skip if this filepath+range has already been processed
            std::string key = filepath + ":" + std::to_string(r.start) + "-" + std::to_string(r.end);
            if (processed_file_keys_.count(key)) continue;
            processed_file_keys_.insert(key);

            std::string lines_content = ReadFileLinesAsString(filepath, r.start, r.end);
            if (!lines_content.empty()) {
                content_oss << "\n--- File: " << filepath << " (lines "
                            << r.start << "-" << r.end << ") ---\n"
                            << lines_content
                            << "--- End of file ---\n";
            }
        }

        if (!content_oss.str().empty()) {
            insertions.push_back(content_oss.str());
        }
    }

    // Also match bare file paths without line spec — insert outline
    if (insertions.size() == 0) {
        auto begin2 = std::sregex_iterator(user_input.begin(), user_input.end(), file_ref_bare_re);
        for (auto it = begin2; it != end; ++it) {
            std::smatch m = *it;
            std::string filepath = m[1].str();

            // Skip if this filepath outline has already been processed
            std::string key = filepath + ":outline";
            if (processed_file_keys_.count(key)) continue;
            processed_file_keys_.insert(key);

            namespace fs = std::filesystem;
            if (!fs::exists(filepath)) continue;

            // If it's a directory, list its contents instead of generating an outline
            if (fs::is_directory(filepath)) {
                std::string listing = GenerateDirectoryListing(filepath);
                if (!listing.empty()) {
                    insertions.push_back(
                        "\n--- Directory: " + filepath + " ---\n" + listing + "--- End of directory ---\n");
                }
            }
            else {
                std::string outline = GenerateFileOutline(filepath);
                if (!outline.empty()) {
                    insertions.push_back(
                        "\n--- File Outline: " + filepath + " ---\n" + outline + "--- End of outline ---\n");
                }
            }
        }
    }

    // Also match directory paths — list contents
    if (insertions.size() == 0) {
        auto begin3 = std::sregex_iterator(user_input.begin(), user_input.end(), dir_ref_re);
        for (auto it = begin3; it != end; ++it) {
            std::smatch m = *it;
            std::string filepath = m[1].str();

            // Skip if already processed
            std::string key = filepath + ":dir";
            if (processed_file_keys_.count(key)) continue;
            processed_file_keys_.insert(key);

            namespace fs = std::filesystem;
            if (!fs::exists(filepath) || !fs::is_directory(filepath)) continue;

            std::string listing = GenerateDirectoryListing(filepath);
            if (!listing.empty()) {
                insertions.push_back(
                    "\n--- Directory: " + filepath + " ---\n" + listing + "--- End of directory ---\n");
            }
        }
    }

    // Apply insertions in reverse order so positions remain valid
    for (auto it = insertions.rbegin(); it != insertions.rend(); ++it) {
        result += it->c_str();
    }
    LOG_DEBUG("preprocess_file_references", result);

    return result;
}

// ── LLM-based planning decision ────────────────────────────

bool Agent::needs_planning(ChatResponse &resp) {
    // Fast path: trivially short inputs never need planning.
    try {
        std::string answer = resp.content;
        // Trim whitespace and convert to upper case for comparison.
        size_t start = answer.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) return false;
        answer = answer.substr(start);
        std::transform(answer.begin(), answer.end(), answer.begin(), ::toupper);
        return answer.find("YES") != std::string::npos;
    } catch (...) {
        // If the LLM call fails, fall back to a conservative default: no planning.
        return false;
    }
}

// Streaming version: tokens are printed as they arrive via on_token callback
std::string Agent::run_stream(const std::string& user_input, TokenCallback on_token, ChatResponse* usage_out) {
    // Lazy discover local tools on first chat.
    // discover_local_tools();

    // Preprocess: auto-inject file content for referenced files
    std::string enriched_input = preprocess_file_references(user_input);

    // Add user message to memory (with enriched content)
    ChatMessage user_msg{"user", enriched_input, ""};
    memory_.add(user_msg);

    // Reset iteration count before task
    current_iteration_ = 0;

    // Run streaming reasoning loop
    ChatResponse response = reasoning_loop_stream(user_input, on_token);

    // Pass usage info back if requested
    if (usage_out) {
        usage_out->prompt_tokens     = response.prompt_tokens;
        usage_out->completion_tokens = response.completion_tokens;
        usage_out->max_tokens        = response.max_tokens;
    }

    // Add assistant response to memory
    memory_.add(ChatMessage{"assistant", response.content, ""});

    return response.content;
}

// ── User Reply handler ─────────────────────────────────────────────
// After the caller has handled Abort and Custom, this processes the remaining
// actions. result is empty for pre-execution; contains tool output for post-execution.
enum class ReplyDirective {
    Continue,   // proceed with (possibly modified) args
    Skip,       // skip this tool call entirely
    ReExecute,  // re-execute with modified args (post-execution only)
};

static ReplyDirective handle_user_reply(
    ChatResponse::ToolCall& tc,
    const std::string& result,
    const UserReplyResult& reply) {

    if (reply.action == ReplyAction::No) {
        LOG_WARN("Tool", "Skipped: " + tc.name);
        return ReplyDirective::Skip;
    }

    if (result.empty()) {
        // Pre-execution: fall through to execute with args.
        return ReplyDirective::Continue;
    }
    // Post-execution: re-execute with args.
    return ReplyDirective::ReExecute;
}


// Streaming reasoning loop: same logic but uses chat_stream with token callback.
ChatResponse Agent::reasoning_loop_stream(const std::string& user_input, TokenCallback on_token) {
    int iteration = 0;
    size_t total_prompt = 0, total_completion = 0;  // accumulate across iterations

    while (iteration < max_iterations_) {
        iteration++;
        current_iteration_++;

        // Compress context if history is too large
        if (memory_.summarize(llm_)) {
            LOG_INFO("Memory", "\nContext compressed via summarization.\n");
        }

        const auto& messages = memory_.get_messages();

        // Ensure the last message in history is a "user" role.
        // LM Studio's Jinja prompt template requires a user query at the end;
        // after tool results are added, the tail may be [tool] or [assistant],
        // which causes "No user query found in messages." errors.
        std::vector<ChatMessage> messages_for_llm = messages;
        if (!messages.empty()) {
            const auto& last = messages.back();
            if (last.role != "user") {
                ChatMessage continue_user{"user", "Continue based on the tool results above."};
                messages_for_llm.push_back(continue_user);
            }
        }

        auto tool_defs = registry_.get_definitions();

        // Call LLM with streaming
        ChatResponse resp = llm_.chat_stream(messages_for_llm, on_token, tool_defs);

        // Accumulate token usage across iterations
		tokens_used_ += resp.total_tokens();
		max_tokens_ = resp.max_tokens;
        total_prompt += resp.prompt_tokens;
        total_completion += resp.completion_tokens;

        // If task planning is enabled and the input looks like a complex task, use the advanced pipeline.
        if (task_planning_ && needs_planning(resp)) {
            TUI::out("%s", run_planned(user_input, resp, on_token).c_str());
            return resp;
        }

        // If no tool calls, return the response
        LOG_DEBUG("Agent", "LLM response: has_tool_calls=" + std::to_string(resp.has_tool_calls) +
                 ", tool_calls.size()=" + std::to_string(resp.tool_calls.size()));
        if (!resp.has_tool_calls || resp.tool_calls.empty()) {
            resp.prompt_tokens = total_prompt;
            resp.completion_tokens = total_completion;
            return resp;
        }

        // Add assistant message with tool_calls to memory (required by OpenAI API)
        ChatMessage assistant_msg{"assistant", resp.content};
        for (const auto& tc : resp.tool_calls) {
            ToolCallInfo tci{};
            tci.id = tc.id;
            tci.name = tc.name;
            tci.arguments = tc.arguments;
            assistant_msg.tool_calls.push_back(std::move(tci));
        }
        memory_.add(assistant_msg);

        // Execute each tool call and add results to memory (non-streaming for tools)
        for (auto& tc : resp.tool_calls) {
            // Guard: skip if LLM returned empty arguments (streaming truncation)
            if (tc.arguments.empty()) {
                LOG_WARN("Tool", "\nSkipping '" + tc.name + u8"' — empty arguments from LLM, feeding error back");
                ChatMessage tool_msg{"tool", "[Error] Tool call was truncated: missing arguments for '" + tc.name + "'. Please retry with complete arguments.", tc.name};
                if (!tc.id.empty()) {
                    tool_msg.content = "[call_id: " + tc.id + "] " + tool_msg.content;
                }
                memory_.add(tool_msg);
                continue;
            }

            // ── User Reply: pre-execution check (Exec/Edit/Always mode) ──

            auto tool_ptr = registry_.find_tool(tc.name);
            if (tool_ptr) {
                LOG_INFO(u8"🛠️Tool", "Executing: " + tc.name + " with args: " + std::to_string(tc.arguments.size()) + " bytes");

                // Show preview before execution
                try {
                    tool_ptr->show_arguments(tc.arguments);
                    tool_ptr->show_preview(tc.arguments);
                } catch (...) {
                    // If preview fails, continue without it
                }

                // Ask user for confirmation if needed
                if (tool_ptr->needs_user_reply(user_reply_mode_)) {
                    auto reply = prompt_user_reply(tc.name, tc.arguments);
                    if (reply.action == ReplyAction::No) {
                        LOG_WARN("Tool", "Skipped: " + tc.name);
                        continue;
                    }
                }
            }
            else {
                LOG_ERROR(u8"🛠️Tool", "\nNot found: " + tc.name + " with args: " + tc.arguments);
            }

            // Validate arguments are well-formed JSON before executing
            try {
                auto arg = json::parse(tc.arguments);
            } catch (const json::parse_error& e) {
                LOG_WARN("Tool", "Truncated/invalid JSON arguments for '" + tc.name + "': " + std::string(e.what()));
                ChatMessage tool_msg{"tool",
                    "[Error] Tool call arguments are malformed or truncated: " + std::string(e.what()) +
                    ". The response may have hit the token limit. For large files, use edit_file for targeted changes instead of write_file.",
                    tc.name};
                if (!tc.id.empty()) {
                    tool_msg.content = "[call_id: " + tc.id + "] " + tool_msg.content;
                }
                memory_.add(tool_msg);
                continue;
            }

            std::string result = registry_.execute(tc.name, tc.arguments);

            // Add tool response to memory
            ChatMessage tool_msg{"tool", result, tc.name};
            if (!tc.id.empty()) {
                tool_msg.content = "[call_id: " + tc.id + "] " + tool_msg.content;
            }
            memory_.add(tool_msg);

            LOG_INFO(u8"🛠️Tool", "Result: " + std::to_string(result.size()) + " bytes\n");
            if (tool_ptr) {
                tool_ptr->show_result(result);
            }
        }

        // Loop again - LLM will see tool results and decide next action
    }

    // Safety fallback after max iterations
    ChatResponse fallback{"[Max iterations reached. Stopping.]"};
    fallback.prompt_tokens = total_prompt;
    fallback.completion_tokens = total_completion;
    return fallback;
}

// ── Advanced Pipeline: Plan → Execute (with Reflection + Multi-Agent) ──

std::string Agent::run_planned(const std::string& user_input, ChatResponse& resp, TokenCallback on_token) {
    TaskPlanner planner(llm_);
    SelfReflector reflector(llm_);
    //MultiAgent multi_agent(llm_.get_base_url());

    // Step 1: Generate a plan
    LOG_INFO("Planner", "\nGenerating task plan...");
    const auto& context = memory_.get_messages();
    Plan plan = planner.generate_plan(resp.content, context);

    if (plan.steps.empty()) {
        // Planning failed - fall back to normal execution.
        LOG_WARN("Planner", "No steps generated, falling back to direct execution.");
        return resp.content;
    }

    // Display the plan
    LOG_INFO("Planner", "\nPlan for: " + plan.overall_goal);
    for (const auto& step : plan.steps) {
        LOG_INFO("Planner", "  Step " + std::to_string(step.id) + ": " + step.description);
    }

    // Step 2: Execute each step with reflection and optional multi-agent routing.
    // Use a while loop so that replanning (which replaces plan.steps) doesn't
    // invalidate the iterator of a range-based for loop.
    std::vector<StepResult> completed_steps;
    std::ostringstream final_result;

    size_t step_index = 0;
    while (step_index < plan.steps.size()) {
        auto& step = plan.steps[step_index];
        step.status = "in_progress";
        LOG_INFO("Planner", "\nExecuting Step " + std::to_string(step.id) + ": " + step.description);

        // Execute the step using multi-agent or direct agent.
        std::string step_output;
        // Use the main agent's reasoning loop for this step.
        ChatMessage user_msg{"user", step.description, ""};
        memory_.add(user_msg);

        std::string step_result;
        auto step_callback = [&step_result, &on_token](const std::string& token, bool is_reasoning) -> bool {
            step_result += token;
            if (on_token) return on_token(token, is_reasoning);
            return true;
        };

        ChatResponse resp = reasoning_loop_stream(step.description, step_callback);
        step_output = resp.content;
        memory_.add(ChatMessage{"assistant", step_output, ""});        

        // Self-Reflection: review the output and retry if needed.
        bool step_success = true;
        std::string error_msg;

        if (self_reflection_) {
            auto reflection = reflector.review(step.description, step_output);

            if (reflection.needs_correction) {
                LOG_WARN("Reflection", "\nIssues found:");
                LOG_WARN("Reflection", "  " + reflection.feedback.substr(0, 300));

                // Retry with feedback.
                for (int retry = 0; retry < max_reflection_retries_; ++retry) {
                    LOG_INFO("Reflection", "\nRetry " + std::to_string(retry + 1) + "/" + std::to_string(max_reflection_retries_));

                    std::string correction_task = step.description +
                        "\n\nPrevious attempt had issues. Fix the following:\n" + reflection.feedback;

                    ChatMessage fix_msg{"user", correction_task, ""};
                    memory_.add(fix_msg);
                    ChatResponse fix_resp = reasoning_loop_stream(user_input, on_token);
                    step_output = fix_resp.content;
                    memory_.add(ChatMessage{"assistant", step_output, ""});

                    // Re-review after correction.
                    auto re_reflection = reflector.review(step.description, step_output);
                    if (!re_reflection.needs_correction) {
                        LOG_INFO("Reflection", "Correction accepted.");
                        break;
                    }

                    reflection = re_reflection; // continue with latest feedback.
                }

                // If still failing after all retries, mark as failed but keep the output.
                if (reflection.needs_correction) {
                    step_success = false;
                    error_msg = "Still has issues after " + std::to_string(max_reflection_retries_) +
                                " retries: " + reflection.feedback.substr(0, 200);
                    LOG_ERROR("Reflection", "Step still has issues after max retries.");
                }
            } else {
                LOG_INFO("Reflection", "Step passed quality check.");
            }
        }

        step.status = step_success ? "completed" : "failed";
        step.result = step_output.substr(0, 500); // store a summary.

        completed_steps.push_back({step.id, step.description, step_output, step_success, error_msg});

        ++step_index;

        if (!step_success) {
            // Attempt to re-plan from this point.
            LOG_WARN("Planner", "\nStep " + std::to_string(step.id) + " failed, attempting replan...");
            Plan new_plan = planner.replan(plan.overall_goal, completed_steps, error_msg);

            if (!new_plan.steps.empty()) {
                LOG_INFO("Planner", "Replanned steps:");
                for (const auto& ns : new_plan.steps) {
                    LOG_INFO("Planner", "  Step " + std::to_string(ns.id) + ": " + ns.description);
                }

                // Replace remaining steps with the replan and restart from index 0.
                plan.steps = std::move(new_plan.steps);
                step_index = 0;
            } else {
                LOG_ERROR("Planner", "Replan failed, continuing with next step.");
            }
        }
    }

    // Step 3: Assemble final result.
    final_result << "## Task Completed\n\n";
    final_result << "**Goal:** " << plan.overall_goal << "\n\n";
    final_result << "**Steps executed:**\n\n";

    for (const auto& sr : completed_steps) {
        final_result << "### Step " << sr.id << ": " << sr.description << "\n\n";
        if (sr.success) {
            std::string preview = sr.output;
            if (preview.size() > 1000) preview.resize(1000);
            final_result << preview << "\n\n";
        } else {
            final_result << "**[FAILED]** " << sr.error_message << "\n\n";
        }
    }

    // Add the assembled result to memory.
    ChatMessage user_msg{"user", user_input, ""};
    memory_.add(user_msg);
    memory_.add(ChatMessage{"assistant", final_result.str(), ""});

    return final_result.str();
}

} // namespace agent
