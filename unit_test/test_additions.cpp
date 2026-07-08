// Additions for test_delete_files_tools (after empty_input_returns_error block)

    // empty paths array returns no files found
    {
        LOG_INFO("delete_files", "empty_paths_no_files");
        auto tool = create_delete_files_tool();
        std::string result = tool->execute("{\"paths\":[]}");
        UNIT_TEST("empty_paths_no_files", result.find("No files found") != std::string::npos);
    }

    // directory+glob with no matches returns no files found
    {
        LOG_INFO("delete_files", "test_delete_glob_nomatch_temp");
        std::string dir = "test_delete_glob_nomatch_temp";
        if (fs::exists(dir)) fs::remove_all(dir);
        fs::create_directories(dir);

        std::ofstream out(fs::path(dir) / "a.txt");
        out << "txt\n";
        out.close();

        auto tool = create_delete_files_tool();
        std::string result = tool->execute(
            "{\"directory\":\"" + dir + "\",\"glob\":\"*.log\"}");
        UNIT_TEST("no_match_no_files", result.find("No files found") != std::string::npos);

        safe_remove_all(dir);
    }
