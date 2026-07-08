#include <catch2/catch_all.hpp>
#include "tools.h"
#include <fstream>
#include <filesystem>

using namespace agent;

TEST_CASE("CodeSearchTool: basic regex search", "[tool][code-search]") {
    namespace fs = std::filesystem;
    std::string dir = "test_code_search_temp";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(dir);

    // Create a test file.
    std::ofstream out(fs::path(dir) / "hello.cpp");
    out << "#include <iostream>\n";
    out << "int main() { return 0; }\n";
    out.close();

    auto tool = create_code_search_tool();
    REQUIRE(tool->name() == "search_code");

    std::string result = tool->execute(R"({"pattern": "main", "directory": ")" + dir + R"("})");
    CHECK(result.find("hello.cpp") != std::string::npos);
    CHECK(result.find("int main()") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("CodeSearchTool: no matches returns message", "[tool][code-search]") {
    namespace fs = std::filesystem;
    std::string dir = "test_code_search_temp2";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(dir);

    std::ofstream out(fs::path(dir) / "hello.cpp");
    out << "int x = 42;\n";
    out.close();

    auto tool = create_code_search_tool();
    std::string result = tool->execute(R"({"pattern": "nonexistent", "directory": ")" + dir + R"("})");
    CHECK(result.find("No matches found") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("CodeSearchTool: file_pattern filter works", "[tool][code-search]") {
    namespace fs = std::filesystem;
    std::string dir = "test_code_search_temp3";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(dir);

    std::ofstream out1(fs::path(dir) / "hello.cpp");
    out1 << "int main() {}\n";
    out1.close();

    std::ofstream out2(fs::path(dir) / "hello.h");
    out2 << "void foo();\n";
    out2.close();

    auto tool = create_code_search_tool();
    std::string result = tool->execute(R"({"pattern": ".*", "directory": ")" + dir + R"(", "file_pattern": "*.cpp"})");
    CHECK(result.find("hello.cpp") != std::string::npos);
    CHECK(result.find("hello.h") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("CodeSearchTool: empty pattern returns error", "[tool][code-search]") {
    auto tool = create_code_search_tool();
    std::string result = tool->execute(R"({"pattern": ""})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("CodeSearchTool: invalid JSON returns error", "[tool][code-search]") {
    auto tool = create_code_search_tool();
    std::string result = tool->execute("not json");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("CodeSearchTool: empty input returns error", "[tool][code-search]") {
    auto tool = create_code_search_tool();
    std::string result = tool->execute("");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("CodeSearchTool: invalid regex returns error", "[tool][code-search]") {
    auto tool = create_code_search_tool();
    std::string result = tool->execute(R"({"pattern": "[invalid"})");
    CHECK(result.find("Invalid regex") != std::string::npos);
}

TEST_CASE("CodeSearchTool: skips hidden directories", "[tool][code-search]") {
    namespace fs = std::filesystem;
    std::string dir = "test_code_search_temp4";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(fs::path(dir) / ".hidden");

    // File in hidden dir should be skipped.
    std::ofstream out1(fs::path(dir) / ".hidden" / "secret.cpp");
    out1 << "int main() {}\n";
    out1.close();

    // File in visible dir should be found.
    std::ofstream out2(fs::path(dir) / "visible.cpp");
    out2 << "int main() {}\n";
    out2.close();

    auto tool = create_code_search_tool();
    std::string result = tool->execute(R"({"pattern": "main", "directory": ")" + dir + R"("})");
    CHECK(result.find("visible.cpp") != std::string::npos);
    CHECK(result.find("secret.cpp") == std::string::npos);

    fs::remove_all(dir);
}
