#include <catch2/catch_all.hpp>
#include "command_dispatcher.h"
#include <sstream>

using namespace agent;

TEST_CASE("CommandDispatcher: dispatch known command", "[commands]") {
    CommandDispatcher dispatcher;
    bool called = false;
    dispatcher.register_command("test", [&](const auto&) { called = true; });

    REQUIRE(dispatcher.dispatch("/test") == true);
    REQUIRE(called == true);
}

TEST_CASE("CommandDispatcher: non-slash input not dispatched", "[commands]") {
    CommandDispatcher dispatcher;
    bool called = false;
    dispatcher.register_command("test", [&](const auto&) { called = true; });

    REQUIRE(dispatcher.dispatch("hello world") == false);
    REQUIRE(called == false);
}

TEST_CASE("CommandDispatcher: empty input not dispatched", "[commands]") {
    CommandDispatcher dispatcher;
    REQUIRE(dispatcher.dispatch("") == false);
}

TEST_CASE("CommandDispatcher: command with arguments", "[commands]") {
    CommandDispatcher dispatcher;
    std::vector<std::string> received_args;
    dispatcher.register_command("echo", [&](const auto& args) {
        received_args = args;
    });

    dispatcher.dispatch("/echo hello world");
    REQUIRE(received_args.size() == 2);
    REQUIRE(received_args[0] == "hello");
    REQUIRE(received_args[1] == "world");
}

TEST_CASE("CommandDispatcher: command with no arguments", "[commands]") {
    CommandDispatcher dispatcher;
    std::vector<std::string> received_args;
    dispatcher.register_command("ping", [&](const auto& args) {
        received_args = args;
    });

    dispatcher.dispatch("/ping");
    REQUIRE(received_args.empty());
}

TEST_CASE("CommandDispatcher: unknown command returns true (handled)", "[commands]") {
    CommandDispatcher dispatcher;
    // Unknown commands are still "handled" - they don't go to LLM.
    REQUIRE(dispatcher.dispatch("/nonexistent") == true);
}
