#include "pch.h"
#include "unit_test.h"
#include "long_term_memory.h"
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
// LongTermMemory class tests
// ============================================================

void test_long_term_memory_class(UnitReport& parent)
{
    UnitReport unit("long_term_memory_class");
    LOG_INFO("long_term_memory", "entry");

    // --- Test 1: Constructor with default config ---
    {
        LOG_INFO("long_term_memory", "default_constructor");
        LongTermMemory ltm;
        UNIT_TEST("facts_empty", ltm.get_facts().empty());
        UNIT_TEST("sessions_empty", ltm.get_recent_sessions(10).empty());
    }

    // --- Test 2: Constructor with custom config ---
    {
        LOG_INFO("long_term_memory", "custom_config_constructor");
        LongTermMemory::Config cfg;
        cfg.store_dir = "test_ltm_custom_temp";
        cfg.max_sessions = 50;
        cfg.auto_extract_facts = false;

        LongTermMemory ltm(cfg);
        UNIT_TEST("facts_empty", ltm.get_facts().empty());
    }

    // --- Test 3: add_fact — single fact ---
    {
        LOG_INFO("long_term_memory", "add_single_fact");
        LongTermMemory ltm;
        ltm.add_fact("project.build_system", "CMake");

        auto facts = ltm.get_facts();
        UNIT_TEST("one_fact", facts.size() == 1);
        UNIT_TEST("key_matches", facts[0].key == "project.build_system");
        UNIT_TEST("value_matches", facts[0].value == "CMake");
    }

    // --- Test 4: add_fact — multiple facts ---
    {
        LOG_INFO("long_term_memory", "add_multiple_facts");
        LongTermMemory ltm;
        ltm.add_fact("project.build_system", "CMake");
        ltm.add_fact("coding.style", "modern_cpp20");
        ltm.add_fact("user.preference.language", "zh-tw");

        auto facts = ltm.get_facts();
        UNIT_TEST("three_facts", facts.size() == 3);
    }

    // --- Test 5: add_fact — overwrite existing key ---
    {
        LOG_INFO("long_term_memory", "overwrite_existing_fact");
        LongTermMemory ltm;
        ltm.add_fact("project.build_system", "CMake");
        ltm.add_fact("project.build_system", "Meson");

        auto facts = ltm.get_facts();
        UNIT_TEST("still_one_fact", facts.size() == 1);
        UNIT_TEST("value_overwritten", facts[0].value == "Meson");
    }

    // --- Test 6: get_facts — all facts (empty prefix) ---
    {
        LOG_INFO("long_term_memory", "get_all_facts");
        LongTermMemory ltm;
        ltm.add_fact("project.build_system", "CMake");
        ltm.add_fact("coding.style", "modern_cpp20");

        auto facts = ltm.get_facts();
        UNIT_TEST("two_facts", facts.size() == 2);
    }

    // --- Test 7: get_facts — filter by prefix ---
    {
        LOG_INFO("long_term_memory", "get_facts_by_prefix");
        LongTermMemory ltm;
        ltm.add_fact("project.build_system", "CMake");
        ltm.add_fact("project.language", "cpp");
        ltm.add_fact("coding.style", "modern_cpp20");

        auto facts = ltm.get_facts("project.");
        UNIT_TEST("two_project_facts", facts.size() == 2);
        for (const auto& f : facts) {
            UNIT_TEST("key_starts_with_project", f.key.find("project.") == 0);
        }
    }

    // --- Test 8: get_facts — prefix with no matches ---
    {
        LOG_INFO("long_term_memory", "get_facts_no_match_prefix");
        LongTermMemory ltm;
        ltm.add_fact("project.build_system", "CMake");

        auto facts = ltm.get_facts("nonexistent.");
        UNIT_TEST("no_matching_facts", facts.empty());
    }

    // --- Test 9: remove_fact — existing key ---
    {
        LOG_INFO("long_term_memory", "remove_existing_fact");
        LongTermMemory ltm;
        ltm.add_fact("project.build_system", "CMake");
        ltm.add_fact("coding.style", "modern_cpp20");

        ltm.remove_fact("project.build_system");

        auto facts = ltm.get_facts();
        UNIT_TEST("one_fact_remaining", facts.size() == 1);
        UNIT_TEST("remaining_key_correct", facts[0].key == "coding.style");
    }

    // --- Test 10: remove_fact — non-existing key (should not crash) ---
    {
        LOG_INFO("long_term_memory", "remove_nonexistent_fact");
        LongTermMemory ltm;
        ltm.add_fact("project.build_system", "CMake");

        ltm.remove_fact("nonexistent.key");  // should not crash

        auto facts = ltm.get_facts();
        UNIT_TEST("fact_still_there", facts.size() == 1);
    }

    // --- Test 11: remove_fact — on empty memory (should not crash) ---
    {
        LOG_INFO("long_term_memory", "remove_from_empty_memory");
        LongTermMemory ltm;
        ltm.remove_fact("any.key");  // should not crash
        UNIT_TEST("still_empty", ltm.get_facts().empty());
    }

    // --- Test 12: save and load — facts round-trip ---
    {
        LOG_INFO("long_term_memory", "save_load_facts_roundtrip");
        std::string dir = "test_ltm_save_temp";
        safe_remove_all(dir);

        LongTermMemory::Config cfg;
        cfg.store_dir = dir;
        LongTermMemory ltm(cfg);

        ltm.add_fact("project.build_system", "CMake");
        ltm.add_fact("coding.style", "modern_cpp20");
        ltm.add_fact("user.preference.language", "zh-tw");

        ltm.save();

        // Verify files created
        UNIT_TEST("sessions_file_exists", fs::exists(dir + "/sessions.json"));
        UNIT_TEST("facts_file_exists", fs::exists(dir + "/facts.json"));

        // Load into a new instance
        LongTermMemory ltm2(cfg);
        bool loaded = ltm2.load();
        UNIT_TEST("load_succeeded", loaded);

        auto facts = ltm2.get_facts();
        UNIT_TEST("three_facts_loaded", facts.size() == 3);

        // Verify specific values
        for (const auto& f : facts) {
            if (f.key == "project.build_system") {
                UNIT_TEST("build_system_value", f.value == "CMake");
            }
            if (f.key == "coding.style") {
                UNIT_TEST("style_value", f.value == "modern_cpp20");
            }
        }

        safe_remove_all(dir);
    }

    // --- Test 13: save and load — sessions round-trip ---
    {
        LOG_INFO("long_term_memory", "save_load_sessions_roundtrip");
        std::string dir = "test_ltm_sess_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        LongTermMemory::Config cfg;
        cfg.store_dir = dir;
        LongTermMemory ltm(cfg);

        // Manually create sessions by writing JSON directly (since save_session requires LLM)
        {
            json sj;
            json s1, s2;
            s1["timestamp"] = "2025-01-15T14:30:00";
            s1["topic"] = "Implemented RAG vector store";
            s1["summary"] = "Added vector store integration with HNSW index";
            s1["message_count"] = 25;
            s2["timestamp"] = "2025-01-16T10:00:00";
            s2["topic"] = "Fixed memory leak";
            s2["summary"] = "Resolved dangling pointer in session manager";
            s2["message_count"] = 12;
            sj["sessions"] = json::array({s1, s2});

            std::ofstream ofs(dir + "/sessions.json");
            ofs << sj.dump(2);
        }

        // Empty facts file
        {
            std::ofstream ofs(dir + "/facts.json");
            ofs << "{}";
        }

        LongTermMemory ltm2(cfg);
        bool loaded = ltm2.load();
        UNIT_TEST("load_succeeded", loaded);

        auto sessions = ltm2.get_recent_sessions(10);
        UNIT_TEST("two_sessions_loaded", sessions.size() == 2);
        // Newest first: s2 (2025-01-16) comes before s1 (2025-01-15)
        UNIT_TEST("first_topic", sessions.size() > 0 && sessions[0].topic == "Fixed memory leak");
        UNIT_TEST("second_topic", sessions.size() > 1 && sessions[1].topic == "Implemented RAG vector store");
        UNIT_TEST("first_message_count", sessions.size() > 0 && sessions[0].message_count == 12);

        safe_remove_all(dir);
    }

    // --- Test 14: load — non-existent directory returns false ---
    {
        LOG_INFO("long_term_memory", "load_nonexistent_dir");
        std::string dir = "test_ltm_nodir_temp";
        safe_remove_all(dir);

        LongTermMemory::Config cfg;
        cfg.store_dir = dir;
        LongTermMemory ltm(cfg);

        bool loaded = ltm.load();
        UNIT_TEST("load_returns_false", !loaded);
    }

    // --- Test 15: load — empty directory (no json files) returns false ---
    {
        LOG_INFO("long_term_memory", "load_empty_dir");
        std::string dir = "test_ltm_empty_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        LongTermMemory::Config cfg;
        cfg.store_dir = dir;
        LongTermMemory ltm(cfg);

        bool loaded = ltm.load();
        UNIT_TEST("load_returns_false", !loaded);

        safe_remove_all(dir);
    }

    // --- Test 16: load — corrupted JSON (should not crash) ---
    {
        LOG_INFO("long_term_memory", "load_corrupted_json");
        std::string dir = "test_ltm_corrupt_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        // Write corrupted JSON
        {
            std::ofstream ofs(dir + "/sessions.json");
            ofs << "{not valid json}";
        }
        {
            std::ofstream ofs(dir + "/facts.json");
            ofs << "also not valid";
        }

        LongTermMemory::Config cfg;
        cfg.store_dir = dir;
        LongTermMemory ltm(cfg);

        bool loaded = ltm.load();  // should not crash, returns false since no data parsed
        UNIT_TEST("load_returns_false", !loaded);
        UNIT_TEST("no_facts_loaded", ltm.get_facts().empty());

        safe_remove_all(dir);
    }

    // --- Test 17: get_recent_sessions — more requested than available ---
    {
        LOG_INFO("long_term_memory", "recent_sessions_more_than_available");
        std::string dir = "test_ltm_rss_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        LongTermMemory::Config cfg;
        cfg.store_dir = dir;
        LongTermMemory ltm(cfg);

        // Write 2 sessions
        {
            json sj;
            json s1, s2;
            s1["timestamp"] = "2025-01-15T14:30:00";
            s1["topic"] = "Session 1";
            s1["summary"] = "Summary 1";
            s1["message_count"] = 10;
            s2["timestamp"] = "2025-01-16T10:00:00";
            s2["topic"] = "Session 2";
            s2["summary"] = "Summary 2";
            s2["message_count"] = 5;
            sj["sessions"] = json::array({s1, s2});

            std::ofstream ofs(dir + "/sessions.json");
            ofs << sj.dump(2);
        }
        {
            std::ofstream ofs(dir + "/facts.json");
            ofs << "{}";
        }

        ltm.load();

        auto sessions = ltm.get_recent_sessions(10);  // request 10, only 2 available
        UNIT_TEST("returns_all_two", sessions.size() == 2);

        safe_remove_all(dir);
    }

    // --- Test 18: get_recent_sessions — n=0 returns empty ---
    {
        LOG_INFO("long_term_memory", "recent_sessions_zero_n");
        LongTermMemory ltm;
        auto sessions = ltm.get_recent_sessions(0);
        UNIT_TEST("empty_result", sessions.empty());
    }

    // --- Test 19: get_recent_sessions — n<0 returns empty ---
    {
        LOG_INFO("long_term_memory", "recent_sessions_negative_n");
        LongTermMemory ltm;
        auto sessions = ltm.get_recent_sessions(-5);
        UNIT_TEST("empty_result", sessions.empty());
    }

    // --- Test 20: build_context_string — with facts only ---
    {
        LOG_INFO("long_term_memory", "context_string_facts_only");
        LongTermMemory ltm;
        ltm.add_fact("project.build_system", "CMake");
        ltm.add_fact("coding.style", "modern_cpp20");

        std::string ctx = ltm.build_context_string(5);
        UNIT_TEST("contains_facts_header", ctx.find("Semantic Facts") != std::string::npos);
        UNIT_TEST("contains_build_system", ctx.find("project.build_system") != std::string::npos);
        UNIT_TEST("contains_cmake_value", ctx.find("CMake") != std::string::npos);
        UNIT_TEST("no_recent_sessions_header", ctx.find("Recent Sessions") == std::string::npos);
    }

    // --- Test 21: build_context_string — empty memory returns empty string ---
    {
        LOG_INFO("long_term_memory", "context_string_empty");
        LongTermMemory ltm;
        std::string ctx = ltm.build_context_string(5);
        UNIT_TEST("empty_context", ctx.empty());
    }

    // --- Test 22: build_context_string — with sessions (loaded from file) ---
    {
        LOG_INFO("long_term_memory", "context_string_with_sessions");
        std::string dir = "test_ltm_ctx_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        LongTermMemory::Config cfg;
        cfg.store_dir = dir;
        LongTermMemory ltm(cfg);

        // Write sessions
        {
            json sj;
            json s1;
            s1["timestamp"] = "2025-01-15T14:30:00";
            s1["topic"] = "Implemented RAG vector store";
            s1["summary"] = "Added vector store integration with HNSW index for semantic search capabilities";
            s1["message_count"] = 25;
            sj["sessions"] = json::array({s1});

            std::ofstream ofs(dir + "/sessions.json");
            ofs << sj.dump(2);
        }
        {
            std::ofstream ofs(dir + "/facts.json");
            ofs << "{}";
        }

        ltm.load();
        std::string ctx = ltm.build_context_string(5);
        UNIT_TEST("contains_recent_sessions_header", ctx.find("Recent Sessions") != std::string::npos);
        UNIT_TEST("contains_session_topic", ctx.find("Implemented RAG vector store") != std::string::npos);
        UNIT_TEST("contains_date", ctx.find("2025-01-15") != std::string::npos);

        safe_remove_all(dir);
    }

    // --- Test 23: build_context_string — with both facts and sessions ---
    {
        LOG_INFO("long_term_memory", "context_string_facts_and_sessions");
        std::string dir = "test_ltm_ctx2_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        LongTermMemory::Config cfg;
        cfg.store_dir = dir;
        LongTermMemory ltm(cfg);

        // Write sessions and facts
        {
            json sj;
            json s1;
            s1["timestamp"] = "2025-01-15T14:30:00";
            s1["topic"] = "Session topic";
            s1["summary"] = "Summary text";
            s1["message_count"] = 10;
            sj["sessions"] = json::array({s1});

            std::ofstream ofs(dir + "/sessions.json");
            ofs << sj.dump(2);
        }
        {
            json fj;
            json f1;
            f1["key"] = "project.build_system";
            f1["value"] = "CMake";
            f1["source_session"] = "2025-01-15T14:30:00";
            f1["timestamp"] = "2025-01-15T14:30:00";
            fj["facts"] = json::array({f1});

            std::ofstream ofs(dir + "/facts.json");
            ofs << fj.dump(2);
        }

        ltm.load();
        std::string ctx = ltm.build_context_string(5);
        UNIT_TEST("contains_facts_section", ctx.find("Semantic Facts") != std::string::npos);
        UNIT_TEST("contains_sessions_section", ctx.find("Recent Sessions") != std::string::npos);

        safe_remove_all(dir);
    }

    // --- Test 24: current_timestamp — format validation ---
    {
        LOG_INFO("long_term_memory", "current_timestamp_format");
        std::string ts = LongTermMemory::current_timestamp();
        UNIT_TEST("not_empty", !ts.empty());
        UNIT_TEST("length_is_19", ts.size() == 19);  // "YYYY-MM-DDTHH:MM:SS"
        UNIT_TEST("contains_T_separator", ts.find('T') != std::string::npos);
        UNIT_TEST("year_starts_with_20", ts.substr(0, 4) >= "2020");
    }

    // --- Test 25: save — creates directory if not exists ---
    {
        LOG_INFO("long_term_memory", "save_creates_directory");
        std::string dir = "test_ltm_mkdir_temp";
        safe_remove_all(dir);

        LongTermMemory::Config cfg;
        cfg.store_dir = dir + "/nested/subdir";
        LongTermMemory ltm(cfg);

        UNIT_TEST("dir_not_exists_before", !fs::exists(cfg.store_dir));

        ltm.save();  // should create the directory

        UNIT_TEST("dir_exists_after_save", fs::exists(cfg.store_dir));
        UNIT_TEST("sessions_file_created", fs::exists(dir + "/nested/subdir/sessions.json"));
        UNIT_TEST("facts_file_created", fs::exists(dir + "/nested/subdir/facts.json"));

        safe_remove_all(dir);
    }

    // --- Test 27: FactEntry timestamp is set on add_fact ---
    {
        LOG_INFO("long_term_memory", "fact_entry_has_timestamp");
        LongTermMemory ltm;
        ltm.add_fact("test.key", "test_value");

        auto facts = ltm.get_facts();
        UNIT_TEST("timestamp_not_empty", !facts[0].timestamp.empty());
    }

    // --- Test 28: FactEntry source_session is empty on manual add_fact ---
    {
        LOG_INFO("long_term_memory", "fact_entry_source_session_empty");
        LongTermMemory ltm;
        ltm.add_fact("test.key", "test_value");

        auto facts = ltm.get_facts();
        UNIT_TEST("source_session_empty", facts[0].source_session.empty());
    }

    // --- Test 29: load — sessions with missing optional fields use defaults ---
    {
        LOG_INFO("long_term_memory", "load_sessions_missing_fields");
        std::string dir = "test_ltm_mf_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        // Write session with only timestamp (missing topic, summary, message_count)
        {
            json sj;
            json s1;
            s1["timestamp"] = "2025-01-15T14:30:00";
            sj["sessions"] = json::array({s1});

            std::ofstream ofs(dir + "/sessions.json");
            ofs << sj.dump(2);
        }
        {
            std::ofstream ofs(dir + "/facts.json");
            ofs << "{}";
        }

        LongTermMemory::Config cfg;
        cfg.store_dir = dir;
        LongTermMemory ltm(cfg);
        ltm.load();

        auto sessions = ltm.get_recent_sessions(10);
        UNIT_TEST("one_session_loaded", sessions.size() == 1);
        UNIT_TEST("default_topic", sessions[0].topic == "General conversation");
        UNIT_TEST("empty_summary", sessions[0].summary.empty());
        UNIT_TEST("zero_message_count", sessions[0].message_count == 0);

        safe_remove_all(dir);
    }

    // --- Test 30: load — facts with empty key are skipped ---
    {
        LOG_INFO("long_term_memory", "load_facts_empty_key_skipped");
        std::string dir = "test_ltm_ek_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        // Write facts: one with empty key, one valid
        {
            json fj;
            json f1, f2;
            f1["key"] = "";  // should be skipped
            f1["value"] = "should_be_skipped";
            f2["key"] = "valid.key";
            f2["value"] = "valid_value";
            fj["facts"] = json::array({f1, f2});

            std::ofstream ofs(dir + "/facts.json");
            ofs << fj.dump(2);
        }
        {
            std::ofstream ofs(dir + "/sessions.json");
            ofs << "{}";
        }

        LongTermMemory::Config cfg;
        cfg.store_dir = dir;
        LongTermMemory ltm(cfg);
        ltm.load();

        auto facts = ltm.get_facts();
        UNIT_TEST("only_one_fact_loaded", facts.size() == 1);
        UNIT_TEST("valid_key_loaded", facts[0].key == "valid.key");

        safe_remove_all(dir);
    }

    parent.report.push_back(unit);
}

// ============================================================
// Entry point for long_term_memory tests
// ============================================================

void test_long_term_memory(UnitReport& parent)
{
    // Ensure SafetyGuard whitelist contains current path for tests.
    auto& sg = SafetyGuard::get_instance();
    sg.set_path_whitelist({fs::current_path().string()});

    UnitReport unit("long_term_memory");
    LOG_INFO("long_term_memory", "entry");

    test_long_term_memory_class(unit);

    parent.report.push_back(unit);
}
