#include <catch2/catch_all.hpp>
#include "tool.h"

using namespace agent;

// A simple test tool.
class DummyTool : public Tool {
public:
    std::string name() const override { return "dummy"; }
    std::string description() const override { return "A dummy tool."; }
    std::string parameters_schema() const override { return "{}"; }
    std::string execute(const std::string&) override { return "ok"; }
};

TEST_CASE("ToolRegistry: register and get tools", "[tool]") {
    ToolRegistry reg;
    reg.register_tool(std::make_shared<DummyTool>());
    auto tools = reg.get_tools();
    REQUIRE(tools.size() == 1);
    REQUIRE(tools[0]->name() == "dummy");
}

TEST_CASE("ToolRegistry: execute tool", "[tool]") {
    ToolRegistry reg;
    reg.register_tool(std::make_shared<DummyTool>());
    auto result = reg.execute("dummy", "{}");
    REQUIRE(result == "ok");
}

TEST_CASE("ToolRegistry: unknown tool returns error", "[tool]") {
    ToolRegistry reg;
    auto result = reg.execute("nonexistent", "{}");
    CHECK(!result.empty());
}

TEST_CASE("ToolDefinition: to_definition works", "[tool]") {
    DummyTool tool;
    auto def = tool.to_definition();
    REQUIRE(def.name == "dummy");
    REQUIRE(!def.description.empty());
}
