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

// ── ProjectOverviewTool ────────────────────────────────────────

void test_overview_tool(UnitReport& parent)
{
    UnitReport unit("overview_tools");
    LOG_INFO("test_overview_tools", "overview_tools");

    // basic overview returns header
    {
        LOG_INFO("overview", "basic_header");
        std::string dir = "test_ov_basic_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        auto tool = create_project_overview_tool();
        json args;
        args["directory"] = dir;
        std::string result = tool->execute(args.dump());
        UNIT_TEST("basic_header", result.find("PROJECT OVERVIEW") != std::string::npos);

        safe_remove_all(dir);
    }

    // build system detection: CMakeLists.txt
    {
        LOG_INFO("overview", "detect_cmake");
        std::string dir = "test_ov_cmake_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "CMakeLists.txt");
        out << "cmake_minimum_required(VERSION 3.10)\n";
        out.close();

        auto tool = create_project_overview_tool();
        json args;
        args["directory"] = dir;
        std::string result = tool->execute(args.dump());
        UNIT_TEST("detect_cmake", result.find("CMake") != std::string::npos);

        safe_remove_all(dir);
    }

    // build system detection: Makefile
    {
        LOG_INFO("overview", "detect_make");
        std::string dir = "test_ov_make_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "Makefile");
        out << "all:\n\t@echo done\n";
        out.close();

        auto tool = create_project_overview_tool();
        json args;
        args["directory"] = dir;
        std::string result = tool->execute(args.dump());
        UNIT_TEST("detect_make", result.find("Make") != std::string::npos);

        safe_remove_all(dir);
    }

    // build system detection: package.json
    {
        LOG_INFO("overview", "detect_npm");
        std::string dir = "test_ov_npm_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "package.json");
        out << "{\"name\": \"test\", \"version\": \"1.0.0\"}\n";
        out.close();

        auto tool = create_project_overview_tool();
        json args;
        args["directory"] = dir;
        std::string result = tool->execute(args.dump());
        UNIT_TEST("detect_npm", result.find("Node.js") != std::string::npos);

        safe_remove_all(dir);
    }

    // no build system detected
    {
        LOG_INFO("overview", "no_build_system");
        std::string dir = "test_ov_nobuild_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        auto tool = create_project_overview_tool();
        json args;
        args["directory"] = dir;
        std::string result = tool->execute(args.dump());
        UNIT_TEST("no_build_system", result.find("No known build system detected") != std::string::npos);

        safe_remove_all(dir);
    }

    // code metrics: counts files by extension
    {
        LOG_INFO("overview", "code_metrics_counts");
        std::string dir = "test_ov_metrics_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        // Create 2 .cpp files and 1 .h file
        std::ofstream out1(fs::path(dir) / "main.cpp");
        out1 << "int main() {}\n";
        out1.close();

        std::ofstream out2(fs::path(dir) / "util.cpp");
        out2 << "void util() {}\n";
        out2.close();

        std::ofstream out3(fs::path(dir) / "header.h");
        out3 << "#pragma once\n";
        out3.close();

        auto tool = create_project_overview_tool();
        json args;
        args["directory"] = dir;
        std::string result = tool->execute(args.dump());
        UNIT_TEST("metrics_has_cpp", result.find(".cpp") != std::string::npos);
        UNIT_TEST("metrics_has_h", result.find(".h") != std::string::npos);

        safe_remove_all(dir);
    }

    // code metrics: no source files
    {
        LOG_INFO("overview", "no_source_files");
        std::string dir = "test_ov_nosrc_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        auto tool = create_project_overview_tool();
        json args;
        args["directory"] = dir;
        std::string result = tool->execute(args.dump());
        UNIT_TEST("no_source_files", result.find("No source files found") != std::string::npos);

        safe_remove_all(dir);
    }

    // directory summary: lists subdirectories
    {
        LOG_INFO("overview", "directory_summary");
        std::string dir = "test_ov_dirsum_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(fs::path(dir) / "src");

        std::ofstream out(fs::path(dir) / "src" / "main.cpp");
        out << "int main() {}\n";
        out.close();

        auto tool = create_project_overview_tool();
        json args;
        args["directory"] = dir;
        std::string result = tool->execute(args.dump());
        UNIT_TEST("dirsum_has_src", result.find("src") != std::string::npos);

        safe_remove_all(dir);
    }

    // VCS status section exists
    {
        LOG_INFO("overview", "vcs_status_section");
        std::string dir = "test_ov_vcs_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        auto tool = create_project_overview_tool();
        json args;
        args["directory"] = dir;
        std::string result = tool->execute(args.dump());
        UNIT_TEST("vcs_status_section", result.find("CURRENT STATUS") != std::string::npos);

        safe_remove_all(dir);
    }

    // default directory when not specified
    {
        LOG_INFO("overview", "default_directory");
        auto tool = create_project_overview_tool();
        json args;
        std::string result = tool->execute(args.dump());
        UNIT_TEST("default_directory", result.find("PROJECT OVERVIEW") != std::string::npos);
    }

    // invalid JSON returns error
    {
        LOG_INFO("overview", "invalid_json_returns_error");
        auto tool = create_project_overview_tool();
        std::string result = tool->execute("not json");
        UNIT_TEST("invalid_json_returns_error", result.find("Error") != std::string::npos);
    }

    parent.report.push_back(unit);
}
