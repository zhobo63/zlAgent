#include <catch2/catch_all.hpp>
#include "long_term_memory.h"
#include <filesystem>

using namespace agent;

TEST_CASE("LongTermMemory: add and get facts", "[ltm]") {
    LongTermMemory ltm;
    ltm.add_fact("project.build_system", "CMake");
    ltm.add_fact("coding.style", "Google C++");

    auto all = ltm.get_facts();
    REQUIRE(all.size() == 2);

    auto filtered = ltm.get_facts("project.");
    REQUIRE(filtered.size() == 1);
    REQUIRE(filtered[0].key == "project.build_system");
}

TEST_CASE("LongTermMemory: remove fact", "[ltm]") {
    LongTermMemory ltm;
    ltm.add_fact("test.key", "value");
    REQUIRE(ltm.get_facts().size() == 1);

    ltm.remove_fact("test.key");
    REQUIRE(ltm.get_facts().size() == 0);
}

TEST_CASE("LongTermMemory: save and load roundtrip", "[ltm]") {
    std::string dir = "test_ltm_temp";
    if (std::filesystem::exists(dir)) std::filesystem::remove_all(dir);

    LongTermMemory::Config cfg1;
    cfg1.store_dir = dir;
    LongTermMemory ltm(cfg1);

    ltm.add_fact("project.name", "zlagent");
    ltm.save();

    // Load into a new instance.
    LongTermMemory ltm2(cfg1);
    REQUIRE(ltm2.load() == true);
    auto facts = ltm2.get_facts();
    REQUIRE(facts.size() == 1);
    REQUIRE(facts[0].value == "zlagent");

    std::filesystem::remove_all(dir);
}

TEST_CASE("LongTermMemory: load nonexistent dir returns false", "[ltm]") {
    LongTermMemory::Config cfg2;
    cfg2.store_dir = "nonexistent_ltm_dir";
    LongTermMemory ltm(cfg2);
    REQUIRE(ltm.load() == false);
}

TEST_CASE("LongTermMemory: build_context_string includes facts", "[ltm]") {
    LongTermMemory ltm;
    ltm.add_fact("test.key", "value");

    std::string ctx = ltm.build_context_string();
    REQUIRE(!ctx.empty());
    CHECK(ctx.find("Semantic Facts") != std::string::npos);
}

TEST_CASE("LongTermMemory: build_context_string empty when no data", "[ltm]") {
    LongTermMemory ltm;
    std::string ctx = ltm.build_context_string();
    REQUIRE(ctx.empty());
}

TEST_CASE("LongTermMemory: get_recent_sessions empty by default", "[ltm]") {
    LongTermMemory ltm;
    auto sessions = ltm.get_recent_sessions(5);
    REQUIRE(sessions.empty());
}
