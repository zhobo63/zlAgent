#include <catch2/catch_all.hpp>
#include "memory.h"

using namespace agent;

TEST_CASE("Memory: add and get messages", "[memory]") {
    Memory mem(10);
    mem.add(ChatMessage{"user", "hello", ""});
    mem.add(ChatMessage{"assistant", "hi there", ""});

    auto msgs = mem.get_messages();
    REQUIRE(msgs.size() == 2);
    REQUIRE(msgs[0].content == "hello");
}

TEST_CASE("Memory: sliding window truncation", "[memory]") {
    Memory mem(3);
    for (int i = 0; i < 5; ++i) {
        mem.add(ChatMessage{"user", "msg" + std::to_string(i), ""});
    }

    auto msgs = mem.get_messages();
    REQUIRE(msgs.size() == 3);
    // Should keep the last 3.
    REQUIRE(msgs[0].content == "msg2");
}

TEST_CASE("Memory: clear", "[memory]") {
    Memory mem(10);
    mem.add(ChatMessage{"user", "test", ""});
    mem.clear();
    auto msgs = mem.get_messages();
    REQUIRE(msgs.empty());
}

TEST_CASE("Memory: set_system_prompt replaces existing", "[memory]") {
    Memory mem(10);
    mem.set_system_prompt("old prompt");
    mem.add(ChatMessage{"user", "hello", ""});

    mem.set_system_prompt("new prompt");
    auto msgs = mem.get_messages();
    REQUIRE(msgs[0].role == "system");
    REQUIRE(msgs[0].content == "new prompt");
}

TEST_CASE("Memory: set_system_prompt inserts if none exists", "[memory]") {
    Memory mem(10);
    mem.set_system_prompt("first prompt");
    auto msgs = mem.get_messages();
    REQUIRE(msgs.size() >= 1);
    REQUIRE(msgs[0].role == "system");
}
