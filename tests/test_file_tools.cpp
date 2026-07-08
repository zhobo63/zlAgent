#include <catch2/catch_all.hpp>
#include "tools.h"
#include <fstream>
#include <filesystem>

using namespace agent;
namespace fs = std::filesystem;

// ── ReadFileTool ───────────────────────────────────────────────

TEST_CASE("ReadFileTool: read full file", "[tool][file]") {
    std::string dir = "test_read_file_temp";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(dir);

    std::ofstream out(fs::path(dir) / "hello.txt");
    out << "line1\nline2\nline3\n";
    out.close();

    auto tool = create_read_file_tool();
    REQUIRE(tool->name() == "read_file");

    std::string result = tool->execute(R"({"path": ")" + (dir + "/hello.txt") + R"("})");
    CHECK(result.find("line1") != std::string::npos);
    CHECK(result.find("line3") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("ReadFileTool: read with line range", "[tool][file]") {
    std::string dir = "test_read_file_range_temp";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(dir);

    std::ofstream out(fs::path(dir) / "hello.txt");
    for (int i = 1; i <= 10; ++i)
        out << "line" << i << "\n";
    out.close();

    auto tool = create_read_file_tool();
    std::string result = tool->execute(R"({"path": ")" + (dir + "/hello.txt") + R"(", "start_line": 3, "end_line": 5})");
    CHECK(result.find("line3") != std::string::npos);
    CHECK(result.find("line5") != std::string::npos);
    CHECK(result.find("line1") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("ReadFileTool: nonexistent file returns error", "[tool][file]") {
    auto tool = create_read_file_tool();
    std::string result = tool->execute(R"({"path": "/nonexistent/file.txt"})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("ReadFileTool: empty path returns error", "[tool][file]") {
    auto tool = create_read_file_tool();
    std::string result = tool->execute(R"({"path": ""})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("ReadFileTool: invalid JSON returns error", "[tool][file]") {
    auto tool = create_read_file_tool();
    std::string result = tool->execute("not json");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("ReadFileTool: empty input returns error", "[tool][file]") {
    auto tool = create_read_file_tool();
    std::string result = tool->execute("");
    CHECK(result.find("Error") != std::string::npos);
}

// ── WriteFileTool ──────────────────────────────────────────────

TEST_CASE("WriteFileTool: write new file", "[tool][file]") {
    std::string dir = "test_write_file_temp";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(dir);

    auto tool = create_write_file_tool();
    REQUIRE(tool->name() == "write_file");

    std::string result = tool->execute(R"({"path": ")" + (dir + "/new.txt") + R"(", "content": "hello world"})");
    CHECK(result.find("Successfully wrote") != std::string::npos);
    CHECK(fs::exists(fs::path(dir) / "new.txt"));

    // Verify content.
    std::ifstream in(fs::path(dir) / "new.txt");
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(content == "hello world");

    fs::remove_all(dir);
}

TEST_CASE("WriteFileTool: overwrite existing file", "[tool][file]") {
    std::string dir = "test_write_overwrite_temp";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(dir);

    // Create initial file.
    std::ofstream out(fs::path(dir) / "existing.txt");
    out << "old content";
    out.close();

    auto tool = create_write_file_tool();
    tool->execute(R"({"path": ")" + (dir + "/existing.txt") + R"(", "content": "new content"})");

    std::ifstream in(fs::path(dir) / "existing.txt");
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(content == "new content");

    fs::remove_all(dir);
}

TEST_CASE("WriteFileTool: empty path returns error", "[tool][file]") {
    auto tool = create_write_file_tool();
    std::string result = tool->execute(R"({"path": "", "content": "test"})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("WriteFileTool: empty content returns error", "[tool][file]") {
    auto tool = create_write_file_tool();
    std::string result = tool->execute(R"({"path": "/tmp/test.txt", "content": ""})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("WriteFileTool: invalid JSON returns error", "[tool][file]") {
    auto tool = create_write_file_tool();
    std::string result = tool->execute("not json");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("WriteFileTool: empty input returns error", "[tool][file]") {
    auto tool = create_write_file_tool();
    std::string result = tool->execute("");
    CHECK(result.find("Error") != std::string::npos);
}

// ── AppendFileTool ─────────────────────────────────────────────

TEST_CASE("AppendFileTool: append to existing file", "[tool][file]") {
    std::string dir = "test_append_file_temp";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(dir);

    // Create initial file.
    std::ofstream out(fs::path(dir) / "append.txt");
    out << "first line\n";
    out.close();

    auto tool = create_append_file_tool();
    REQUIRE(tool->name() == "append_file");

    tool->execute(R"({"path": ")" + (dir + "/append.txt") + R"(", "content": "second line\n"})");

    std::ifstream in(fs::path(dir) / "append.txt");
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(content.find("first line") != std::string::npos);
    CHECK(content.find("second line") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("AppendFileTool: append creates new file", "[tool][file]") {
    std::string dir = "test_append_new_temp";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(dir);

    auto tool = create_append_file_tool();
    tool->execute(R"({"path": ")" + (dir + "/new.txt") + R"(", "content": "hello\n"})");

    CHECK(fs::exists(fs::path(dir) / "new.txt"));

    fs::remove_all(dir);
}

TEST_CASE("AppendFileTool: empty path returns error", "[tool][file]") {
    auto tool = create_append_file_tool();
    std::string result = tool->execute(R"({"path": "", "content": "test"})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("AppendFileTool: invalid JSON returns error", "[tool][file]") {
    auto tool = create_append_file_tool();
    std::string result = tool->execute("not json");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("AppendFileTool: empty input returns error", "[tool][file]") {
    auto tool = create_append_file_tool();
    std::string result = tool->execute("");
    CHECK(result.find("Error") != std::string::npos);
}

// ── InsertFileContentTool ──────────────────────────────────────

TEST_CASE("InsertFileContentTool: insert at line 1", "[tool][file]") {
    std::string dir = "test_insert_file_temp";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(dir);

    std::ofstream out(fs::path(dir) / "insert.txt");
    out << "line1\nline2\nline3\n";
    out.close();

    auto tool = create_insert_file_content_tool();
    REQUIRE(tool->name() == "insert_file_content");

    tool->execute(R"({"path": ")" + (dir + "/insert.txt") + R"(", "line_number": 1, "content": "new line\n"})");

    std::ifstream in(fs::path(dir) / "insert.txt");
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(content.find("new line") != std::string::npos);
    CHECK(content.find("line1") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("InsertFileContentTool: insert at end", "[tool][file]") {
    std::string dir = "test_insert_end_temp";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(dir);

    std::ofstream out(fs::path(dir) / "insert.txt");
    out << "line1\nline2\n";
    out.close();

    auto tool = create_insert_file_content_tool();
    tool->execute(R"({"path": ")" + (dir + "/insert.txt") + R"(", "line_number": 3, "content": "appended\n"})");

    std::ifstream in(fs::path(dir) / "insert.txt");
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(content.find("appended") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("InsertFileContentTool: empty path returns error", "[tool][file]") {
    auto tool = create_insert_file_content_tool();
    std::string result = tool->execute(R"({"path": "", "line_number": 1, "content": "test"})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("InsertFileContentTool: invalid JSON returns error", "[tool][file]") {
    auto tool = create_insert_file_content_tool();
    std::string result = tool->execute("not json");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("InsertFileContentTool: empty input returns error", "[tool][file]") {
    auto tool = create_insert_file_content_tool();
    std::string result = tool->execute("");
    CHECK(result.find("Error") != std::string::npos);
}

// ── EditFileTool ───────────────────────────────────────────────

TEST_CASE("EditFileTool: text replacement", "[tool][file]") {
    std::string dir = "test_edit_file_temp";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(dir);

    std::ofstream out(fs::path(dir) / "edit.txt");
    out << "hello world\nfoo bar\n";
    out.close();

    auto tool = create_edit_file_tool();
    REQUIRE(tool->name() == "edit_file");

    std::string result = tool->execute(R"({"path": ")" + (dir + "/edit.txt") + R"(", "old_text": "hello world", "new_text": "goodbye world"})");
    CHECK(result.find("Error") == std::string::npos);

    std::ifstream in(fs::path(dir) / "edit.txt");
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(content.find("goodbye world") != std::string::npos);
    CHECK(content.find("hello world") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("EditFileTool: nonexistent old_text returns error", "[tool][file]") {
    std::string dir = "test_edit_notfound_temp";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(dir);

    std::ofstream out(fs::path(dir) / "edit.txt");
    out << "hello world\n";
    out.close();

    auto tool = create_edit_file_tool();
    std::string result = tool->execute(R"({"path": ")" + (dir + "/edit.txt") + R"(", "old_text": "not found", "new_text": "replacement"})");
    CHECK(result.find("Error") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("EditFileTool: empty path returns error", "[tool][file]") {
    auto tool = create_edit_file_tool();
    std::string result = tool->execute(R"({"path": "", "old_text": "a", "new_text": "b"})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("EditFileTool: invalid JSON returns error", "[tool][file]") {
    auto tool = create_edit_file_tool();
    std::string result = tool->execute("not json");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("EditFileTool: empty input returns error", "[tool][file]") {
    auto tool = create_edit_file_tool();
    std::string result = tool->execute("");
    CHECK(result.find("Error") != std::string::npos);
}

// ── ListDirectoryTool ──────────────────────────────────────────

TEST_CASE("ListDirectoryTool: list directory", "[tool][file]") {
    std::string dir = "test_list_dir_temp";
    if (fs::exists(dir)) fs::remove_all(dir);
    fs::create_directories(fs::path(dir) / "subdir");

    std::ofstream out1(fs::path(dir) / "file1.txt");
    out1.close();
    std::ofstream out2(fs::path(dir) / "file2.cpp");
    out2.close();

    auto tool = create_list_directory_tool();
    REQUIRE(tool->name() == "list_directory");

    std::string result = tool->execute(R"({"path": ")" + dir + R"("})");
    CHECK(result.find("file1.txt") != std::string::npos);
    CHECK(result.find("subdir") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("ListDirectoryTool: nonexistent directory returns error", "[tool][file]") {
    auto tool = create_list_directory_tool();
    std::string result = tool->execute(R"({"path": "/nonexistent/dir"})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("ListDirectoryTool: empty path returns error", "[tool][file]") {
    auto tool = create_list_directory_tool();
    std::string result = tool->execute(R"({"path": ""})");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("ListDirectoryTool: invalid JSON returns error", "[tool][file]") {
    auto tool = create_list_directory_tool();
    std::string result = tool->execute("not json");
    CHECK(result.find("Error") != std::string::npos);
}

TEST_CASE("ListDirectoryTool: empty input returns error", "[tool][file]") {
    auto tool = create_list_directory_tool();
    std::string result = tool->execute("");
    CHECK(result.find("Error") != std::string::npos);
}
