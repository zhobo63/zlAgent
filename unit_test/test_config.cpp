#include "pch.h"
#include "unit_test.h"
#include "config.h"
#include "safety_guard.h"

using namespace agent;
namespace fs = std::filesystem;
using json = nlohmann::json;

static void safe_remove_all(const std::string& path)
{
    try {
        if (fs::exists(path)) fs::remove_all(path);
    } catch (...) {}
}

// ============================================================
// IniParser tests
// ============================================================

void test_ini_parser(UnitReport& parent)
{
    UnitReport unit("ini_parser");
    LOG_INFO("ini_parser", "entry");

    // --- Test 1: Basic parse - section and key-value pairs ---
    {
        LOG_INFO("ini_parser", "basic_parse");
        std::string dir = "test_ip_basic_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        // Create test INI file
        {
            std::ofstream f(fs::path(dir) / "test.ini");
            f << "[section1]\n";
            f << "key1 = value1\n";
            f << "key2 = value2\n";
            f << "\n";
            f << "[section2]\n";
            f << "key3 = value3\n";
        }

        auto result = IniParser::parse((fs::path(dir) / "test.ini").string());
        UNIT_TEST("has_section1", result.find("section1") != result.end());
        UNIT_TEST("has_section2", result.find("section2") != result.end());
        UNIT_TEST("key1_value", result["section1"]["key1"] == "value1");
        UNIT_TEST("key2_value", result["section1"]["key2"] == "value2");
        UNIT_TEST("key3_value", result["section2"]["key3"] == "value3");

        safe_remove_all(dir);
    }

    // --- Test 2: Parse with comments and whitespace ---
    {
        LOG_INFO("ini_parser", "parse_with_comments_and_whitespace");
        std::string dir = "test_ip_ws_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.ini");
            f << "# comment line\n";
            f << "; another comment\n";
            f << "[section]\n";
            f << "  key1 = value1 # inline comment\n";
            f << "key2 = value2; semicolon comment\n";
            f << "   key3   =   value3   \n";
        }

        auto result = IniParser::parse((fs::path(dir) / "test.ini").string());
        UNIT_TEST("section_exists", result.find("section") != result.end());
        UNIT_TEST("key1_no_inline_comment", result["section"]["key1"] == "value1");
        UNIT_TEST("key2_semicolon_comment_stripped", result["section"]["key2"] == "value2");
        UNIT_TEST("key3_trimmed_value", result["section"]["key3"] == "value3");

        safe_remove_all(dir);
    }

    // --- Test 3: Parse with empty section (no keys) ---
    {
        LOG_INFO("ini_parser", "parse_empty_section");
        std::string dir = "test_ip_es_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.ini");
            f << "[empty_section]\n";
            f << "\n";
            f << "[non_empty]\n";
            f << "key = value\n";
        }

        auto result = IniParser::parse((fs::path(dir) / "test.ini").string());
        UNIT_TEST("empty_section_exists", result.find("empty_section") != result.end());
        UNIT_TEST("non_empty_has_key", result["non_empty"].find("key") != result["non_empty"].end());

        safe_remove_all(dir);
    }

    // --- Test 4: Parse non-existent file returns empty map ---
    {
        LOG_INFO("ini_parser", "parse_nonexistent_file");
        std::string dir = "test_ip_nf_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        auto result = IniParser::parse((fs::path(dir) / "nonexistent.ini").string());
        UNIT_TEST("empty_result", result.empty());

        safe_remove_all(dir);
    }

    // --- Test 5: Parse with no sections (only key-value pairs at top level) ---
    {
        LOG_INFO("ini_parser", "parse_no_sections");
        std::string dir = "test_ip_ns_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.ini");
            f << "key1 = value1\n";
            f << "key2 = value2\n";
        }

        auto result = IniParser::parse((fs::path(dir) / "test.ini").string());
        // Keys without section go to empty string key
        UNIT_TEST("has_empty_section", result.find("") != result.end());
        UNIT_TEST("key1_value", result[""]["key1"] == "value1");
        UNIT_TEST("key2_value", result[""]["key2"] == "value2");

        safe_remove_all(dir);
    }

    // --- Test 6: Parse with special characters in values ---
    {
        LOG_INFO("ini_parser", "parse_special_chars");
        std::string dir = "test_ip_sc_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.ini");
            f << "[section]\n";
            f << "key1 = value with spaces\n";
            f << "key2 = value=with=equals\n";
            f << "key3 = value;with;semicolons#and#hashes\n";
        }

        auto result = IniParser::parse((fs::path(dir) / "test.ini").string());
        UNIT_TEST("spaces_preserved", result["section"]["key1"] == "value with spaces");
        UNIT_TEST("equals_in_value", result["section"]["key2"] == "value=with=equals");
        UNIT_TEST("semicolons_and_hashes_stripped", result["section"]["key3"] == "value");

        safe_remove_all(dir);
    }

    // --- Test 7: Parse with empty value ---
    {
        LOG_INFO("ini_parser", "parse_empty_value");
        std::string dir = "test_ip_ev_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.ini");
            f << "[section]\n";
            f << "key1 =\n";
            f << "key2 =  \n";
        }

        auto result = IniParser::parse((fs::path(dir) / "test.ini").string());
        UNIT_TEST("empty_value", result["section"]["key1"] == "");
        UNIT_TEST("whitespace_only_becomes_empty", result["section"]["key2"] == "");

        safe_remove_all(dir);
    }

    // --- Test 8: Parse with duplicate keys (last one wins) ---
    {
        LOG_INFO("ini_parser", "parse_duplicate_keys");
        std::string dir = "test_ip_dk_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.ini");
            f << "[section]\n";
            f << "key = first_value\n";
            f << "key = second_value\n";
        }

        auto result = IniParser::parse((fs::path(dir) / "test.ini").string());
        UNIT_TEST("duplicate_key_last_wins", result["section"]["key"] == "second_value");

        safe_remove_all(dir);
    }

    // --- Test 9: Update existing key in existing section ---
    {
        LOG_INFO("ini_parser", "update_existing_key");
        std::string dir = "test_ipuk_ek_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        // Create test INI file
        {
            std::ofstream f(fs::path(dir) / "test.ini");
            f << "[section]\n";
            f << "key1 = old_value\n";
            f << "key2 = value2\n";
        }

        bool result = IniParser::update_key((fs::path(dir) / "test.ini").string(), "section", "key1", "new_value");
        UNIT_TEST("update_succeeded", result);

        // Verify the update
        auto parsed = IniParser::parse((fs::path(dir) / "test.ini").string());
        UNIT_TEST("key_updated", parsed["section"]["key1"] == "new_value");
        UNIT_TEST("other_key_unchanged", parsed["section"]["key2"] == "value2");

        safe_remove_all(dir);
    }

    // --- Test 10: Add new key to existing section ---
    {
        LOG_INFO("ini_parser", "add_new_key");
        std::string dir = "test_ipuk_nk_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.ini");
            f << "[section]\n";
            f << "key1 = value1\n";
        }

        bool result = IniParser::update_key((fs::path(dir) / "test.ini").string(), "section", "new_key", "new_value");
        UNIT_TEST("add_succeeded", result);

        auto parsed = IniParser::parse((fs::path(dir) / "test.ini").string());
        UNIT_TEST("new_key_exists", parsed["section"].find("new_key") != parsed["section"].end());
        UNIT_TEST("new_key_value", parsed["section"]["new_key"] == "new_value");
        UNIT_TEST("old_key_unchanged", parsed["section"]["key1"] == "value1");

        safe_remove_all(dir);
    }

    // --- Test 11: Add new section with key ---
    {
        LOG_INFO("ini_parser", "add_new_section");
        std::string dir = "test_ipuk_ns_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.ini");
            f << "[section1]\n";
            f << "key1 = value1\n";
        }

        bool result = IniParser::update_key((fs::path(dir) / "test.ini").string(), "new_section", "key2", "value2");
        UNIT_TEST("add_succeeded", result);

        auto parsed = IniParser::parse((fs::path(dir) / "test.ini").string());
        UNIT_TEST("new_section_exists", parsed.find("new_section") != parsed.end());
        UNIT_TEST("new_section_key_value", parsed["new_section"]["key2"] == "value2");
        UNIT_TEST("old_section_unchanged", parsed["section1"]["key1"] == "value1");

        safe_remove_all(dir);
    }

    // --- Test 12: Update key in non-existent file (creates new file) ---
    {
        LOG_INFO("ini_parser", "update_nonexistent_file_creates_new");
        std::string dir = "test_ipuk_nf_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        bool result = IniParser::update_key((fs::path(dir) / "new.ini").string(), "section", "key", "value");
        UNIT_TEST("file_created", result);
        UNIT_TEST("file_exists", fs::exists(fs::path(dir) / "new.ini"));

        auto parsed = IniParser::parse((fs::path(dir) / "new.ini").string());
        UNIT_TEST("key_value_correct", parsed["section"]["key"] == "value");

        safe_remove_all(dir);
    }

    // --- Test 13: Update key in file that cannot be opened for writing (read-only) ---
    {
        LOG_INFO("ini_parser", "update_readonly_file_fails");
        std::string dir = "test_ipuk_ro_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        // Create a file and make it read-only (on Windows)
        {
            std::ofstream f(fs::path(dir) / "readonly.ini");
            f << "[section]\n";
            f << "key = value\n";
        }
#ifdef _WIN32
        // On Windows, we can't easily make a file read-only in unit tests without admin rights.
        // Skip this test on Windows as it requires elevated privileges.
#else
        chmod((fs::path(dir) / "readonly.ini").string().c_str(), 0444);
        bool result = IniParser::update_key((fs::path(dir) / "readonly.ini").string(), "section", "key", "new_value");
        UNIT_TEST("update_fails_on_readonly", !result);
#endif

        safe_remove_all(dir);
    }

    // --- Test 14: Update key with empty section name ---
    {
        LOG_INFO("ini_parser", "update_empty_section_name");
        std::string dir = "test_ipuk_es_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        bool result = IniParser::update_key((fs::path(dir) / "test.ini").string(), "", "key", "value");
        UNIT_TEST("update_succeeded", result);

        auto parsed = IniParser::parse((fs::path(dir) / "test.ini").string());
        UNIT_TEST("empty_section_created", parsed.find("") != parsed.end());
        UNIT_TEST("key_value_correct", parsed[""]["key"] == "value");

        safe_remove_all(dir);
    }

    // --- Test 15: Update key with empty value ---
    {
        LOG_INFO("ini_parser", "update_empty_value");
        std::string dir = "test_ipuk_ev_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.ini");
            f << "[section]\n";
            f << "key = old_value\n";
        }

        bool result = IniParser::update_key((fs::path(dir) / "test.ini").string(), "section", "key", "");
        UNIT_TEST("update_succeeded", result);

        auto parsed = IniParser::parse((fs::path(dir) / "test.ini").string());
        UNIT_TEST("value_is_empty", parsed["section"]["key"] == "");

        safe_remove_all(dir);
    }

    // --- Test 16: Update key with special characters in value ---
    {
        LOG_INFO("ini_parser", "update_special_chars_value");
        std::string dir = "test_ipuk_sc_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.ini");
            f << "[section]\n";
            f << "key = old_value\n";
        }

        bool result = IniParser::update_key((fs::path(dir) / "test.ini").string(), "section", "key", "value with spaces; and # hash");
        UNIT_TEST("update_succeeded", result);

        auto parsed = IniParser::parse((fs::path(dir) / "test.ini").string());
        // Note: the parser strips inline comments, so value will be "value with spaces"
        UNIT_TEST("special_chars_in_value", parsed["section"]["key"] == "value with spaces");

        safe_remove_all(dir);
    }

    parent.report.push_back(unit);
}

// ============================================================
// Config class tests (parse_bool, load, save)
// ============================================================

void test_config_class(UnitReport& parent)
{
    UnitReport unit("config_class");
    LOG_INFO("config_class", "entry");

    // --- parse_bool: Test 1: Parse true values ---
    {
        LOG_INFO("config_class", "parse_true_values");
        UNIT_TEST("true_string", Config::parse_bool("true") == true);
        UNIT_TEST("True_string", Config::parse_bool("True") == true);
        UNIT_TEST("TRUE_string", Config::parse_bool("TRUE") == true);
        UNIT_TEST("one_number", Config::parse_bool("1") == true);
        UNIT_TEST("yes_string", Config::parse_bool("yes") == true);
        UNIT_TEST("Yes_string", Config::parse_bool("Yes") == true);
    }

    // --- parse_bool: Test 2: Parse false values ---
    {
        LOG_INFO("config_class", "parse_false_values");
        UNIT_TEST("false_string", Config::parse_bool("false") == false);
        UNIT_TEST("False_string", Config::parse_bool("False") == false);
        UNIT_TEST("FALSE_string", Config::parse_bool("FALSE") == false);
        UNIT_TEST("zero_number", Config::parse_bool("0") == false);
        UNIT_TEST("no_string", Config::parse_bool("no") == false);
        UNIT_TEST("No_string", Config::parse_bool("No") == false);
    }

    // --- parse_bool: Test 3: Parse invalid values with default true ---
    {
        LOG_INFO("config_class", "parse_invalid_default_true");
        UNIT_TEST("invalid_returns_default_true", Config::parse_bool("maybe", true) == true);
        UNIT_TEST("empty_string_returns_default_true", Config::parse_bool("", true) == true);
        UNIT_TEST("number_2_returns_default_true", Config::parse_bool("2", true) == true);
    }

    // --- parse_bool: Test 4: Parse invalid values with default false ---
    {
        LOG_INFO("config_class", "parse_invalid_default_false");
        UNIT_TEST("invalid_returns_default_false", Config::parse_bool("maybe", false) == false);
        UNIT_TEST("empty_string_returns_default_false", Config::parse_bool("", false) == false);
    }

    // --- parse_bool: Test 5: Parse with whitespace in value ---
    {
        LOG_INFO("config_class", "parse_with_whitespace");
        // Note: parse_bool does NOT trim, so " true" is not recognized as true
        UNIT_TEST("whitespace_before_true_returns_default_false", Config::parse_bool(" true") == false);
        UNIT_TEST("whitespace_after_true_returns_default_false", Config::parse_bool("true ") == false);
    }

    // --- parse_bool: Test 6: Parse with default value parameter ---
    {
        LOG_INFO("config_class", "parse_with_default_parameter");
        UNIT_TEST("default_true_when_invalid", Config::parse_bool("invalid", true) == true);
        UNIT_TEST("default_false_when_invalid", Config::parse_bool("invalid", false) == false);
    }

    // --- parse_bool: Test 7: Parse empty string with default ---
    {
        LOG_INFO("config_class", "parse_empty_string_with_default");
        UNIT_TEST("empty_with_true_default", Config::parse_bool("", true) == true);
        UNIT_TEST("empty_with_false_default", Config::parse_bool("", false) == false);
    }

    // --- parse_bool: Test 8: Parse case-insensitive values ---
    {
        LOG_INFO("config_class", "parse_case_insensitive");
        UNIT_TEST("TRUE_all_caps", Config::parse_bool("TRUE") == true);
        UNIT_TEST("FALSE_all_caps", Config::parse_bool("FALSE") == false);
        UNIT_TEST("YES_all_caps", Config::parse_bool("YES") == true);
        UNIT_TEST("NO_all_caps", Config::parse_bool("NO") == false);
    }

    // --- load: Test 1: Load from non-existent file returns defaults ---
    {
        LOG_INFO("config_class", "load_nonexistent_file_returns_defaults");
        std::string dir = "test_cl_nf_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        auto cfg = Config::load((fs::path(dir) / "nonexistent.ini").string());
        UNIT_TEST("llm_url_default", cfg.llm.url == "http://127.0.0.1:1234");
        UNIT_TEST("llm_model_default", cfg.llm.model == "local");
        UNIT_TEST("llm_temperature_default", cfg.llm.temperature == 0.2);
        UNIT_TEST("llm_max_tokens_default", cfg.llm.max_tokens == 16384);
        UNIT_TEST("memory_max_messages_default", cfg.memory.max_messages == 50);
        UNIT_TEST("agent_language_default", cfg.agent_.language == "multi");

        safe_remove_all(dir);
    }

    // --- load: Test 2: Load from empty file returns defaults ---
    {
        LOG_INFO("config_class", "load_empty_file_returns_defaults");
        std::string dir = "test_cl_ef_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        // Create empty file
        {
            std::ofstream f(fs::path(dir) / "empty.ini");
        }

        auto cfg = Config::load((fs::path(dir) / "empty.ini").string());
        UNIT_TEST("llm_url_default", cfg.llm.url == "http://127.0.0.1:1234");
        UNIT_TEST("agent_language_default", cfg.agent_.language == "multi");

        safe_remove_all(dir);
    }

    // --- load: Test 3: Load with all sections populated ---
    {
        LOG_INFO("config_class", "load_with_all_sections");
        std::string dir = "test_cl_as_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        // Create INI file with all sections
        {
            std::ofstream f(fs::path(dir) / "full.ini");
            f << "[llm]\n";
            f << "url = http://example.com:8080\n";
            f << "model = gpt-4\n";
            f << "temperature = 0.7\n";
            f << "max_tokens = 32768\n";
            f << "\n";
            f << "[memory]\n";
            f << "max_messages = 100\n";
            f << "long_term_enabled = true\n";
            f << "store_dir = .zlagent/memory2\n";
            f << "max_sessions = 200\n";
            f << "inject_facts_to_prompt = false\n";
            f << "auto_extract_facts = false\n";
            f << "\n";
            f << "[agent]\n";
            f << "max_iterations = 20\n";
            f << "language = cpp\n";
            f << "auto_detect_language = false\n";
            f << "prompt_file = prompts/system.md\n";
            f << "user_reply_mode = always\n";
            f << "\n";
            f << "[features]\n";
            f << "task_planning = false\n";
            f << "self_reflection = false\n";
            f << "multi_agent = true\n";
            f << "max_reflection_retries = 5\n";
            f << "\n";
            f << "[plugins]\n";
            f << "directory = my_plugins\n";
            f << "\n";
            f << "[local_tools]\n";
            f << "enabled = false\n";
            f << "\n";
            f << "[logging]\n";
            f << "level = debug\n";
            f << "\n";
            f << "[safety]\n";
            f << "dangerous_tool_confirmation = false\n";
            f << "path_whitelist = /home/user, /tmp\n";
            f << "working_directory = /home/user/projects\n";
            f << "strict_mode = true\n";
            f << "skill_content_check = false\n";
            f << "input_filter = false\n";
            f << "\n";
            f << "[rag]\n";
            f << "enabled = true\n";
            f << "embedding_backend = lm_studio\n";
            f << "embedding_mode = text-embedding-3-large\n";
            f << "store_path = kb.json\n";
            f << "top_k = 10\n";
            f << "min_score = 0.5\n";
            f << "knowledge_dirs = docs, wiki\n";
            f << "\n";
            f << "[terminal_commands]\n";
            f << "enabled = false\n";
            f << "direct = ls, pwd\n";
            f << "confirm = rm, cp\n";
            f << "ask_unknown = true\n";
            f << "\n";
            f << "[telegram]\n";
            f << "enabled = true\n";
            f << "bot_token = 123456:ABCdefGHIjklMNOpqrsTUVwxyz\n";
            f << "poll_timeout_sec = 60\n";
            f << "max_updates_per_poll = 50\n";
            f << "allowed_chat_ids = 12345, 67890\n";
        }

        auto cfg = Config::load((fs::path(dir) / "full.ini").string());

        // LLM section
        UNIT_TEST("llm_url", cfg.llm.url == "http://example.com:8080");
        UNIT_TEST("llm_model", cfg.llm.model == "gpt-4");
        UNIT_TEST("llm_temperature", cfg.llm.temperature == 0.7);
        UNIT_TEST("llm_max_tokens", cfg.llm.max_tokens == 32768);

        // Memory section
        UNIT_TEST("memory_max_messages", cfg.memory.max_messages == 100);
        UNIT_TEST("memory_long_term_enabled", cfg.memory.long_term_enabled == true);
        UNIT_TEST("memory_store_dir", cfg.memory.store_dir == ".zlagent/memory2");
        UNIT_TEST("memory_max_sessions", cfg.memory.max_sessions == 200);
        UNIT_TEST("memory_inject_facts", cfg.memory.inject_facts_to_prompt == false);
        UNIT_TEST("memory_auto_extract", cfg.memory.auto_extract_facts == false);

        // Agent section
        UNIT_TEST("agent_max_iterations", cfg.agent_.max_iterations == 20);
        UNIT_TEST("agent_language", cfg.agent_.language == "cpp");
        UNIT_TEST("agent_auto_detect", cfg.agent_.auto_detect_language == false);
        UNIT_TEST("agent_prompt_file", cfg.agent_.prompt_file == "prompts/system.md");
        UNIT_TEST("agent_user_reply_mode", cfg.agent_.user_reply_mode == "always");

        // Features section
        UNIT_TEST("features_task_planning", cfg.features.task_planning == false);
        UNIT_TEST("features_self_reflection", cfg.features.self_reflection == false);
        UNIT_TEST("features_multi_agent", cfg.features.multi_agent == true);
        UNIT_TEST("features_max_reflection_retries", cfg.features.max_reflection_retries == 5);

        // Plugins section
        UNIT_TEST("plugins_directory", cfg.plugins.directory == "my_plugins");

        // Local tools section
        UNIT_TEST("local_tools_enabled", cfg.local_tools.enabled == false);

        // Logging section
        UNIT_TEST("logging_level", cfg.logging.level == "debug");

        // Safety section
        UNIT_TEST("safety_dangerous_confirmation", cfg.safety.dangerous_tool_confirmation == false);
        UNIT_TEST("safety_path_whitelist_size", cfg.safety.path_whitelist.size() == 2);
        UNIT_TEST("safety_path_whitelist_0", cfg.safety.path_whitelist[0] == "/home/user");
        UNIT_TEST("safety_path_whitelist_1", cfg.safety.path_whitelist[1] == "/tmp");
        UNIT_TEST("safety_working_directory", cfg.safety.working_directory == "/home/user/projects");
        UNIT_TEST("safety_strict_mode", cfg.safety.strict_mode == true);
        UNIT_TEST("safety_skill_content_check", cfg.safety.skill_content_check == false);
        UNIT_TEST("safety_input_filter", cfg.safety.input_filter == false);

        // RAG section
        UNIT_TEST("rag_enabled", cfg.rag.enabled == true);
        UNIT_TEST("rag_embedding_backend", cfg.rag.embedding_backend == "lm_studio");
        UNIT_TEST("rag_embedding_model", cfg.rag.embedding_model == "text-embedding-3-large");
        UNIT_TEST("rag_store_path", cfg.rag.store_path == "kb.json");
        UNIT_TEST("rag_top_k", cfg.rag.top_k == 10);
        UNIT_TEST("rag_min_score", cfg.rag.min_score == 0.5f);
        UNIT_TEST("rag_knowledge_dirs_size", cfg.rag.knowledge_dirs.size() == 2);
        UNIT_TEST("rag_knowledge_dirs_0", cfg.rag.knowledge_dirs[0] == "docs");
        UNIT_TEST("rag_knowledge_dirs_1", cfg.rag.knowledge_dirs[1] == "wiki");

        // Terminal commands section
        UNIT_TEST("terminal_commands_enabled", cfg.terminal_commands.enabled == false);
        UNIT_TEST("terminal_direct_size", cfg.terminal_commands.direct_commands.size() == 2);
        UNIT_TEST("terminal_direct_0", cfg.terminal_commands.direct_commands[0] == "ls");
        UNIT_TEST("terminal_direct_1", cfg.terminal_commands.direct_commands[1] == "pwd");
        UNIT_TEST("terminal_confirm_size", cfg.terminal_commands.confirm_commands.size() == 2);
        UNIT_TEST("terminal_confirm_0", cfg.terminal_commands.confirm_commands[0] == "rm");
        UNIT_TEST("terminal_confirm_1", cfg.terminal_commands.confirm_commands[1] == "cp");
        UNIT_TEST("terminal_ask_unknown", cfg.terminal_commands.ask_unknown == true);

        // Telegram section
        UNIT_TEST("telegram_enabled", cfg.telegram.enabled == true);
        UNIT_TEST("telegram_bot_token", cfg.telegram.bot_token == "123456:ABCdefGHIjklMNOpqrsTUVwxyz");
        UNIT_TEST("telegram_poll_timeout", cfg.telegram.poll_timeout_sec == 60);
        UNIT_TEST("telegram_max_updates", cfg.telegram.max_updates_per_poll == 50);
        UNIT_TEST("telegram_allowed_chat_ids_size", cfg.telegram.allowed_chat_ids.size() == 2);
        UNIT_TEST("telegram_allowed_chat_ids_0", cfg.telegram.allowed_chat_ids[0] == 12345);
        UNIT_TEST("telegram_allowed_chat_ids_1", cfg.telegram.allowed_chat_ids[1] == 67890);

        safe_remove_all(dir);
    }

    // --- load: Test 4: Load with partial sections (only some sections present) ---
    {
        LOG_INFO("config_class", "load_partial_sections");
        std::string dir = "test_cl_ps_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        // Create INI file with only llm and agent sections
        {
            std::ofstream f(fs::path(dir) / "partial.ini");
            f << "[llm]\n";
            f << "url = http://custom.com:9000\n";
            f << "model = llama-2\n";
            f << "temperature = 0.5\n";
            f << "max_tokens = 8192\n";
            f << "\n";
            f << "[agent]\n";
            f << "language = python\n";
        }

        auto cfg = Config::load((fs::path(dir) / "partial.ini").string());

        // Verify loaded values
        UNIT_TEST("llm_url_custom", cfg.llm.url == "http://custom.com:9000");
        UNIT_TEST("llm_model_custom", cfg.llm.model == "llama-2");
        UNIT_TEST("llm_temperature_custom", cfg.llm.temperature == 0.5);
        UNIT_TEST("llm_max_tokens_custom", cfg.llm.max_tokens == 8192);
        UNIT_TEST("agent_language_custom", cfg.agent_.language == "python");

        // Verify defaults for missing sections
        UNIT_TEST("memory_default_max_messages", cfg.memory.max_messages == 50);
        UNIT_TEST("features_default_task_planning", cfg.features.task_planning == true);
        UNIT_TEST("rag_default_enabled", cfg.rag.enabled == false);

        safe_remove_all(dir);
    }

    // --- load: Test 5: Load with boolean values in various formats ---
    {
        LOG_INFO("config_class", "load_boolean_various_formats");
        std::string dir = "test_cl_bf_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        // Create INI file with various boolean formats
        {
            std::ofstream f(fs::path(dir) / "bools.ini");
            f << "[features]\n";
            f << "task_planning = yes\n";
            f << "self_reflection = no\n";
            f << "multi_agent = 1\n";
            f << "max_reflection_retries = 3\n";
        }

        auto cfg = Config::load((fs::path(dir) / "bools.ini").string());
        UNIT_TEST("task_planning_yes", cfg.features.task_planning == true);
        UNIT_TEST("self_reflection_no", cfg.features.self_reflection == false);
        UNIT_TEST("multi_agent_1", cfg.features.multi_agent == true);
        UNIT_TEST("max_reflection_retries", cfg.features.max_reflection_retries == 3);

        safe_remove_all(dir);
    }

    // --- load: Test 6: Load with CSV list values ---
    {
        LOG_INFO("config_class", "load_csv_list_values");
        std::string dir = "test_cl_cv_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        // Create INI file with CSV values
        {
            std::ofstream f(fs::path(dir) / "csv.ini");
            f << "[safety]\n";
            f << "path_whitelist = /a, /b, /c\n";
            f << "\n";
            f << "[rag]\n";
            f << "enabled = true\n";
            f << "knowledge_dirs = dir1, dir2, dir3\n";
        }

        auto cfg = Config::load((fs::path(dir) / "csv.ini").string());
        UNIT_TEST("safety_whitelist_size", cfg.safety.path_whitelist.size() == 3);
        UNIT_TEST("safety_whitelist_0", cfg.safety.path_whitelist[0] == "/a");
        UNIT_TEST("safety_whitelist_1", cfg.safety.path_whitelist[1] == "/b");
        UNIT_TEST("safety_whitelist_2", cfg.safety.path_whitelist[2] == "/c");
        UNIT_TEST("rag_knowledge_dirs_size", cfg.rag.knowledge_dirs.size() == 3);
        UNIT_TEST("rag_knowledge_dirs_0", cfg.rag.knowledge_dirs[0] == "dir1");
        UNIT_TEST("rag_knowledge_dirs_1", cfg.rag.knowledge_dirs[1] == "dir2");
        UNIT_TEST("rag_knowledge_dirs_2", cfg.rag.knowledge_dirs[2] == "dir3");

        safe_remove_all(dir);
    }

    // --- load: Test 7: Load with CSV int list values (telegram) ---
    {
        LOG_INFO("config_class", "load_csv_int_list_values");
        std::string dir = "test_cl_cil_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        // Create INI file with CSV int values
        {
            std::ofstream f(fs::path(dir) / "telegram.ini");
            f << "[telegram]\n";
            f << "enabled = true\n";
            f << "bot_token = 123:ABCdef\n";
            f << "poll_timeout_sec = 45\n";
            f << "max_updates_per_poll = 75\n";
            f << "allowed_chat_ids = 100, 200, 300\n";
        }

        auto cfg = Config::load((fs::path(dir) / "telegram.ini").string());
        UNIT_TEST("telegram_enabled", cfg.telegram.enabled == true);
        UNIT_TEST("telegram_bot_token", cfg.telegram.bot_token == "123:ABCdef");
        UNIT_TEST("telegram_poll_timeout", cfg.telegram.poll_timeout_sec == 45);
        UNIT_TEST("telegram_max_updates", cfg.telegram.max_updates_per_poll == 75);
        UNIT_TEST("telegram_chat_ids_size", cfg.telegram.allowed_chat_ids.size() == 3);
        UNIT_TEST("telegram_chat_ids_0", cfg.telegram.allowed_chat_ids[0] == 100);
        UNIT_TEST("telegram_chat_ids_1", cfg.telegram.allowed_chat_ids[1] == 200);
        UNIT_TEST("telegram_chat_ids_2", cfg.telegram.allowed_chat_ids[2] == 300);

        safe_remove_all(dir);
    }

    // --- load: Test 8: Load with invalid boolean values (should use defaults) ---
    {
        LOG_INFO("config_class", "load_invalid_boolean_uses_default");
        std::string dir = "test_cl_ib_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        // Create INI file with invalid boolean values
        {
            std::ofstream f(fs::path(dir) / "invalid_bool.ini");
            f << "[features]\n";
            f << "task_planning = maybe\n";
            f << "self_reflection = 2\n";
            f << "multi_agent = yes\n";
        }

        auto cfg = Config::load((fs::path(dir) / "invalid_bool.ini").string());
        // task_planning default is true, invalid value returns default
        UNIT_TEST("task_planning_invalid_returns_default_true", cfg.features.task_planning == true);
        // self_reflection default is true, invalid value returns default
        UNIT_TEST("self_reflection_invalid_returns_default_true", cfg.features.self_reflection == true);
        // multi_agent default is false, "yes" returns true (valid)
        UNIT_TEST("multi_agent_yes_returns_true", cfg.features.multi_agent == true);

        safe_remove_all(dir);
    }

    // --- save: Test 1: Save default config and verify file exists ---
    {
        LOG_INFO("config_class", "save_default_config_creates_file");
        std::string dir = "test_cs_dc_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        Config cfg; // all defaults
        bool result = Config::save(cfg, (fs::path(dir) / "default.ini").string());
        UNIT_TEST("save_succeeded", result);
        UNIT_TEST("file_exists", fs::exists(fs::path(dir) / "default.ini"));

        // Verify the saved file can be parsed back and matches defaults
        auto loaded = Config::load((fs::path(dir) / "default.ini").string());
        UNIT_TEST("llm_url_after_save", loaded.llm.url == cfg.llm.url);
        UNIT_TEST("agent_language_after_save", loaded.agent_.language == cfg.agent_.language);

        safe_remove_all(dir);
    }

    // --- save: Test 2: Save custom config and verify values round-trip ---
    {
        LOG_INFO("config_class", "save_custom_config_roundtrip");
        std::string dir = "test_cs_cc_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        Config cfg;
        cfg.llm.url = "http://custom.com:8080";
        cfg.llm.model = "gpt-4-turbo";
        cfg.llm.temperature = 0.9;
        cfg.llm.max_tokens = 65536;
        cfg.agent_.language = "rust";
        cfg.features.task_planning = false;
        cfg.rag.enabled = true;

        bool result = Config::save(cfg, (fs::path(dir) / "custom.ini").string());
        UNIT_TEST("save_succeeded", result);

        auto loaded = Config::load((fs::path(dir) / "custom.ini").string());
        UNIT_TEST("llm_url_roundtrip", loaded.llm.url == cfg.llm.url);
        UNIT_TEST("llm_model_roundtrip", loaded.llm.model == cfg.llm.model);
        UNIT_TEST("llm_temperature_roundtrip", loaded.llm.temperature == cfg.llm.temperature);
        UNIT_TEST("llm_max_tokens_roundtrip", loaded.llm.max_tokens == cfg.llm.max_tokens);
        UNIT_TEST("agent_language_roundtrip", loaded.agent_.language == cfg.agent_.language);
        UNIT_TEST("features_task_planning_roundtrip", loaded.features.task_planning == cfg.features.task_planning);
        UNIT_TEST("rag_enabled_roundtrip", loaded.rag.enabled == cfg.rag.enabled);

        safe_remove_all(dir);
    }

    // --- save: Test 3: Save config with CSV lists and verify round-trip ---
    {
        LOG_INFO("config_class", "save_csv_lists_roundtrip");
        std::string dir = "test_cs_cl_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        Config cfg;
        cfg.safety.path_whitelist = {"/home/user", "/tmp", "/var/log"};
        cfg.rag.knowledge_dirs = {"docs", "wiki", "readme"};
        cfg.terminal_commands.direct_commands = {"ls", "pwd", "cat"};
        cfg.telegram.allowed_chat_ids = {100, 200, 300};

        bool result = Config::save(cfg, (fs::path(dir) / "csv.ini").string());
        UNIT_TEST("save_succeeded", result);

        auto loaded = Config::load((fs::path(dir) / "csv.ini").string());
        UNIT_TEST("safety_whitelist_size_roundtrip", loaded.safety.path_whitelist.size() == cfg.safety.path_whitelist.size());
        UNIT_TEST("safety_whitelist_0_roundtrip", loaded.safety.path_whitelist[0] == cfg.safety.path_whitelist[0]);
        UNIT_TEST("rag_knowledge_dirs_size_roundtrip", loaded.rag.knowledge_dirs.size() == cfg.rag.knowledge_dirs.size());
        UNIT_TEST("terminal_direct_size_roundtrip", loaded.terminal_commands.direct_commands.size() == cfg.terminal_commands.direct_commands.size());
        UNIT_TEST("telegram_chat_ids_size_roundtrip", loaded.telegram.allowed_chat_ids.size() == cfg.telegram.allowed_chat_ids.size());

        safe_remove_all(dir);
    }

    // --- save: Test 4: Save to non-writable location (should fail) ---
    {
        LOG_INFO("config_class", "save_to_nonwritable_location_fails");
#ifdef _WIN32
        // On Windows, try saving to a read-only directory or invalid path
        std::string dir = "test_cs_nw_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        Config cfg;
        // Try saving to a non-existent parent directory - should fail on Windows
        bool result = Config::save(cfg, "Z:\\nonexistent\\dir\\config.ini");
        UNIT_TEST("save_to_invalid_path_fails", !result);
#else
        // On Linux, try saving to /root which is typically not writable by regular users
        Config cfg;
        bool result = Config::save(cfg, "/root/nonexistent/config.ini");
        UNIT_TEST("save_to_nonwritable_location_fails", !result);
#endif
    }

    // --- save: Test 5: Save config with boolean values and verify round-trip ---
    {
        LOG_INFO("config_class", "save_boolean_values_roundtrip");
        std::string dir = "test_cs_bv_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        Config cfg;
        cfg.memory.long_term_enabled = true;
        cfg.memory.inject_facts_to_prompt = false;
        cfg.features.task_planning = true;
        cfg.features.multi_agent = false;
        cfg.safety.strict_mode = true;
        cfg.rag.enabled = false;

        bool result = Config::save(cfg, (fs::path(dir) / "bools.ini").string());
        UNIT_TEST("save_succeeded", result);

        auto loaded = Config::load((fs::path(dir) / "bools.ini").string());
        UNIT_TEST("long_term_enabled_roundtrip", loaded.memory.long_term_enabled == cfg.memory.long_term_enabled);
        UNIT_TEST("inject_facts_roundtrip", loaded.memory.inject_facts_to_prompt == cfg.memory.inject_facts_to_prompt);
        UNIT_TEST("task_planning_roundtrip", loaded.features.task_planning == cfg.features.task_planning);
        UNIT_TEST("multi_agent_roundtrip", loaded.features.multi_agent == cfg.features.multi_agent);
        UNIT_TEST("strict_mode_roundtrip", loaded.safety.strict_mode == cfg.safety.strict_mode);
        UNIT_TEST("rag_enabled_roundtrip", loaded.rag.enabled == cfg.rag.enabled);

        safe_remove_all(dir);
    }

    // --- save: Test 6: Save config with numeric values and verify round-trip ---
    {
        LOG_INFO("config_class", "save_numeric_values_roundtrip");
        std::string dir = "test_cs_nv_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        Config cfg;
        cfg.llm.temperature = 0.75;
        cfg.llm.max_tokens = 32768;
        cfg.memory.max_messages = 100;
        cfg.rag.top_k = 10;
        cfg.rag.min_score = 0.4f;

        bool result = Config::save(cfg, (fs::path(dir) / "numeric.ini").string());
        UNIT_TEST("save_succeeded", result);

        auto loaded = Config::load((fs::path(dir) / "numeric.ini").string());
        UNIT_TEST("temperature_roundtrip", loaded.llm.temperature == cfg.llm.temperature);
        UNIT_TEST("max_tokens_roundtrip", loaded.llm.max_tokens == cfg.llm.max_tokens);
        UNIT_TEST("max_messages_roundtrip", loaded.memory.max_messages == cfg.memory.max_messages);
        UNIT_TEST("rag_top_k_roundtrip", loaded.rag.top_k == cfg.rag.top_k);
        UNIT_TEST("rag_min_score_roundtrip", loaded.rag.min_score == cfg.rag.min_score);

        safe_remove_all(dir);
    }

    // --- save: Test 7: Save config with empty string values and verify round-trip ---
    {
        LOG_INFO("config_class", "save_empty_string_values_roundtrip");
        std::string dir = "test_cs_es_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        Config cfg;
        cfg.llm.url = "";
        cfg.agent_.prompt_file = "";
        cfg.safety.working_directory = "";

        bool result = Config::save(cfg, (fs::path(dir) / "empty.ini").string());
        UNIT_TEST("save_succeeded", result);

        auto loaded = Config::load((fs::path(dir) / "empty.ini").string());
        UNIT_TEST("llm_url_empty_roundtrip", loaded.llm.url == cfg.llm.url);
        UNIT_TEST("prompt_file_empty_roundtrip", loaded.agent_.prompt_file == cfg.agent_.prompt_file);
        UNIT_TEST("working_directory_empty_roundtrip", loaded.safety.working_directory == cfg.safety.working_directory);

        safe_remove_all(dir);
    }

    // --- save: Test 8: Save and verify file format (sections are written correctly) ---
    {
        LOG_INFO("config_class", "save_file_format_verification");
        std::string dir = "test_cs_ff_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        Config cfg;
        bool result = Config::save(cfg, (fs::path(dir) / "format.ini").string());
        UNIT_TEST("save_succeeded", result);

        // Read the file and verify format
        std::ifstream f(fs::path(dir) / "format.ini");
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        UNIT_TEST("file_not_empty", !content.empty());
        UNIT_TEST("has_llm_section", content.find("[llm]") != std::string::npos);
        UNIT_TEST("has_memory_section", content.find("[memory]") != std::string::npos);
        UNIT_TEST("has_agent_section", content.find("[agent]") != std::string::npos);
        UNIT_TEST("has_features_section", content.find("[features]") != std::string::npos);

        safe_remove_all(dir);
    }

    parent.report.push_back(unit);
}

// ============================================================
// Entry point for config tests
// ============================================================

void test_config(UnitReport& parent)
{
    auto& sg = SafetyGuard::get_instance();
    sg.set_path_whitelist({fs::current_path().string()});

    UnitReport unit("config");
    LOG_INFO("config", "entry");

    test_ini_parser(unit);
    test_config_class(unit);

    parent.report.push_back(unit);
}