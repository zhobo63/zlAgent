#include <catch2/catch_all.hpp>
#include "command_dispatcher.h"
#include <sstream>

using namespace agent;

TEST_CASE("CommandDispatcher: dispatch known command", "[commands]") {
    CommandDispatcher dispatcher;
    bool called = false;
    dispatcher.register_command("test", [&](const std::vector<std::string>&, std::string&) { called = true; });

    std::string response;
    REQUIRE(dispatcher.dispatch("/test", response) == true);
    REQUIRE(called == true);
}

TEST_CASE("CommandDispatcher: non-slash input not dispatched", "[commands]") {
    CommandDispatcher dispatcher;
    bool called = false;
    dispatcher.register_command("test", [&](const std::vector<std::string>&, std::string&) { called = true; });

    std::string response;
    REQUIRE(dispatcher.dispatch("hello world", response) == false);
    REQUIRE(called == false);
}

TEST_CASE("CommandDispatcher: empty input not dispatched", "[commands]") {
    CommandDispatcher dispatcher;
    std::string response;
    REQUIRE(dispatcher.dispatch("", response) == false);
}

TEST_CASE("CommandDispatcher: command with arguments", "[commands]") {
    CommandDispatcher dispatcher;
    std::vector<std::string> received_args;
    dispatcher.register_command("echo", [&](const std::vector<std::string>& args, std::string&) {
        received_args = args;
    });

    std::string response;
    dispatcher.dispatch("/echo hello world", response);
    REQUIRE(received_args.size() == 2);
    REQUIRE(received_args[0] == "hello");
    REQUIRE(received_args[1] == "world");
}

TEST_CASE("CommandDispatcher: command with no arguments", "[commands]") {
    CommandDispatcher dispatcher;
    std::vector<std::string> received_args;
    dispatcher.register_command("ping", [&](const std::vector<std::string>& args, std::string&) {
        received_args = args;
    });

    std::string response;
    dispatcher.dispatch("/ping", response);
    REQUIRE(received_args.empty());
}

TEST_CASE("CommandDispatcher: unknown command returns true (handled)", "[commands]") {
    CommandDispatcher dispatcher;
    // Unknown commands are still "handled" - they don't go to LLM.
    std::string response;
    REQUIRE(dispatcher.dispatch("/nonexistent", response) == true);
}
