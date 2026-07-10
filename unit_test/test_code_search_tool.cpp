#include "pch.h"
#include "unit_test.h"
#include "tools.h"

using namespace agent;
namespace fs = std::filesystem;
using json = nlohmann::json;

static void safe_remove_all(const std::string& path)
{
    try {
        if (fs::exists(path)) fs::remove_all(path);
    } catch (...) {}
}

// ── CodeSearchTool ─────────────────────────────────────────────

void test_code_search_tools(UnitReport& parent)
{
    UnitReport unit("code_search_tools");
    LOG_INFO("test_code_search_tools", "code_search_tools");

    // basic search matches
    {
        LOG_INFO("code_search", "basic_search_matches");
        std::string dir = "test_cs_basic_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "hello.cpp");
        out << "int main() {\n    return 0;\n}"
            << std::endl;
        out.close();

        auto tool = create_code_search_tool();
        json args;
        args["pattern"] = "main";
        args["directory"] = dir;
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("basic_search_matches", result.find("int main()") != std::string::npos);

        safe_remove_all(dir);
    }

    // case insensitive match
    {
        LOG_INFO("code_search", "case_insensitive_match");
        std::string dir = "test_cs_case_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "hello.cpp");
        out << "INT MAIN() {\n    RETURN 0;\n}"
            << std::endl;
        out.close();

        auto tool = create_code_search_tool();
        json args;
        args["pattern"] = "int main";
        args["directory"] = dir;
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("case_insensitive_match", result.find("INT MAIN()") != std::string::npos);

        safe_remove_all(dir);
    }

    // regex pattern match
    {
        LOG_INFO("code_search", "regex_pattern_match");
        std::string dir = "test_cs_regex_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "hello.cpp");
        out << "std::vector<int> v;\n"
            << "std::string s;\n"
            << "int x;\n";
        out.close();

        auto tool = create_code_search_tool();
        json args;
        args["pattern"] = R"(std::(vector|string))";
        args["directory"] = dir;
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("regex_vector_match", result.find("std::vector<int>") != std::string::npos);
        UNIT_TEST("regex_string_match", result.find("std::string s") != std::string::npos);

        safe_remove_all(dir);
    }

    // no matches found
    {
        LOG_INFO("code_search", "no_matches_found");
        std::string dir = "test_cs_nomatch_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "hello.cpp");
        out << "int main() { return 0; }"
            << std::endl;
        out.close();

        auto tool = create_code_search_tool();
        json args;
        args["pattern"] = "nonexistent_pattern_xyz";
        args["directory"] = dir;
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("no_matches_found", result.find("No matches found") != std::string::npos);

        safe_remove_all(dir);
    }

    // file filter: *.cpp only
    {
        LOG_INFO("code_search", "file_filter_cpp_only");
        std::string dir = "test_cs_filter_cpp_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out1(fs::path(dir) / "hello.cpp");
        out1 << "int main() { return 0; }"
             << std::endl;
        out1.close();

        std::ofstream out2(fs::path(dir) / "hello.h");
        out2 << "void foo();\n";
        out2.close();

        auto tool = create_code_search_tool();
        json args;
        args["pattern"] = ".*";
        args["directory"] = dir;
        args["file_pattern"] = "*.cpp";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("filter_cpp_has_main", result.find("int main()") != std::string::npos);
        UNIT_TEST("filter_cpp_no_foo", result.find("void foo()") == std::string::npos);

        safe_remove_all(dir);
    }

    // file filter: *.h only
    {
        LOG_INFO("code_search", "file_filter_h_only");
        std::string dir = "test_cs_filter_h_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out1(fs::path(dir) / "hello.cpp");
        out1 << "int main() { return 0; }"
             << std::endl;
        out1.close();

        std::ofstream out2(fs::path(dir) / "hello.h");
        out2 << "void foo();\n";
        out2.close();

        auto tool = create_code_search_tool();
        json args;
        args["pattern"] = ".*";
        args["directory"] = dir;
        args["file_pattern"] = "*.h";
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("filter_h_no_main", result.find("int main()") == std::string::npos);
        UNIT_TEST("filter_h_has_foo", result.find("void foo()") != std::string::npos);

        safe_remove_all(dir);
    }

    // no file filter: all types searched
    {
        LOG_INFO("code_search", "no_file_filter_all_types");
        std::string dir = "test_cs_nofilter_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out1(fs::path(dir) / "hello.cpp");
        out1 << "int main() { return 0; }"
             << std::endl;
        out1.close();

        std::ofstream out2(fs::path(dir) / "hello.h");
        out2 << "void foo();\n";
        out2.close();

        auto tool = create_code_search_tool();
        json args;
        args["pattern"] = ".*";
        args["directory"] = dir;
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("nofilter_has_main", result.find("int main()") != std::string::npos);
        UNIT_TEST("nofilter_has_foo", result.find("void foo()") != std::string::npos);

        safe_remove_all(dir);
    }

    // recursive search in subdirectory
    {
        LOG_INFO("code_search", "recursive_search_subdir");
        std::string dir = "test_cs_recursive_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(fs::path(dir) / "sub" / "deep");

        std::ofstream out1(fs::path(dir) / "top.cpp");
        out1 << "int top();\n";
        out1.close();

        std::ofstream out2(fs::path(dir) / "sub" / "mid.cpp");
        out2 << "int mid();\n";
        out2.close();

        std::ofstream out3(fs::path(dir) / "sub" / "deep" / "bottom.cpp");
        out3 << "int bottom();\n";
        out3.close();

        auto tool = create_code_search_tool();
        json args;
        args["pattern"] = ".*";
        args["directory"] = dir;
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("recursive_has_top", result.find("int top()") != std::string::npos);
        UNIT_TEST("recursive_has_mid", result.find("int mid()") != std::string::npos);
        UNIT_TEST("recursive_has_bottom", result.find("int bottom()") != std::string::npos);

        safe_remove_all(dir);
    }

    // skip hidden directories
    {
        LOG_INFO("code_search", "skip_hidden_directories");
        std::string dir = "test_cs_hidden_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(fs::path(dir) / ".git");

        std::ofstream out1(fs::path(dir) / "hello.cpp");
        out1 << "int main();\n";
        out1.close();

        std::ofstream out2(fs::path(dir) / ".git" / "config.txt");
        out2 << "secret_data\n";
        out2.close();

        auto tool = create_code_search_tool();
        json args;
        args["pattern"] = ".*";
        args["directory"] = dir;
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("hidden_has_main", result.find("int main()") != std::string::npos);
        UNIT_TEST("hidden_no_secret", result.find("secret_data") == std::string::npos);

        safe_remove_all(dir);
    }

    // ignore .gitignore directories
    {
        LOG_INFO("code_search", "ignore_gitignore_dirs");
        std::string dir = "test_cs_gitignore_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(fs::path(dir) / "build");

        // Create .gitignore with "build/"
        std::ofstream gitignore(fs::path(dir) / ".gitignore");
        gitignore << "build/\n";
        gitignore.close();

        std::ofstream out1(fs::path(dir) / "hello.cpp");
        out1 << "int main();\n";
        out1.close();

        std::ofstream out2(fs::path(dir) / "build" / "output.o");
        out2 << "binary_data\n";
        out2.close();

        auto tool = create_code_search_tool();
        json args;
        args["pattern"] = ".*";
        args["directory"] = dir;
        auto args_str = args.dump();
        tool->show_arguments(args_str);
        tool->show_preview(args_str);
        std::string result = tool->execute(args_str);
        tool->show_result(result);
        UNIT_TEST("gitignore_has_main", result.find("int main()") != std::string::npos);
        UNIT_TEST("gitignore_no_binary", result.find("binary_data") == std::string::npos);

        safe_remove_all(dir);
    }

    // empty pattern returns error
    {
        LOG_INFO("code_search", "empty_pattern_returns_error");
        auto tool = create_code_search_tool();
        json args;
        args["pattern"] = "";
        std::string result = tool->execute(args.dump());
        UNIT_TEST("empty_pattern_returns_error", result.find("Error") != std::string::npos);
    }

    // invalid regex returns error
    {
        LOG_INFO("code_search", "invalid_regex_returns_error");
        auto tool = create_code_search_tool();
        json args;
        args["pattern"] = "[";  // unclosed bracket
        std::string result = tool->execute(args.dump());
        UNIT_TEST("invalid_regex_returns_error", result.find("Invalid regex") != std::string::npos);
    }

    // invalid JSON returns error
    {
        LOG_INFO("code_search", "invalid_json_returns_error");
        auto tool = create_code_search_tool();
        std::string result = tool->execute("not json");
        UNIT_TEST("invalid_json_returns_error", result.find("Error") != std::string::npos);
    }

    // empty input returns error
    {
        LOG_INFO("code_search", "empty_input_returns_error");
        auto tool = create_code_search_tool();
        std::string result = tool->execute("");
        UNIT_TEST("empty_input_returns_error", result.find("Error") != std::string::npos);
    }

    parent.report.push_back(unit);
}
