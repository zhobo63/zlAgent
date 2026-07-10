#include "pch.h"
#include "unit_test.h"
#include <fstream>

#include "tools.h"
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

// ── ReadFileTool ───────────────────────────────────────────────

void test_read_file_tools(UnitReport& parent)
{
    UnitReport unit("read_file");
    LOG_INFO("test_read_file_tools", "read_file");

    // read full file
    {
        LOG_INFO("read_file", "test_read_file_temp");
        std::string dir = "test_read_file_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "hello.txt");
        out << "line1\nline2\nline3\n";
        out.close();

        auto tool = create_read_file_tool();
        UNIT_TEST("name_is_read_file", tool->name() == "read_file");

        json args;
        args["path"] = dir + "/hello.txt";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("contains_line1", result.find("line1") != std::string::npos);
        UNIT_TEST("contains_line3", result.find("line3") != std::string::npos);

        safe_remove_all(dir);
    }

    // read with line range
    {
        LOG_INFO("read_file", "test_read_file_range_temp");
        std::string dir = "test_read_file_range_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "hello.txt");
        for (int i = 1; i <= 10; ++i)
            out << "line" << i << "\n";
        out.close();

        auto tool = create_read_file_tool();
        json args;
        args["path"] = dir + "/hello.txt";
        args["start_line"] = 3;
        args["end_line"] = 5;
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("range_contains_line3", result.find("line3") != std::string::npos);
        UNIT_TEST("range_contains_line5", result.find("line5") != std::string::npos);
        UNIT_TEST("range_no_line1", result.find("line1") == std::string::npos);

        safe_remove_all(dir);
    }

    // nonexistent file returns error
    {
        LOG_INFO("read_file", "nonexistent_returns_error");
        auto tool = create_read_file_tool();
        json args;
        args["path"] = "/nonexistent/file.txt";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("nonexistent_returns_error", result.find("Error") != std::string::npos);
    }

    // empty path returns error
    {
        LOG_INFO("read_file", "empty_path_returns_error");
        auto tool = create_read_file_tool();
        json args;
        args["path"] = "";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("empty_path_returns_error", result.find("Error") != std::string::npos);
    }

    // invalid JSON returns error
    {
        LOG_INFO("read_file", "invalid_json_returns_error");
        auto tool = create_read_file_tool();
        std::string args_str = "not json";
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("invalid_json_returns_error", result.find("Error") != std::string::npos);
    }

    // empty input returns error
    {
        LOG_INFO("read_file", "empty_input_returns_error");
        auto tool = create_read_file_tool();
        std::string args_str = "";
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("empty_input_returns_error", result.find("Error") != std::string::npos);
    }

    // large file returns outline (>200 lines)
    {
        LOG_INFO("read_file", "test_read_file_outline_temp");
        std::string dir = "test_read_file_outline_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "large.txt");
        for (int i = 1; i <= 250; ++i)
            out << "line" << i << "\n";
        out.close();

        auto tool = create_read_file_tool();
        json args;
        args["path"] = dir + "/large.txt";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("outline_mode_triggered", result.find("# File outline for") != std::string::npos);

        safe_remove_all(dir);
    }

    // read with only start_line (no end_line) reads to EOF
    {
        LOG_INFO("read_file", "test_read_file_start_only_temp");
        std::string dir = "test_read_file_start_only_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "hello.txt");
        for (int i = 1; i <= 10; ++i)
            out << "line" << i << "\n";
        out.close();

        auto tool = create_read_file_tool();
        json args;
        args["path"] = dir + "/hello.txt";
        args["start_line"] = 8;
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("start_only_contains_line8", result.find("line8") != std::string::npos);
        UNIT_TEST("start_only_no_line7", result.find("line7") == std::string::npos);

        safe_remove_all(dir);
    }

    parent.report.push_back(unit);
}

// ── WriteFileTool ──────────────────────────────────────────────

void test_write_file_tools(UnitReport& parent)
{
    UnitReport unit("write_file");
    LOG_INFO("test_write_file_tools", "write_file");

    // write new file
    {
        LOG_INFO("write_file", "test_write_file_temp");
        std::string dir = "test_write_file_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        auto tool = create_write_file_tool();
        UNIT_TEST("name_is_write_file", tool->name() == "write_file");

        json args;
        args["path"] = dir + "/new.txt";
        args["content"] = "hello world";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("successfully_wrote", result.find("Successfully wrote") != std::string::npos);
        UNIT_TEST("file_exists", fs::exists(fs::path(dir) / "new.txt"));

        std::ifstream in(fs::path(dir) / "new.txt");
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        UNIT_TEST("content_matches", content == "hello world");

        safe_remove_all(dir);
    }

    // overwrite existing file
    {
        LOG_INFO("write_file", "test_write_overwrite_temp");
        std::string dir = "test_write_overwrite_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "existing.txt");
        out << "old content";
        out.close();

        auto tool = create_write_file_tool();
        json args;
        args["path"] = dir + "/existing.txt";
        args["content"] = "new content";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);

        std::ifstream in(fs::path(dir) / "existing.txt");
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        UNIT_TEST("overwrite_content_matches", content == "new content");

        safe_remove_all(dir);
    }

    // empty path returns error
    {
        LOG_INFO("write_file", "empty_path_returns_error");
        auto tool = create_write_file_tool();
        json args;
        args["path"] = "";
        args["content"] = "test";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("empty_path_returns_error", result.find("Error") != std::string::npos);
    }

    // empty content returns error
    {
        LOG_INFO("write_file", "empty_content_returns_error");
        auto tool = create_write_file_tool();
        json args;
        args["path"] = "/tmp/test.txt";
        args["content"] = "";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("empty_content_returns_error", result.find("Error") != std::string::npos);
    }

    // invalid JSON returns error
    {
        LOG_INFO("write_file", "invalid_json_returns_error");
        auto tool = create_write_file_tool();
        std::string args_str = "not json";
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("invalid_json_returns_error", result.find("Error") != std::string::npos);
    }

    // empty input returns error
    {
        LOG_INFO("write_file", "empty_input_returns_error");
        auto tool = create_write_file_tool();
        std::string args_str = "";
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("empty_input_returns_error", result.find("Error") != std::string::npos);
    }

    // write file with auto-created parent directory
    {
        LOG_INFO("write_file", "test_write_auto_dir_temp");
        std::string dir = "test_write_auto_dir_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        auto tool = create_write_file_tool();
        json args;
        args["path"] = dir + "/sub/deep/file.txt";
        args["content"] = "nested";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("auto_dir_success", result.find("Error") == std::string::npos);
        UNIT_TEST("auto_dir_file_exists", fs::exists(fs::path(dir) / "sub" / "deep" / "file.txt"));

        safe_remove_all(dir);
    }

    parent.report.push_back(unit);
}

// ── AppendFileTool ─────────────────────────────────────────────

void test_append_file_tools(UnitReport& parent)
{
    UnitReport unit("append_file");
    LOG_INFO("test_append_file_tools", "append_file");

    // append to existing file
    {
        LOG_INFO("append_file", "test_append_file_temp");
        std::string dir = "test_append_file_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "append.txt");
        out << "first line\n";
        out.close();

        auto tool = create_append_file_tool();
        UNIT_TEST("name_is_append_file", tool->name() == "append_file");

        json args;
        args["path"] = dir + "/append.txt";
        args["content"] = "second line\n";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);

        std::ifstream in(fs::path(dir) / "append.txt");
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        UNIT_TEST("contains_first_line", content.find("first line") != std::string::npos);
        UNIT_TEST("contains_second_line", content.find("second line") != std::string::npos);

        safe_remove_all(dir);
    }

    // append creates new file
    {
        LOG_INFO("append_file", "test_append_new_temp");
        std::string dir = "test_append_new_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        auto tool = create_append_file_tool();
        json args;
        args["path"] = dir + "/new.txt";
        args["content"] = "hello\n";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);

        UNIT_TEST("append_creates_new_file", fs::exists(fs::path(dir) / "new.txt"));

        safe_remove_all(dir);
    }

    // empty path returns error
    {
        LOG_INFO("append_file", "empty_path_returns_error");
        auto tool = create_append_file_tool();
        json args;
        args["path"] = "";
        args["content"] = "test";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("empty_path_returns_error", result.find("Error") != std::string::npos);
    }

    // invalid JSON returns error
    {
        LOG_INFO("append_file", "invalid_json_returns_error");
        auto tool = create_append_file_tool();
        std::string args_str = "not json";
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("invalid_json_returns_error", result.find("Error") != std::string::npos);
    }

    // empty input returns error
    {
        LOG_INFO("append_file", "empty_input_returns_error");
        auto tool = create_append_file_tool();
        std::string args_str = "";
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("empty_input_returns_error", result.find("Error") != std::string::npos);
    }

    // empty content returns error
    {
        LOG_INFO("append_file", "empty_content_returns_error");
        auto tool = create_append_file_tool();
        json args;
        args["path"] = "/tmp/test.txt";
        args["content"] = "";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("empty_content_returns_error", result.find("Error") != std::string::npos);
    }

    parent.report.push_back(unit);
}

// ── InsertFileContentTool ──────────────────────────────────────

void test_insert_file_content_tools(UnitReport& parent)
{
    UnitReport unit("insert_file_content");
    LOG_INFO("test_insert_file_content_tools", "insert_file_content");

    // insert at line 1
    {
        LOG_INFO("insert_file_content", "test_insert_file_temp");
        std::string dir = "test_insert_file_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "insert.txt");
        out << "line1\nline2\nline3\n";
        out.close();

        auto tool = create_insert_file_content_tool();
        UNIT_TEST("name_is_insert_file_content", tool->name() == "insert_file_content");

        json args;
        args["path"] = dir + "/insert.txt";
        args["line_number"] = 1;
        args["content"] = "new line\n";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);

        std::ifstream in(fs::path(dir) / "insert.txt");
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        UNIT_TEST("contains_new_line", content.find("new line") != std::string::npos);
        UNIT_TEST("still_contains_line1", content.find("line1") != std::string::npos);

        safe_remove_all(dir);
    }

    // insert at end
    {
        LOG_INFO("insert_file_content", "test_insert_end_temp");
        std::string dir = "test_insert_end_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "insert.txt");
        out << "line1\nline2\n";
        out.close();

        auto tool = create_insert_file_content_tool();
        json args;
        args["path"] = dir + "/insert.txt";
        args["line_number"] = 3;
        args["content"] = "appended\n";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);

        std::ifstream in(fs::path(dir) / "insert.txt");
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        UNIT_TEST("contains_appended", content.find("appended") != std::string::npos);

        safe_remove_all(dir);
    }

    // empty path returns error
    {
        LOG_INFO("insert_file_content", "empty_path_returns_error");
        auto tool = create_insert_file_content_tool();
        json args;
        args["path"] = "";
        args["line_number"] = 1;
        args["content"] = "test";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("empty_path_returns_error", result.find("Error") != std::string::npos);
    }

    // invalid JSON returns error
    {
        LOG_INFO("insert_file_content", "invalid_json_returns_error");
        auto tool = create_insert_file_content_tool();
        std::string args_str = "not json";
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("invalid_json_returns_error", result.find("Error") != std::string::npos);
    }

    // empty input returns error
    {
        LOG_INFO("insert_file_content", "empty_input_returns_error");
        auto tool = create_insert_file_content_tool();
        std::string args_str = "";
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("empty_input_returns_error", result.find("Error") != std::string::npos);
    }

    // insert at middle line (line_number=2)
    {
        LOG_INFO("insert_file_content", "test_insert_middle_temp");
        std::string dir = "test_insert_middle_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "insert.txt");
        out << "line1\nline2\nline3\n";
        out.close();

        auto tool = create_insert_file_content_tool();
        json args;
        args["path"] = dir + "/insert.txt";
        args["line_number"] = 2;
        args["content"] = "middle line\n";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);

        std::ifstream in(fs::path(dir) / "insert.txt");
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        UNIT_TEST("contains_middle_line", content.find("middle line") != std::string::npos);
        UNIT_TEST("still_contains_all_originals", content.find("line1") != std::string::npos && content.find("line2") != std::string::npos && content.find("line3") != std::string::npos);

        safe_remove_all(dir);
    }

    // nonexistent file returns error
    {
        LOG_INFO("insert_file_content", "nonexistent_returns_error");
        auto tool = create_insert_file_content_tool();
        json args;
        args["path"] = "/nonexistent/file.txt";
        args["line_number"] = 1;
        args["content"] = "test";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("nonexistent_returns_error", result.find("Error") != std::string::npos);
    }

    // empty content returns error
    {
        LOG_INFO("insert_file_content", "empty_content_returns_error");
        auto tool = create_insert_file_content_tool();
        json args;
        args["path"] = "/tmp/test.txt";
        args["line_number"] = 1;
        args["content"] = "";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("empty_content_returns_error", result.find("Error") != std::string::npos);
    }

    parent.report.push_back(unit);
}

// ── EditFileTool ───────────────────────────────────────────────

void test_edit_file_tools(UnitReport& parent)
{
    UnitReport unit("edit_file");
    LOG_INFO("test_edit_file_tools", "edit_file");

    // text replacement
    {
        LOG_INFO("edit_file", "test_edit_file_temp");
        std::string dir = "test_edit_file_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "edit.txt");
        out << "hello world\nfoo bar\n";
        out.close();

        auto tool = create_edit_file_tool();
        UNIT_TEST("name_is_edit_file", tool->name() == "edit_file");

        json args;
        args["path"] = dir + "/edit.txt";
        args["old_text"] = "hello world";
        args["new_text"] = "goodbye world";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("no_error_on_replace", result.find("Error") == std::string::npos);

        std::ifstream in(fs::path(dir) / "edit.txt");
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        UNIT_TEST("contains_goodbye", content.find("goodbye world") != std::string::npos);
        UNIT_TEST("no_hello", content.find("hello world") == std::string::npos);

        safe_remove_all(dir);
    }

    // nonexistent old_text returns error
    {
        LOG_INFO("edit_file", "test_edit_notfound_temp");
        std::string dir = "test_edit_notfound_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "edit.txt");
        out << "hello world\n";
        out.close();

        auto tool = create_edit_file_tool();
        json args;
        args["path"] = dir + "/edit.txt";
        args["old_text"] = "not found";
        args["new_text"] = "replacement";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("notfound_returns_error", result.find("Error") != std::string::npos);

        safe_remove_all(dir);
    }

    // empty path returns error
    {
        LOG_INFO("edit_file", "empty_path_returns_error");
        auto tool = create_edit_file_tool();
        json args;
        args["path"] = "";
        args["old_text"] = "a";
        args["new_text"] = "b";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("empty_path_returns_error", result.find("Error") != std::string::npos);
    }

    // invalid JSON returns error
    {
        LOG_INFO("edit_file", "invalid_json_returns_error");
        auto tool = create_edit_file_tool();
        std::string args_str = "not json";
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("invalid_json_returns_error", result.find("Error") != std::string::npos);
    }

    // empty input returns error
    {
        LOG_INFO("edit_file", "empty_input_returns_error");
        auto tool = create_edit_file_tool();
        std::string args_str = "";
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("empty_input_returns_error", result.find("Error") != std::string::npos);
    }

    // line-based mode: replace lines by start_line/end_line
    {
        LOG_INFO("edit_file", "test_edit_line_mode_temp");
        std::string dir = "test_edit_line_mode_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "edit.txt");
        out << "line1\nline2\nline3\nline4\n";
        out.close();

        auto tool = create_edit_file_tool();
        json args;
        args["path"] = dir + "/edit.txt";
        args["start_line"] = 2;
        args["end_line"] = 3;
        args["new_text"] = "replaced";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("line_mode_no_error", result.find("Error") == std::string::npos);

        std::ifstream in(fs::path(dir) / "edit.txt");
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        UNIT_TEST("line_mode_contains_replaced", content.find("replaced") != std::string::npos);
        UNIT_TEST("line_mode_no_line2", content.find("line2") == std::string::npos);

        safe_remove_all(dir);
    }

    // old_text matches multiple locations returns error
    {
        LOG_INFO("edit_file", "test_edit_multiple_matches_temp");
        std::string dir = "test_edit_multiple_matches_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "edit.txt");
        out << "hello world\nfoo bar\nhello world\n";
        out.close();

        auto tool = create_edit_file_tool();
        json args;
        args["path"] = dir + "/edit.txt";
        args["old_text"] = "hello world";
        args["new_text"] = "goodbye";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("multiple_matches_returns_error", result.find("matches multiple locations") != std::string::npos);

        safe_remove_all(dir);
    }

    // both old_text and start_line/end_line provided returns error
    {
        LOG_INFO("edit_file", "test_edit_both_modes_temp");
        std::string dir = "test_edit_both_modes_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "edit.txt");
        out << "hello world\n";
        out.close();

        auto tool = create_edit_file_tool();
        json args;
        args["path"] = dir + "/edit.txt";
        args["old_text"] = "hello";
        args["new_text"] = "bye";
        args["start_line"] = 1;
        args["end_line"] = 1;
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("both_modes_returns_error", result.find("Cannot use both") != std::string::npos);

        safe_remove_all(dir);
    }

    // end_line exceeds file length returns error
    {
        LOG_INFO("edit_file", "test_edit_exceeds_length_temp");
        std::string dir = "test_edit_exceeds_length_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "edit.txt");
        out << "line1\nline2\n";
        out.close();

        auto tool = create_edit_file_tool();
        json args;
        args["path"] = dir + "/edit.txt";
        args["start_line"] = 1;
        args["end_line"] = 5;
        args["new_text"] = "replaced";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("exceeds_length_returns_error", result.find("Error") != std::string::npos);

        safe_remove_all(dir);
    }

    // both old_text and new_text empty returns error
    {
        LOG_INFO("edit_file", "empty_texts_returns_error");
        auto tool = create_edit_file_tool();
        json args;
        args["path"] = "/tmp/test.txt";
        args["old_text"] = "";
        args["new_text"] = "";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("empty_texts_returns_error", result.find("Error") != std::string::npos);
    }

    parent.report.push_back(unit);
}

// ── EditFilesTool ──────────────────────────────────────────

void test_edit_files_tools(UnitReport& parent)
{

}

// ── ListDirectoryTool ──────────────────────────────────────────

void test_list_directory_tools(UnitReport& parent)
{
    UnitReport unit("list_directory");
    LOG_INFO("test_list_directory_tools", "list_directory");

    // list directory
    {
        LOG_INFO("list_directory", "test_list_dir_temp");
        std::string dir = "test_list_dir_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(fs::path(dir) / "subdir");

        std::ofstream out1(fs::path(dir) / "file1.txt");
        out1.close();
        std::ofstream out2(fs::path(dir) / "file2.cpp");
        out2.close();

        auto tool = create_list_directory_tool();
        UNIT_TEST("name_is_list_directory", tool->name() == "list_directory");

        json args;
        args["path"] = dir;
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("contains_file1", result.find("file1.txt") != std::string::npos);
        UNIT_TEST("contains_subdir", result.find("subdir") != std::string::npos);

        safe_remove_all(dir);
    }

    // nonexistent directory returns error
    {
        LOG_INFO("list_directory", "nonexistent_returns_error");
        auto tool = create_list_directory_tool();
        json args;
        args["path"] = "/nonexistent/dir";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("nonexistent_returns_error", result.find("Error") != std::string::npos);
    }

    // empty path returns error
    {
        LOG_INFO("list_directory", "empty_path_returns_error");
        auto tool = create_list_directory_tool();
        json args;
        args["path"] = "";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("empty_path_returns_error", result.find("Error") != std::string::npos);
    }

    // invalid JSON returns error
    {
        LOG_INFO("list_directory", "invalid_json_returns_error");
        auto tool = create_list_directory_tool();
        std::string args_str = "not json";
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("invalid_json_returns_error", result.find("Error") != std::string::npos);
    }

    // empty input returns error
    {
        LOG_INFO("list_directory", "empty_input_returns_error");
        auto tool = create_list_directory_tool();
        std::string args_str = "";
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("empty_input_returns_error", result.find("Error") != std::string::npos);
    }

    // empty directory returns success with no items
    {
        LOG_INFO("list_directory", "test_list_empty_dir_temp");
        std::string dir = "test_list_empty_dir_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        auto tool = create_list_directory_tool();
        json args;
        args["path"] = dir;
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("empty_dir_no_error", result.find("Error") == std::string::npos);

        safe_remove_all(dir);
    }

    parent.report.push_back(unit);
}

// ── ReadFilesTool ──────────────────────────────────────────────

void test_read_files_tools(UnitReport& parent)
{
    UnitReport unit("read_files");
    LOG_INFO("test_read_files_tools", "read_files");

    // read multiple files via string array paths (outline mode default)
    {
        LOG_INFO("read_files", "test_read_files_string_array_temp");
        std::string dir = "test_read_files_string_array_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out1(fs::path(dir) / "a.txt");
        out1 << "content of a\n";
        out1.close();

        std::ofstream out2(fs::path(dir) / "b.txt");
        out2 << "content of b\n";
        out2.close();

        auto tool = create_read_files_tool();
        UNIT_TEST("name_is_read_files", tool->name() == "read_files");

        json args;
        args["paths"] = {dir + "/a.txt", dir + "/b.txt"};
        args["outline"] = false;
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string raw_result = tool->execute(args_str);
        tool->show_result(raw_result);
        UNIT_TEST("has_file_header_a", raw_result.find("# File for") != std::string::npos);
        UNIT_TEST("success_count_is_2", (raw_result.find("content of a") != std::string::npos && raw_result.find("content of b") != std::string::npos));

        safe_remove_all(dir);
    }

    // read with object array (per-file options via 'files')
    {
        LOG_INFO("read_files", "test_read_files_object_array_temp");
        std::string dir = "test_read_files_object_array_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "range.txt");
        for (int i = 1; i <= 10; ++i)
            out << "line" << i << "\n";
        out.close();

        auto tool = create_read_files_tool();
        json args;
        json file_obj;
        file_obj["path"] = dir + "/range.txt";
        file_obj["outline"] = false;
        file_obj["start_line"] = 3;
        file_obj["end_line"] = 5;
        args["files"] = json::array({file_obj});
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string raw_result = tool->execute(args_str);
        tool->show_result(raw_result);
        UNIT_TEST("range_success", (raw_result.find("line3") != std::string::npos && raw_result.find("line5") != std::string::npos));

        safe_remove_all(dir);
    }

    // directory + glob mode
    {
        LOG_INFO("read_files", "test_read_files_glob_temp");
        std::string dir = "test_read_files_glob_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out1(fs::path(dir) / "x.txt");
        out1 << "txt content\n";
        out1.close();

        std::ofstream out2(fs::path(dir) / "y.cpp");
        out2 << "cpp content\n";
        out2.close();

        auto tool = create_read_files_tool();
        json args;
        args["directory"] = dir;
        args["glob"] = "*.txt";
        args["outline"] = false;
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string raw_result = tool->execute(args_str);
        tool->show_result(raw_result);
        UNIT_TEST("glob_success", raw_result.find("txt content") != std::string::npos);

        safe_remove_all(dir);
    }

    // nonexistent file in paths returns error entry
    {
        LOG_INFO("read_files", "nonexistent_file_error");
        auto tool = create_read_files_tool();
        json args;
        args["paths"] = {"/nonexistent/file.txt"};
        args["outline"] = false;
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string raw_result = tool->execute(args_str);
        tool->show_result(raw_result);
        UNIT_TEST("error_entry_present", raw_result.find("# Error:") != std::string::npos);
    }

    // missing required params returns error
    {
        LOG_INFO("read_files", "missing_params_returns_error");
        auto tool = create_read_files_tool();
        json args;
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("missing_params_returns_error", result.find("Error") != std::string::npos);
    }

    // missing outline returns error (outline is required)
    {
        LOG_INFO("read_files", "missing_outline_returns_error");
        auto tool = create_read_files_tool();
        json args;
        args["paths"] = {"/some/file.txt"};
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("missing_outline_returns_error", result.find("Error") != std::string::npos);
    }

    // invalid JSON returns error
    {
        LOG_INFO("read_files", "invalid_json_returns_error");
        auto tool = create_read_files_tool();
        std::string args_str = "not json";
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("invalid_json_returns_error", result.find("Error") != std::string::npos);
    }

    // empty input returns error
    {
        LOG_INFO("read_files", "empty_input_returns_error");
        auto tool = create_read_files_tool();
        std::string args_str = "";
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("empty_input_returns_error", result.find("Error") != std::string::npos);
    }

    // outline mode with actual code file (outline=true)
    {
        LOG_INFO("read_files", "test_read_files_outline_temp");
        std::string dir = "test_read_files_outline_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "outline.cpp");
        out << "void foo() {}\n"
            << "int bar(int x) { return x; }\n";
        out.close();

        auto tool = create_read_files_tool();
        json args;
        args["paths"] = {dir + "/outline.cpp"};
        args["outline"] = true;
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string raw_result = tool->execute(args_str);
        tool->show_result(raw_result);
        UNIT_TEST("outline_success", raw_result.find("foo") != std::string::npos);
        UNIT_TEST("outline_has_bar", raw_result.find("bar") != std::string::npos);

        safe_remove_all(dir);
    }

    // outline mode with start_line/end_line range
    {
        LOG_INFO("read_files", "test_read_files_outline_range_temp");
        std::string dir = "test_read_files_outline_range_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "range.cpp");
        for (int i = 1; i <= 20; ++i)
            out << "void func" << i << "() {}\n";
        out.close();

        auto tool = create_read_files_tool();
        json args;
        json file_obj;
        file_obj["path"] = dir + "/range.cpp";
        file_obj["outline"] = true;
        file_obj["start_line"] = 5;
        file_obj["end_line"] = 10;
        args["files"] = json::array({file_obj});
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string raw_result = tool->execute(args_str);
        tool->show_result(raw_result);
        UNIT_TEST("outline_range_has_func5", raw_result.find("func5") != std::string::npos);
        UNIT_TEST("outline_range_no_func1", raw_result.find("func1()") == std::string::npos);
        UNIT_TEST("outline_range_no_func15", raw_result.find("func15()") == std::string::npos);

        safe_remove_all(dir);
    }

    // content verification: paths mode reads correct content
    {
        LOG_INFO("read_files", "test_read_files_content_verify_temp");
        std::string dir = "test_read_files_content_verify_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "hello.txt");
        out << "hello world\n";
        out.close();

        auto tool = create_read_files_tool();
        json args;
        args["paths"] = {dir + "/hello.txt"};
        args["outline"] = false;
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string raw_result = tool->execute(args_str);
        tool->show_result(raw_result);
        UNIT_TEST("content_verify_has_hello", raw_result.find("hello world") != std::string::npos);

        safe_remove_all(dir);
    }

    // directory+glob with outline=true
    {
        LOG_INFO("read_files", "test_read_files_glob_outline_temp");
        std::string dir = "test_read_files_glob_outline_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "a.cpp");
        out << "void alpha() {}\n";
        out.close();

        auto tool = create_read_files_tool();
        json args;
        args["directory"] = dir;
        args["glob"] = "*.cpp";
        args["outline"] = true;
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string raw_result = tool->execute(args_str);
        tool->show_result(raw_result);
        UNIT_TEST("glob_outline_has_alpha", raw_result.find("alpha") != std::string::npos);

        safe_remove_all(dir);
    }

    // per-file outline in object array (files mode)
    {
        LOG_INFO("read_files", "test_read_files_perfile_outline_temp");
        std::string dir = "test_read_files_perfile_outline_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out1(fs::path(dir) / "a.cpp");
        out1 << "void foo() {}\n";
        out1.close();

        std::ofstream out2(fs::path(dir) / "b.txt");
        out2 << "plain text\n";
        out2.close();

        auto tool = create_read_files_tool();
        json args;
        json f1, f2;
        f1["path"] = dir + "/a.cpp";
        f1["outline"] = true;
        f2["path"] = dir + "/b.txt";
        f2["outline"] = false;
        args["files"] = json::array({f1, f2});
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string raw_result = tool->execute(args_str);
        tool->show_result(raw_result);
        UNIT_TEST("perfile_outline_has_foo", raw_result.find("foo") != std::string::npos);
        UNIT_TEST("perfile_content_has_plain", raw_result.find("plain text") != std::string::npos);

        safe_remove_all(dir);
    }

    // mixed success and failure paths
    {
        LOG_INFO("read_files", "test_read_files_mixed_temp");
        std::string dir = "test_read_files_mixed_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "a.txt");
        out << "content of a\n";
        out.close();

        auto tool = create_read_files_tool();
        json args;
        args["paths"] = {dir + "/a.txt", "/nonexistent/b.txt"};
        args["outline"] = false;
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string raw_result = tool->execute(args_str);
        tool->show_result(raw_result);
        UNIT_TEST("mixed_success_count", raw_result.find("content of a") != std::string::npos);
        UNIT_TEST("mixed_error_count", raw_result.find("# Error:") != std::string::npos);

        safe_remove_all(dir);
    }

    // combined modes: paths + files + directory/glob in one call
    {
        LOG_INFO("read_files", "test_read_files_combined_temp");
        std::string dir = "test_read_files_combined_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(fs::path(dir) / "subdir");

        // file for paths mode
        std::ofstream out1(fs::path(dir) / "p.txt");
        out1 << "paths content\n";
        out1.close();

        // file for files mode (line range)
        std::ofstream out2(fs::path(dir) / "f.txt");
        for (int i = 1; i <= 5; ++i)
            out2 << "line" << i << "\n";
        out2.close();

        // file for directory+glob mode
        std::ofstream out3(fs::path(dir) / "subdir" / "g.txt");
        out3 << "glob content\n";
        out3.close();

        auto tool = create_read_files_tool();
        json args;
        // Mode 1: paths
        args["paths"] = {dir + "/p.txt"};
        args["outline"] = false;
        // Mode 2: files (per-file options)
        {
            json file_obj;
            file_obj["path"] = dir + "/f.txt";
            file_obj["start_line"] = 3;
            file_obj["end_line"] = 4;
            args["files"] = json::array({file_obj});
        }
        // Mode 3: directory+glob
        args["directory"] = dir + "/subdir";
        args["glob"] = "*.txt";

        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string raw_result = tool->execute(args_str);
        tool->show_result(raw_result);
        UNIT_TEST("combined_has_paths_content", raw_result.find("paths content") != std::string::npos);
        UNIT_TEST("combined_has_line_range", raw_result.find("line3") != std::string::npos);
        UNIT_TEST("combined_has_glob_content", raw_result.find("glob content") != std::string::npos);

        safe_remove_all(dir);
    }

    parent.report.push_back(unit);
}

// ── DeleteFilesTool ────────────────────────────────────────────

void test_delete_files_tools(UnitReport& parent)
{
    UnitReport unit("delete_files");
    LOG_INFO("test_delete_files_tools", "delete_files");

    // delete files via paths
    {
        LOG_INFO("delete_files", "test_delete_paths_temp");
        std::string dir = "test_delete_paths_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out1(fs::path(dir) / "del1.txt");
        out1 << "to delete\n";
        out1.close();

        std::ofstream out2(fs::path(dir) / "keep.txt");
        out2 << "keep me\n";
        out2.close();

        auto tool = create_delete_files_tool();
        UNIT_TEST("name_is_delete_files", tool->name() == "delete_files");

        json args;
        args["paths"] = {dir + "/del1.txt"};
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("deleted_file_gone", !fs::exists(fs::path(dir) / "del1.txt"));
        UNIT_TEST("kept_file_exists", fs::exists(fs::path(dir) / "keep.txt"));

        safe_remove_all(dir);
    }

    // dry_run mode does not delete
    {
        LOG_INFO("delete_files", "test_delete_dryrun_temp");
        std::string dir = "test_delete_dryrun_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "dry.txt");
        out << "not deleted\n";
        out.close();

        auto tool = create_delete_files_tool();
        json args;
        args["paths"] = {dir + "/dry.txt"};
        args["dry_run"] = true;
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("dryrun_file_still_exists", fs::exists(fs::path(dir) / "dry.txt"));
        UNIT_TEST("dryrun_message_present", result.find("Dry run mode") != std::string::npos);

        safe_remove_all(dir);
    }

    // directory + glob delete
    {
        LOG_INFO("delete_files", "test_delete_glob_temp");
        std::string dir = "test_delete_glob_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out1(fs::path(dir) / "a.log");
        out1 << "log a\n";
        out1.close();

        std::ofstream out2(fs::path(dir) / "b.txt");
        out2 << "txt b\n";
        out2.close();

        auto tool = create_delete_files_tool();
        json args;
        args["directory"] = dir;
        args["glob"] = "*.log";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        tool->execute(args_str);
        tool->show_result("");
        UNIT_TEST("glob_deleted_log", !fs::exists(fs::path(dir) / "a.log"));
        UNIT_TEST("glob_kept_txt", fs::exists(fs::path(dir) / "b.txt"));

        safe_remove_all(dir);
    }

    // nonexistent file in paths returns error entry
    {
        LOG_INFO("delete_files", "nonexistent_file_error");
        auto tool = create_delete_files_tool();
        json args;
        args["paths"] = {"/nonexistent/file.txt"};
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("error_entry_present", result.find("File not found") != std::string::npos);
    }

    // missing required params returns error
    {
        LOG_INFO("delete_files", "missing_params_returns_error");
        auto tool = create_delete_files_tool();
        json args;
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("missing_params_returns_error", result.find("Error") != std::string::npos);
    }

    // invalid JSON returns error
    {
        LOG_INFO("delete_files", "invalid_json_returns_error");
        auto tool = create_delete_files_tool();
        std::string args_str = "not json";
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("invalid_json_returns_error", result.find("Error") != std::string::npos);
    }

    // empty input returns error
    {
        LOG_INFO("delete_files", "empty_input_returns_error");
        auto tool = create_delete_files_tool();
        std::string args_str = "";
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("empty_input_returns_error", result.find("Error") != std::string::npos);
    }

    parent.report.push_back(unit);
}

// ── WriteFilesTool ─────────────────────────────────────────────

void test_write_files_tools(UnitReport& parent)
{
    UnitReport unit("write_files");
    LOG_INFO("test_write_files_tools", "write_files");

    // write multiple files
    {
        LOG_INFO("write_files", "test_write_multi_temp");
        std::string dir = "test_write_multi_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        auto tool = create_write_files_tool();
        UNIT_TEST("name_is_write_files", tool->name() == "write_files");

        json args;
        json f1, f2;
        f1["path"] = dir + "/f1.txt";
        f1["content"] = "hello";
        f2["path"] = dir + "/f2.txt";
        f2["content"] = "world";
        args["files"] = json::array({f1, f2});
        std::string result = tool->execute(args.dump());
        UNIT_TEST("no_error_on_write", result.find("Error") == std::string::npos);

        UNIT_TEST("f1_exists", fs::exists(fs::path(dir) / "f1.txt"));
        UNIT_TEST("f2_exists", fs::exists(fs::path(dir) / "f2.txt"));

        std::ifstream in1(fs::path(dir) / "f1.txt");
        std::string content1((std::istreambuf_iterator<char>(in1)), std::istreambuf_iterator<char>());
        UNIT_TEST("f1_content_matches", content1 == "hello");

        safe_remove_all(dir);
    }

    // write file with auto-created parent directory
    {
        LOG_INFO("write_files", "test_write_auto_dir_temp");
        std::string dir = "test_write_auto_dir_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        auto tool = create_write_files_tool();
        json args;
        json f1;
        f1["path"] = dir + "/sub/deep/f.txt";
        f1["content"] = "nested";
        args["files"] = json::array({f1});
        std::string result = tool->execute(args.dump());
        UNIT_TEST("auto_dir_file_exists", fs::exists(fs::path(dir) / "sub" / "deep" / "f.txt"));

        safe_remove_all(dir);
    }

    // empty path in files array returns error entry
    {
        LOG_INFO("write_files", "empty_path_error_entry");
        auto tool = create_write_files_tool();
        json args;
        json f1;
        f1["path"] = "";
        f1["content"] = "test";
        args["files"] = json::array({f1});
        std::string result = tool->execute(args.dump());
        UNIT_TEST("empty_path_error_entry", result.find("No file path provided") != std::string::npos);
    }

    // missing files array returns error
    {
        LOG_INFO("write_files", "missing_files_returns_error");
        auto tool = create_write_files_tool();
        json args;
        std::string result = tool->execute(args.dump());
        UNIT_TEST("missing_files_returns_error", result.find("Error") != std::string::npos);
    }

    // invalid JSON returns error
    {
        LOG_INFO("write_files", "invalid_json_returns_error");
        auto tool = create_write_files_tool();
        std::string result = tool->execute("not json");
        UNIT_TEST("invalid_json_returns_error", result.find("Error") != std::string::npos);
    }

    // empty input returns error
    {
        LOG_INFO("write_files", "empty_input_returns_error");
        auto tool = create_write_files_tool();
        std::string result = tool->execute("");
        UNIT_TEST("empty_input_returns_error", result.find("Error") != std::string::npos);
    }

    parent.report.push_back(unit);
}

// ── Entry point ────────────────────────────────────────────────

void test_file_tool(UnitReport& parent)
{
    // Disable SafetyGuard for tests — empty whitelist + no working dir = allow all paths.
    auto& sg = SafetyGuard::get_instance();
    sg.reset_path_whitelist();
    sg.set_working_directory("");

    UnitReport unit("file_tool");
    LOG_INFO("test_file_tool", "file_tool");

    test_read_file_tools(unit);
    test_write_file_tools(unit);
    test_append_file_tools(unit);
    test_insert_file_content_tools(unit);
    test_edit_file_tools(unit);
    test_edit_files_tools(unit);
    test_list_directory_tools(unit);
    test_read_files_tools(unit);
    test_delete_files_tools(unit);
    test_write_files_tools(unit);

    parent.report.push_back(unit);
}
