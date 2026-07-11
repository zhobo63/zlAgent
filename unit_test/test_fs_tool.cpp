#include "pch.h"
#include "unit_test.h"

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

// ── CreateDirectoryTool ───────────────────────────────────────

void test_create_directory_tool(UnitReport& parent)
{
    UnitReport unit("create_directory");
    LOG_INFO("test_create_directory", "create_directory");

    // basic directory creation
    {
        LOG_INFO("create_directory", "basic_success");
        std::string dir = "test_cd_basic_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        auto tool = create_create_directory_tool();
        UNIT_TEST("name_is_create_directory", tool->name() == "create_directory");

        json args;
        args["path"] = dir + "/new_folder";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("basic_success", result.find("Successfully created") != std::string::npos);
        UNIT_TEST("dir_exists", fs::exists(fs::path(dir) / "new_folder"));

        safe_remove_all(dir);
    }

    // directory already exists — no error
    {
        LOG_INFO("create_directory", "already_exists_no_error");
        std::string dir = "test_cd_exists_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(fs::path(dir) / "existing_folder");

        auto tool = create_create_directory_tool();
        json args;
        args["path"] = dir + "/existing_folder";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("already_exists_no_error", result.find("Error") == std::string::npos);

        safe_remove_all(dir);
    }

    // nested directory creation (mkdir -p behavior)
    {
        LOG_INFO("create_directory", "nested_success");
        std::string dir = "test_cd_nested_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        auto tool = create_create_directory_tool();
        json args;
        args["path"] = dir + "/a/b/c";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("nested_success", result.find("Successfully created") != std::string::npos);
        UNIT_TEST("nested_dir_exists", fs::exists(fs::path(dir) / "a" / "b" / "c"));

        safe_remove_all(dir);
    }

    // empty path returns error
    {
        LOG_INFO("create_directory", "empty_path_returns_error");
        auto tool = create_create_directory_tool();
        json args;
        args["path"] = "";
        std::string result = tool->execute(args.dump());
        UNIT_TEST("empty_path_returns_error", result.find("Error") != std::string::npos);
    }

    // invalid JSON returns error
    {
        LOG_INFO("create_directory", "invalid_json_returns_error");
        auto tool = create_create_directory_tool();
        std::string result = tool->execute("not json");
        UNIT_TEST("invalid_json_returns_error", result.find("Error") != std::string::npos);
    }

    // empty input returns error
    {
        LOG_INFO("create_directory", "empty_input_returns_error");
        auto tool = create_create_directory_tool();
        std::string result = tool->execute("");
        UNIT_TEST("empty_input_returns_error", result.find("Error") != std::string::npos);
    }

    parent.report.push_back(unit);
}

// ── DeletePathTool ───────────────────────────────────────

void test_delete_path_tool(UnitReport& parent)
{
    UnitReport unit("delete_path");
    LOG_INFO("test_delete_path", "delete_path");

    // delete existing file — success
    {
        LOG_INFO("delete_path", "delete_file_success");
        std::string dir = "test_dp_file_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);
        
        // create a test file
        {
            std::ofstream f(fs::path(dir) / "test.txt");
            f << "hello world";
        }
        UNIT_TEST("file_exists_before_delete", fs::exists(fs::path(dir) / "test.txt"));

        auto tool = create_delete_path_tool();
        json args;
        args["path"] = (fs::path(dir) / "test.txt").string();
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("delete_file_success", result.find("Successfully deleted") != std::string::npos);
        UNIT_TEST("file_does_not_exist_after_delete", !fs::exists(fs::path(dir) / "test.txt"));

        safe_remove_all(dir);
    }

    // delete existing directory — success (recursive)
    {
        LOG_INFO("delete_path", "delete_directory_success");
        std::string dir = "test_dp_dir_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        
        // create nested directories with files
        fs::create_directories(fs::path(dir) / "subdir1" / "subdir2");
        {
            std::ofstream f(fs::path(dir) / "file1.txt");
            f << "content1";
        }
        {
            std::ofstream f(fs::path(dir) / "subdir1" / "file2.txt");
            f << "content2";
        }
        UNIT_TEST("directory_exists_before_delete", fs::exists(dir));

        auto tool = create_delete_path_tool();
        json args;
        args["path"] = dir;
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("delete_directory_success", result.find("Successfully deleted") != std::string::npos);
        UNIT_TEST("directory_does_not_exist_after_delete", !fs::exists(dir));
    }

    // delete non-existent path — error
    {
        LOG_INFO("delete_path", "non_existent_path_error");
        auto tool = create_delete_path_tool();
        json args;
        args["path"] = "test_dp_nonexistent_temp";
        std::string result = tool->execute(args.dump());
        UNIT_TEST("non_existent_path_returns_error", result.find("Error") != std::string::npos);
    }

    // empty path returns error
    {
        LOG_INFO("delete_path", "empty_path_returns_error");
        auto tool = create_delete_path_tool();
        json args;
        args["path"] = "";
        std::string result = tool->execute(args.dump());
        UNIT_TEST("empty_path_returns_error", result.find("Error") != std::string::npos);
    }

    // invalid JSON returns error
    {
        LOG_INFO("delete_path", "invalid_json_returns_error");
        auto tool = create_delete_path_tool();
        std::string result = tool->execute("not json");
        UNIT_TEST("invalid_json_returns_error", result.find("Error") != std::string::npos);
    }

    // empty input returns error
    {
        LOG_INFO("delete_path", "empty_input_returns_error");
        auto tool = create_delete_path_tool();
        std::string result = tool->execute("");
        UNIT_TEST("empty_input_returns_error", result.find("Error") != std::string::npos);
    }

    parent.report.push_back(unit);
}

// ── CopyPathTool ───────────────────────────────────────

void test_copy_path_tool(UnitReport& parent)
{
    UnitReport unit("copy_path");
    LOG_INFO("test_copy_path", "copy_path");

    // copy existing file — success
    {
        LOG_INFO("copy_path", "copy_file_success");
        std::string dir = "test_cp_file_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);
        
        // create a test file
        {
            std::ofstream f(fs::path(dir) / "source.txt");
            f << "hello world";
        }
        UNIT_TEST("file_exists_before_copy", fs::exists(fs::path(dir) / "source.txt"));

        auto tool = create_copy_path_tool();
        json args;
        args["source_path"] = (fs::path(dir) / "source.txt").string();
        args["destination_path"] = (fs::path(dir) / "dest.txt").string();
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("copy_file_success", result.find("Successfully copied") != std::string::npos);
        UNIT_TEST("source_exists_after_copy", fs::exists(fs::path(dir) / "source.txt"));
        UNIT_TEST("dest_exists_after_copy", fs::exists(fs::path(dir) / "dest.txt"));

        // verify content is the same
        {
            std::ifstream src_f(fs::path(dir) / "source.txt");
            std::string src_content((std::istreambuf_iterator<char>(src_f)), std::istreambuf_iterator<char>());
            std::ifstream dst_f(fs::path(dir) / "dest.txt");
            std::string dst_content((std::istreambuf_iterator<char>(dst_f)), std::istreambuf_iterator<char>());
            UNIT_TEST("content_same", src_content == dst_content);
        }

        safe_remove_all(dir);
    }

    // copy existing directory — success (recursive)
    {
        LOG_INFO("copy_path", "copy_directory_success");
        std::string dir = "test_cp_dir_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        
        // create source directory with files
        fs::create_directories(fs::path(dir) / "src_dir" / "src_subdir");
        {
            std::ofstream f(fs::path(dir) / "src_dir" / "src_file.txt");
            f << "content1";
        }
        {
            std::ofstream f(fs::path(dir) / "src_dir" / "src_subdir" / "nested_file.txt");
            f << "content2";
        }

        auto tool = create_copy_path_tool();
        json args;
        args["source_path"] = (fs::path(dir) / "src_dir").string();
        args["destination_path"] = (fs::path(dir) / "dst_dir").string();
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("copy_directory_success", result.find("Successfully copied") != std::string::npos);

        // verify directory structure is preserved
        UNIT_TEST("src_dir_exists_after_copy", fs::exists(fs::path(dir) / "src_dir"));
        UNIT_TEST("dst_dir_exists_after_copy", fs::exists(fs::path(dir) / "dst_dir"));
        UNIT_TEST("dst_file_exists", fs::exists(fs::path(dir) / "dst_dir" / "src_file.txt"));
        UNIT_TEST("dst_nested_file_exists", fs::exists(fs::path(dir) / "dst_dir" / "src_subdir" / "nested_file.txt"));

        safe_remove_all(dir);
    }

    // copy non-existent source — error
    {
        LOG_INFO("copy_path", "non_existent_source_error");
        auto tool = create_copy_path_tool();
        json args;
        args["source_path"] = "test_cp_nonexistent_temp";
        args["destination_path"] = "test_cp_dest_temp";
        std::string result = tool->execute(args.dump());
        UNIT_TEST("non_existent_source_returns_error", result.find("Error") != std::string::npos);
    }

    // copy to existing destination — error
    {
        LOG_INFO("copy_path", "existing_destination_error");
        std::string dir = "test_cp_dest_exists_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);
        
        // create both source and destination files
        {
            std::ofstream f(fs::path(dir) / "source.txt");
            f << "content1";
        }
        {
            std::ofstream f(fs::path(dir) / "dest.txt");
            f << "content2";
        }

        auto tool = create_copy_path_tool();
        json args;
        args["source_path"] = (fs::path(dir) / "source.txt").string();
        args["destination_path"] = (fs::path(dir) / "dest.txt").string();
        std::string result = tool->execute(args.dump());
        UNIT_TEST("existing_destination_returns_error", result.find("Error") != std::string::npos);

        safe_remove_all(dir);
    }

    // empty source_path returns error
    {
        LOG_INFO("copy_path", "empty_source_error");
        auto tool = create_copy_path_tool();
        json args;
        args["source_path"] = "";
        args["destination_path"] = "test_cp_dest_temp";
        std::string result = tool->execute(args.dump());
        UNIT_TEST("empty_source_returns_error", result.find("Error") != std::string::npos);
    }

    // empty destination_path returns error
    {
        LOG_INFO("copy_path", "empty_destination_error");
        auto tool = create_copy_path_tool();
        json args;
        args["source_path"] = "test_cp_source_temp";
        args["destination_path"] = "";
        std::string result = tool->execute(args.dump());
        UNIT_TEST("empty_destination_returns_error", result.find("Error") != std::string::npos);
    }

    // invalid JSON returns error
    {
        LOG_INFO("copy_path", "invalid_json_returns_error");
        auto tool = create_copy_path_tool();
        std::string result = tool->execute("not json");
        UNIT_TEST("invalid_json_returns_error", result.find("Error") != std::string::npos);
    }

    // empty input returns error
    {
        LOG_INFO("copy_path", "empty_input_returns_error");
        auto tool = create_copy_path_tool();
        std::string result = tool->execute("");
        UNIT_TEST("empty_input_returns_error", result.find("Error") != std::string::npos);
    }

    parent.report.push_back(unit);
}

// ── Entry point ────────────────────────────────────────────────

void test_fs_tool(UnitReport& parent)
{
    // Disable SafetyGuard for tests.
    auto& sg = SafetyGuard::get_instance();
    sg.reset_path_whitelist();
    sg.set_working_directory("");

    UnitReport unit("fs_tools");
    LOG_INFO("test_fs_tools", "fs_tools");

    test_create_directory_tool(unit);
    test_delete_path_tool(unit);
    test_copy_path_tool(unit);

    parent.report.push_back(unit);
}
