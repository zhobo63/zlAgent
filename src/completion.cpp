#include "pch.h"

#include "completion.h"
#include "isocline.h"
#include "agent.h"
#include "long_term_memory.h"

// Forward declarations for runtime context access
namespace agent {
    class Agent;
    class SkillRegistry;
    class RAGManager;
    class LongTermMemory;
}

using namespace agent;

namespace agent {

// ── Step 1: Basic command completion ───────────────────────────────

/// All available slash commands (null-terminated for ic_add_completions)
static const char* ALL_COMMANDS[] = {
    "/help",
    "/status",
    "/config",
    "/skills",
    "/model",
    "/model-info",
    "/facts",
    "/sessions",
    "/summary",
    "/new",
    "/save",
    "/search-kb",
    "/add-doc",
    "/quit",
    "/exit",
    nullptr,  // null-terminated
};

// ── Context for argument-level completion ──────────────────────────

/// Holds the current command name so inner callbacks know which arguments to complete.
/// Set synchronously before calling ic_complete_word(), read inside the inner callback.
static std::string g_current_command;

// ── Step 2: Argument completers per command ────────────────────────

/// /model — model index completion (1, 2, 3, ...)
static void model_arg_completer(ic_completion_env_t* cenv, const char* word_prefix) {
    auto ag = get_global_agent();
    if (!ag) return;

    auto models = ag->get_llm().list_models();
    for (size_t i = 0; i < models.size(); ++i) {
        std::string idx = std::to_string(i + 1);
        if (strncmp(word_prefix, idx.c_str(), strlen(word_prefix)) == 0) {
            ic_add_completion(cenv, idx.c_str());
        }
    }
}

/// /reply — mode completion (off, exec, edit, always)
static void reply_arg_completer(ic_completion_env_t* cenv, const char* word_prefix) {
    static const char* MODES[] = { "off", "exec", "edit", "always", nullptr };
    ic_add_completions(cenv, word_prefix, MODES);
}

/// /facts — prefix filter completion (list known fact keys)
static void facts_arg_completer(ic_completion_env_t* cenv, const char* word_prefix) {
    auto ltm = get_global_long_term_memory();
    if (!ltm) return;

    auto all_facts = ltm->get_facts("");
    for (const auto& f : all_facts) {
        if (strncmp(word_prefix, f.key.c_str(), strlen(word_prefix)) == 0) {
            ic_add_completion(cenv, f.key.c_str());
        }
    }
}

/// /sessions — n (count) completion with common defaults
static void sessions_arg_completer(ic_completion_env_t* cenv, const char* word_prefix) {
    // If no prefix or digit prefix, suggest common counts
    if (strlen(word_prefix) == 0 || isdigit(word_prefix[0])) {
        static const char* COUNTS[] = { "5", "10", "20", nullptr };
        ic_add_completions(cenv, word_prefix, COUNTS);
    }
}

/// /search-kb — query term completion (suggests indexed topics if available)
static void search_kb_arg_completer(ic_completion_env_t* cenv, const char* /*word_prefix*/) {
    // RAGManager doesn't expose a topic list API yet; no completions for free-form queries.
    // Future: could suggest frequent query terms or document titles.
    (void)cenv;  // unused for now
}

/// /add-doc — file/directory path completion using isocline's built-in filename completer
static void add_doc_arg_completer(ic_completion_env_t* cenv, const char* word_prefix) {
    if (!word_prefix || strlen(word_prefix) == 0) return;

#ifdef _WIN32
    ic_complete_filename(cenv, word_prefix, '\\', nullptr, nullptr);
#else
    ic_complete_filename(cenv, word_prefix, '/', nullptr, nullptr);
#endif
}

/// /config — no argument completion (free-form key display)
static void config_arg_completer(ic_completion_env_t* cenv, const char* /*word_prefix*/) {
    (void)cenv;  // no specific completions for free-form config keys
}

// ── Step 3: Context-aware dispatch ────────────────────────────────

/// Inner callback used by ic_complete_word() — dispatches to the right arg completer.
static void arg_completer(ic_completion_env_t* cenv, const char* word_prefix) {
    if (!word_prefix || strlen(word_prefix) == 0) return;

    if (g_current_command == "model") {
        model_arg_completer(cenv, word_prefix);
    } else if (g_current_command == "reply") {
        reply_arg_completer(cenv, word_prefix);
    } else if (g_current_command == "facts") {
        facts_arg_completer(cenv, word_prefix);
    } else if (g_current_command == "sessions") {
        sessions_arg_completer(cenv, word_prefix);
    } else if (g_current_command == "search-kb") {
        search_kb_arg_completer(cenv, word_prefix);
    } else if (g_current_command == "add-doc") {
        add_doc_arg_completer(cenv, word_prefix);
    } else if (g_current_command == "config") {
        config_arg_completer(cenv, word_prefix);
    }
}

/// Top-level completion callback invoked by isocline when Tab is pressed.
static void on_completion(ic_completion_env_t* cenv, const char* prefix) {
    if (!prefix || strlen(prefix) == 0) {
        // Empty input — show all commands
        //ic_add_completions(cenv, "", ALL_COMMANDS);
        return;
    }

    // Only provide completions for slash-commands (input starts with '/')
    if (prefix[0] != '/') {
        return;
    }

    // Parse the first word to determine the command
    std::string first_word(prefix);
    auto space_pos = first_word.find(' ');
    if (space_pos != std::string::npos) {
        first_word = first_word.substr(0, space_pos);
    }

    bool has_space = strchr(prefix, ' ') != nullptr;

    // ── Completing the command name itself (no arguments yet) ────────
    if (!has_space) {
        // Let isocline handle filtering; pass "/" as prefix so it knows
        // what portion of the input to replace when completing.
        ic_add_completions(cenv, "/", ALL_COMMANDS);
        return;
    }

    // ── Completing an argument (command already typed) ───────────────
    std::string cmd = first_word.substr(1);  // Remove leading '/'
    g_current_command = cmd;

    // Use ic_complete_word to extract just the current argument word and complete it
    ic_complete_word(cenv, prefix, arg_completer, nullptr);
}

void register_completion() {
    // Enable auto-tab: single Tab = unique match completion, double Tab = show all options
    ic_enable_auto_tab(true);
    ic_enable_completion_preview(true);
    //ic_set_default_completer(on_completion, nullptr);
}

} // namespace agent
