#include <catch2/catch_all.hpp>
#include "config.h"
#include <fstream>
#include <filesystem>

using namespace agent;

TEST_CASE("IniParser: parse empty file", "[config]") {
    auto data = IniParser::parse("nonexistent_file.ini");
    REQUIRE(data.empty());
}

TEST_CASE("IniParser: parse basic key-value", "[config]") {
    // Create a temp INI file.
    std::string path = "test_config_temp.ini";
    std::ofstream out(path);
    out << "[llm]\nurl = http://127.0.0.1:5678\n";
    out.close();

    auto data = IniParser::parse(path);
    REQUIRE(data.size() == 1);
    REQUIRE(data.count("llm") > 0);
    REQUIRE(data["llm"]["url"] == "http://127.0.0.1:5678");

    std::filesystem::remove(path);
}

TEST_CASE("Config: load defaults when no INI", "[config]") {
    auto cfg = Config::load("nonexistent.ini");
    REQUIRE(cfg.llm.url == "http://127.0.0.1:1234");
    REQUIRE(cfg.memory.max_messages == 50);
}

TEST_CASE("Config: parse_bool via INI roundtrip", "[config]") {
    std::string path = "test_bool_temp.ini";
    std::ofstream out(path);
    out << "[safety]\ndangerous_tool_confirmation = true\nskill_content_check = false\n";
    out.close();

    auto cfg = Config::load(path);
    REQUIRE(cfg.safety.dangerous_tool_confirmation == true);
    REQUIRE(cfg.safety.skill_content_check == false);

    std::filesystem::remove(path);
}

TEST_CASE("Config: RAG section defaults", "[config]") {
    auto cfg = Config::load("nonexistent.ini");
    REQUIRE(cfg.rag.enabled == false);
    REQUIRE(cfg.rag.embedding_backend == "tfidf");
    REQUIRE(cfg.rag.top_k == 5);
}

TEST_CASE("Config: memory long-term defaults", "[config]") {
    auto cfg = Config::load("nonexistent.ini");
    REQUIRE(cfg.memory.long_term_enabled == false);
    REQUIRE(cfg.memory.store_dir == ".zlagent/memory");
    REQUIRE(cfg.memory.max_sessions == 100);
}

TEST_CASE("IniParser: update_key creates file", "[config]") {
    std::string path = "test_update_temp.ini";
    if (std::filesystem::exists(path)) std::filesystem::remove(path);

    bool ok = IniParser::update_key(path, "llm", "model", "qwen2.5");
    REQUIRE(ok == true);
    REQUIRE(std::filesystem::exists(path));

    auto data = IniParser::parse(path);
    REQUIRE(data["llm"]["model"] == "qwen2.5");

    std::filesystem::remove(path);
}
