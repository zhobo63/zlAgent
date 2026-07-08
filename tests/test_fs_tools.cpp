#include <catch2/catch_all.hpp>
#include "tools.h"
#include <fstream>
#include <filesystem>

using namespace agent;
namespace fs = std::filesystem;

// ── CreateDirectoryTool ────────────────────────────────────────

TEST_CASE("CreateDirectoryTool: create new directory", "[tool][fs]") {
    std::string dir = "test_create_dir_temp";
    if (fs::exists(dir)) fs::remove_all(dir);

    auto tool = create_create_directory_tool();
    REQUIRE(tool->name() == "create_directory");

    std::string result = tool->execute(R"({"path": ")" + dir + R"("})");
    CHECK(result.find("Successfully created") != std::string::npos);
    CHECK(fs::is_directory(dir));

    fs::remove_all(dir);
}

TEST_CASE("CreateDirectoryTool: create nested directories", "[tool][fs]") {
    std::string dir = "test_create_nested_temp";
    if (fs::exists(dir)) fs::remove_all(dir);

    auto tool = create_create_directory_tool();
    std::string result = tool->execute(R"({"path": ")" + dir + "/a/b/c" + R"("})");
    CHECK(result.find("Successfully created") != std::string::npos);
    CHECK(fs::is_directory(dir));

    fs::remove_all(dir);
}

TEST_CASE("CreateDirectoryTool: already exists returns message", "[tool][fs]") {
    std::string dir = "test_create_exists_temp";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(dir);

    auto tool = create_create_directory_tool();
    std::string result = tool->execute(R"({"path": ")" + dir + R"("})");
    CHECK(result.find("already exists") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("CreateDirectoryTool: empty path returns error", "[tool][fs]") {
    auto tool = create_create_directory_tool();
    std::string result = tool->execute(R"({"path": ""})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("CreateDirectoryTool: invalid JSON returns error", "[tool][fs]") {
    auto tool = create_create_directory_tool();
    std::string result = tool->execute("not json");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("CreateDirectoryTool: empty input returns error", "[tool][fs]") {
    auto tool = create_create_directory_tool();
    std::string result = tool->execute("");
    CHECK(result.find("Error") != std::string::npos);
}

// ── DeletePathTool ─────────────────────────────────────────────

TEST_CASE("DeletePathTool: delete file", "[tool][fs]") {
    std::string dir = "test_delete_file_temp";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(dir);

    std::ofstream out(fs::path(dir) / "delete_me.txt");
    out.close();

    auto tool = create_delete_path_tool();
    REQUIRE(tool->name() == "delete_path");

    std::string result = tool->execute(R"({"path": ")" + (dir + "/delete_me.txt") + R"("})");
    CHECK(result.find("Successfully deleted") != std::string::npos);
    CHECK(!fs::exists(fs::path(dir) / "delete_me.txt"));

    fs::remove_all(dir);
}

TEST_CASE("DeletePathTool: delete directory recursively", "[tool][fs]") {
    std::string dir = "test_delete_dir_temp";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(fs::path(dir) / "sub");

    auto tool = create_delete_path_tool();
    std::string result = tool->execute(R"({"path": ")" + dir + R"("})");
    CHECK(result.find("Successfully deleted") != std::string::npos);
    CHECK(!fs::exists(dir));
}

TEST_CASE("DeletePathTool: nonexistent path returns error", "[tool][fs]") {
    auto tool = create_delete_path_tool();
    std::string result = tool->execute(R"({"path": "/nonexistent/path"})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("DeletePathTool: empty path returns error", "[tool][fs]") {
    auto tool = create_delete_path_tool();
    std::string result = tool->execute(R"({"path": ""})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("DeletePathTool: invalid JSON returns error", "[tool][fs]") {
    auto tool = create_delete_path_tool();
    std::string result = tool->execute("not json");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("DeletePathTool: empty input returns error", "[tool][fs]") {
    auto tool = create_delete_path_tool();
    std::string result = tool->execute("");
    CHECK(result.find("Error") != std::string::npos);
}

// ── CopyPathTool ───────────────────────────────────────────────

TEST_CASE("CopyPathTool: copy file", "[tool][fs]") {
    std::string dir = "test_copy_file_temp";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(dir);

    std::ofstream out(fs::path(dir) / "src.txt");
    out << "copy me\n";
    out.close();

    auto tool = create_copy_path_tool();
    REQUIRE(tool->name() == "copy_path");

    std::string result = tool->execute(R"({"source": ")" + (dir + "/src.txt") + R"(", "destination": ")" + (dir + "/dst.txt") + R"("})");
    CHECK(result.find("Successfully copied") != std::string::npos);
    CHECK(fs::exists(fs::path(dir) / "dst.txt"));

    fs::remove_all(dir);
}

TEST_CASE("CopyPathTool: copy directory", "[tool][fs]") {
    std::string dir = "test_copy_dir_temp";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(fs::path(dir) / "src");

    auto tool = create_copy_path_tool();
    std::string result = tool->execute(R"({"source": ")" + (dir + "/src") + R"(", "destination": ")" + (dir + "/dst") + R"("})");
    CHECK(result.find("Successfully copied") != std::string::npos);
    CHECK(fs::is_directory(dir + "/dst"));

    fs::remove_all(dir);
}

TEST_CASE("CopyPathTool: nonexistent source returns error", "[tool][fs]") {
    auto tool = create_copy_path_tool();
    std::string result = tool->execute(R"({"source": "/nonexistent/src", "destination": "/tmp/dst"})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("CopyPathTool: empty source returns error", "[tool][fs]") {
    auto tool = create_copy_path_tool();
    std::string result = tool->execute(R"({"source": "", "destination": "/tmp/dst"})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("CopyPathTool: invalid JSON returns error", "[tool][fs]") {
    auto tool = create_copy_path_tool();
    std::string result = tool->execute("not json");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("CopyPathTool: empty input returns error", "[tool][fs]") {
    auto tool = create_copy_path_tool();
    std::string result = tool->execute("");
    CHECK(result.find("Error") != std::string::npos);
}

// ── MovePathTool ───────────────────────────────────────────────

TEST_CASE("MovePathTool: move file", "[tool][fs]") {
    std::string dir = "test_move_file_temp";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(dir);

    std::ofstream out(fs::path(dir) / "src.txt");
    out << "move me\n";
    out.close();

    auto tool = create_move_path_tool();
    REQUIRE(tool->name() == "move_path");

    std::string result = tool->execute(R"({"source": ")" + (dir + "/src.txt") + R"(", "destination": ")" + (dir + "/dst.txt") + R"("})");
    CHECK(result.find("Successfully moved") != std::string::npos);
    CHECK(!fs::exists(fs::path(dir) / "src.txt"));
    CHECK(fs::exists(fs::path(dir) / "dst.txt"));

    fs::remove_all(dir);
}

TEST_CASE("MovePathTool: rename file", "[tool][fs]") {
    std::string dir = "test_rename_temp";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(dir);

    std::ofstream out(fs::path(dir) / "old.txt");
    out.close();

    auto tool = create_move_path_tool();
    tool->execute(R"({"source": ")" + (dir + "/old.txt") + R"(", "destination": ")" + (dir + "/new.txt") + R"("})");
    CHECK(!fs::exists(fs::path(dir) / "old.txt"));
    CHECK(fs::exists(fs::path(dir) / "new.txt"));

    fs::remove_all(dir);
}

TEST_CASE("MovePathTool: nonexistent source returns error", "[tool][fs]") {
    auto tool = create_move_path_tool();
    std::string result = tool->execute(R"({"source": "/nonexistent/src", "destination": "/tmp/dst"})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("MovePathTool: empty source returns error", "[tool][fs]") {
    auto tool = create_move_path_tool();
    std::string result = tool->execute(R"({"source": "", "destination": "/tmp/dst"})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("MovePathTool: invalid JSON returns error", "[tool][fs]") {
    auto tool = create_move_path_tool();
    std::string result = tool->execute("not json");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("MovePathTool: empty input returns error", "[tool][fs]") {
    auto tool = create_move_path_tool();
    std::string result = tool->execute("");
    CHECK(result.find("Error") != std::string::npos);
}

// ── FindFilesTool ──────────────────────────────────────────────

TEST_CASE("FindFilesTool: find files by glob", "[tool][fs]") {
    std::string dir = "test_find_files_temp";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(fs::path(dir) / "sub");

    std::ofstream out1(fs::path(dir) / "a.cpp");
    out1.close();
    std::ofstream out2(fs::path(dir) / "b.h");
    out2.close();
    std::ofstream out3(fs::path(dir) / "sub" / "c.cpp");
    out3.close();

    auto tool = create_find_files_tool();
    REQUIRE(tool->name() == "find_files");

    std::string result = tool->execute(R"({"glob": "**/*.cpp", "directory": ")" + dir + R"("})");
    CHECK(result.find("a.cpp") != std::string::npos);
    CHECK(result.find("c.cpp") != std::string::npos);
    CHECK(result.find("b.h") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("FindFilesTool: no matches returns message", "[tool][fs]") {
    std::string dir = "test_find_no_match_temp";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(dir);

    auto tool = create_find_files_tool();
    std::string result = tool->execute(R"({"glob": "**/*.xyz", "directory": ")" + dir + R"("})");
    CHECK(result.find("No files found") != std::string::npos || result.empty());

    fs::remove_all(dir);
}

TEST_CASE("FindFilesTool: empty glob returns error", "[tool][fs]") {
    auto tool = create_find_files_tool();
    std::string result = tool->execute(R"({"glob": ""})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("FindFilesTool: invalid JSON returns error", "[tool][fs]") {
    auto tool = create_find_files_tool();
    std::string result = tool->execute("not json");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("FindFilesTool: empty input returns error", "[tool][fs]") {
    auto tool = create_find_files_tool();
    std::string result = tool->execute("");
    CHECK(result.find("Error") != std::string::npos);
}
