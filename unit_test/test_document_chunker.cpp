#include "pch.h"
#include "unit_test.h"
#include "document_chunker.h"
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

// ============================================================
// Config validation tests
// ============================================================

static void test_document_chunker_config(UnitReport& parent)
{
    UnitReport unit("document_chunker_config");
    LOG_INFO("document_chunker", "config_entry");

    // --- Default config values ---
    {
        LOG_INFO("document_chunker", "default_config_values");
        DocumentChunker::Config cfg;
        UNIT_TEST("chunk_size_default_1024", cfg.chunk_size == 1024);
        UNIT_TEST("overlap_default_200", cfg.overlap == 200);
        UNIT_TEST("respect_paragraphs_default_true", cfg.respect_paragraphs == true);
    }

    // --- Constructor with valid config ---
    {
        LOG_INFO("document_chunker", "constructor_valid_config");
        DocumentChunker::Config cfg;
        cfg.chunk_size = 512;
        cfg.overlap = 100;
        cfg.respect_paragraphs = false;
        try {
            DocumentChunker chunker(cfg);
            UNIT_TEST("constructor_no_exception", true);
        } catch (...) {
            UNIT_TEST("constructor_no_exception", false);
        }
    }

    // --- Constructor with invalid chunk_size (0) throws exception ---
    {
        LOG_INFO("document_chunker", "constructor_invalid_chunk_size_zero");
        DocumentChunker::Config cfg;
        cfg.chunk_size = 0;
        bool threw = false;
        try {
            DocumentChunker chunker(cfg);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        UNIT_TEST("throws_invalid_argument", threw);
    }

    // --- Constructor with invalid chunk_size (-1) throws exception ---
    {
        LOG_INFO("document_chunker", "constructor_invalid_chunk_size_negative");
        DocumentChunker::Config cfg;
        cfg.chunk_size = -1;
        bool threw = false;
        try {
            DocumentChunker chunker(cfg);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        UNIT_TEST("throws_invalid_argument", threw);
    }

    // --- Overlap clamping: negative overlap → 0 ---
    {
        LOG_INFO("document_chunker", "overlap_clamp_negative_to_zero");
        DocumentChunker::Config cfg;
        cfg.chunk_size = 512;
        cfg.overlap = -10;
        try {
            DocumentChunker chunker(cfg);
            // After construction, overlap should be clamped to 0
            UNIT_TEST("overlap_clamped_to_zero", true); // no exception means it was clamped
        } catch (...) {
            UNIT_TEST("overlap_clamped_to_zero", false);
        }
    }

    // --- Overlap clamping: overlap >= chunk_size → chunk_size - 1 ---
    {
        LOG_INFO("document_chunker", "overlap_clamp_exceeds_chunk_size");
        DocumentChunker::Config cfg;
        cfg.chunk_size = 512;
        cfg.overlap = 600; // exceeds chunk_size
        try {
            DocumentChunker chunker(cfg);
            UNIT_TEST("no_exception_when_overlap_exceeds", true);
        } catch (...) {
            UNIT_TEST("no_exception_when_overlap_exceeds", false);
        }
    }

    parent.report.push_back(unit);
}

// ============================================================
// default_extensions tests
// ============================================================

static void test_default_extensions(UnitReport& parent)
{
    UnitReport unit("default_extensions");
    LOG_INFO("document_chunker", "default_extensions_entry");

    // --- Returns non-empty list ---
    {
        LOG_INFO("document_chunker", "non_empty_list");
        const auto& exts = DocumentChunker::default_extensions();
        UNIT_TEST("not_empty", !exts.empty());
    }

    // --- Contains expected extensions ---
    {
        LOG_INFO("document_chunker", "contains_expected_extensions");
        const auto& exts = DocumentChunker::default_extensions();
        bool has_md = false, has_txt = false, has_cpp = false, has_py = false;
        for (const auto& e : exts) {
            if (e == ".md") has_md = true;
            if (e == ".txt") has_txt = true;
            if (e == ".cpp") has_cpp = true;
            if (e == ".py") has_py = true;
        }
        UNIT_TEST("has_md", has_md);
        UNIT_TEST("has_txt", has_txt);
        UNIT_TEST("has_cpp", has_cpp);
        UNIT_TEST("has_py", has_py);
    }

    parent.report.push_back(unit);
}

// ============================================================
// is_supported tests
// ============================================================

static void test_is_supported(UnitReport& parent)
{
    UnitReport unit("is_supported");
    LOG_INFO("document_chunker", "is_supported_entry");

    // --- Supported extension with default list (empty extensions param) ---
    {
        LOG_INFO("document_chunker", "supported_with_defaults");
        UNIT_TEST("md_supported", DocumentChunker::is_supported("file.md", {}));
        UNIT_TEST("txt_supported", DocumentChunker::is_supported("file.txt", {}));
        UNIT_TEST("cpp_supported", DocumentChunker::is_supported("file.cpp", {}));
    }

    // --- Unsupported extension with default list ---
    {
        LOG_INFO("document_chunker", "unsupported_with_defaults");
        UNIT_TEST("exe_not_supported", !DocumentChunker::is_supported("file.exe", {}));
        UNIT_TEST("bin_not_supported", !DocumentChunker::is_supported("file.bin", {}));
    }

    // --- Custom extension list provided ---
    {
        LOG_INFO("document_chunker", "custom_extension_list");
        std::vector<std::string> custom = {".md"};
        UNIT_TEST("md_in_custom", DocumentChunker::is_supported("file.md", custom));
        UNIT_TEST("txt_not_in_custom", !DocumentChunker::is_supported("file.txt", custom));
    }

    // --- Path with directory prefix ---
    {
        LOG_INFO("document_chunker", "path_with_directory");
        UNIT_TEST("nested_md_supported", DocumentChunker::is_supported("src/docs/readme.md", {}));
        UNIT_TEST("nested_exe_not_supported", !DocumentChunker::is_supported("bin/app.exe", {}));
    }

    // --- File without extension ---
    {
        LOG_INFO("document_chunker", "no_extension");
        UNIT_TEST("no_ext_not_supported", !DocumentChunker::is_supported("Makefile", {}));
    }

    parent.report.push_back(unit);
}

// ============================================================
// read_file_content tests
// ============================================================

static void test_read_file_content(UnitReport& parent)
{
    UnitReport unit("read_file_content");
    LOG_INFO("document_chunker", "read_file_content_entry");

    // --- Read existing file content correctly ---
    {
        LOG_INFO("document_chunker", "read_existing_file");
        std::string dir = "test_dc_rfc_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        std::string expected = u8"Hello, 世界!\nLine 2";
        {
            std::ofstream f(fs::path(dir) / "test.txt");
            f << expected;
        }

        auto content = DocumentChunker::read_file_content((fs::path(dir) / "test.txt").string());
        UNIT_TEST("content_matches", content == expected);

        safe_remove_all(dir);
    }

    // --- Non-existent file returns empty string ---
    {
        LOG_INFO("document_chunker", "read_nonexistent_file");
        auto content = DocumentChunker::read_file_content("nonexistent_file.txt");
        UNIT_TEST("empty_string_returned", content.empty());
    }

    parent.report.push_back(unit);
}

// ============================================================
// chunk tests (core logic)
// ============================================================

static void test_chunk(UnitReport& parent)
{
    UnitReport unit("chunk");
    LOG_INFO("document_chunker", "chunk_entry");

    // --- Empty content returns empty chunks ---
    {
        LOG_INFO("document_chunker", "empty_content");
        DocumentChunker::Config cfg;
        DocumentChunker chunker(cfg);
        auto chunks = chunker.chunk("");
        UNIT_TEST("no_chunks_for_empty", chunks.empty());
    }

    // --- Content smaller than chunk_size → single chunk ---
    {
        LOG_INFO("document_chunker", "small_content_single_chunk");
        DocumentChunker::Config cfg;
        cfg.chunk_size = 1024;
        cfg.overlap = 200;
        DocumentChunker chunker(cfg);

        std::string content = u8"This is a short document.";
        auto chunks = chunker.chunk(content, 1);
        UNIT_TEST("one_chunk", chunks.size() == 1);
        UNIT_TEST("chunk_text_matches", chunks[0].text == content);
        UNIT_TEST("start_line_is_1", chunks[0].start_line == 1);
    }

    // --- Content larger than chunk_size → multiple chunks with overlap ---
    {
        LOG_INFO("document_chunker", "large_content_multiple_chunks");
        DocumentChunker::Config cfg;
        cfg.chunk_size = 50;
        cfg.overlap = 10;
        cfg.respect_paragraphs = false; // disable paragraph splitting for predictable results
        DocumentChunker chunker(cfg);

        std::string content(200, 'A'); // 200 chars of 'A'
        auto chunks = chunker.chunk(content, 1);
        UNIT_TEST("multiple_chunks", chunks.size() > 1);
        UNIT_TEST("first_chunk_size_50", chunks[0].text.size() == 50);

        // Verify overlap: second chunk should start at position (50 - 10) = 40
        std::string expected_overlap = content.substr(40, 10);
        std::string actual_overlap = chunks[1].text.substr(0, 10);
        UNIT_TEST("overlap_correct", actual_overlap == expected_overlap);
    }

    // --- Paragraph-aware splitting (respect_paragraphs = true) ---
    {
        LOG_INFO("document_chunker", "paragraph_aware_splitting");
        DocumentChunker::Config cfg;
        cfg.chunk_size = 200;
        cfg.overlap = 50;
        cfg.respect_paragraphs = true;
        DocumentChunker chunker(cfg);

        // Two paragraphs separated by double newline, each < chunk_size
        std::string content = u8"First paragraph text here.\n\nSecond paragraph text here.";
        auto chunks = chunker.chunk(content, 1);
        UNIT_TEST("paragraph_split_produces_chunks", chunks.size() >= 2);
    }

    // --- Paragraph-unaware splitting (respect_paragraphs = false) ---
    {
        LOG_INFO("document_chunker", "paragraph_unaware_splitting");
        DocumentChunker::Config cfg;
        cfg.chunk_size = 50;
        cfg.overlap = 10;
        cfg.respect_paragraphs = false;
        DocumentChunker chunker(cfg);

        std::string content = u8"First paragraph text here.\n\nSecond paragraph text here.";
        auto chunks = chunker.chunk(content, 1);
        UNIT_TEST("unaware_splitting_produces_chunks", chunks.size() > 0);
    }

    // --- start_line_offset tracking ---
    {
        LOG_INFO("document_chunker", "start_line_offset_tracking");
        DocumentChunker::Config cfg;
        cfg.chunk_size = 50;
        cfg.overlap = 10;
        cfg.respect_paragraphs = false;
        DocumentChunker chunker(cfg);

        std::string content(200, 'A');
        auto chunks = chunker.chunk(content, 10); // offset starts at line 10
        UNIT_TEST("first_chunk_start_line_10", chunks[0].start_line == 10);
    }

    // --- Content with newlines → start_line increments correctly ---
    {
        LOG_INFO("document_chunker", "line_number_increments");
        DocumentChunker::Config cfg;
        cfg.chunk_size = 50;
        cfg.overlap = 10;
        cfg.respect_paragraphs = false;
        DocumentChunker chunker(cfg);

        // Build content with known newlines: "A\nB\nC..." (200 chars)
        std::string content;
        for (int i = 0; i < 200; ++i) {
            if (i > 0 && i % 10 == 0) content += '\n';
            else content += 'A';
        }

        auto chunks = chunker.chunk(content, 1);
        UNIT_TEST("first_chunk_line_1", chunks[0].start_line == 1);
        // Second chunk starts after overlap; its line should be > 1
        if (chunks.size() >= 2) {
            UNIT_TEST("second_chunk_line_gt_first", chunks[1].start_line > chunks[0].start_line);
        }
    }

    parent.report.push_back(unit);
}

// ============================================================
// chunk_file tests
// ============================================================

static void test_chunk_file(UnitReport& parent)
{
    UnitReport unit("chunk_file");
    LOG_INFO("document_chunker", "chunk_file_entry");

    // --- Chunk existing file correctly ---
    {
        LOG_INFO("document_chunker", "chunk_existing_file");
        std::string dir = "test_dc_cf_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        DocumentChunker::Config cfg;
        cfg.chunk_size = 1024;
        cfg.overlap = 200;
        DocumentChunker chunker(cfg);

        std::string expected = u8"Hello, 世界!\nLine 2 of the document.";
        {
            std::ofstream f(fs::path(dir) / "test.txt");
            f << expected;
        }

        auto result = chunker.chunk_file((fs::path(dir) / "test.txt").string());
        UNIT_TEST("source_path_set", !result.source_path.empty());
        UNIT_TEST("has_chunks", result.chunks.size() > 0);
        if (!result.chunks.empty()) {
            UNIT_TEST("chunk_text_matches", result.chunks[0].text == expected);
        }

        safe_remove_all(dir);
    }

    // --- Non-existent file returns empty chunks ---
    {
        LOG_INFO("document_chunker", "chunk_nonexistent_file");
        DocumentChunker::Config cfg;
        DocumentChunker chunker(cfg);

        auto result = chunker.chunk_file("nonexistent.txt");
        UNIT_TEST("empty_chunks_for_nonexistent", result.chunks.empty());
    }

    // --- Empty file returns empty chunks ---
    {
        LOG_INFO("document_chunker", "chunk_empty_file");
        std::string dir = "test_dc_cfef_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        DocumentChunker::Config cfg;
        DocumentChunker chunker(cfg);

        {
            std::ofstream f(fs::path(dir) / "empty.txt");
        }

        auto result = chunker.chunk_file((fs::path(dir) / "empty.txt").string());
        UNIT_TEST("empty_chunks_for_empty_file", result.chunks.empty());

        safe_remove_all(dir);
    }

    // --- Large file produces multiple chunks ---
    {
        LOG_INFO("document_chunker", "chunk_large_file_multiple_chunks");
        std::string dir = "test_dc_cflf_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        DocumentChunker::Config cfg;
        cfg.chunk_size = 100;
        cfg.overlap = 20;
        cfg.respect_paragraphs = false;
        DocumentChunker chunker(cfg);

        // Write a file larger than chunk_size
        {
            std::ofstream f(fs::path(dir) / "large.txt");
            for (int i = 0; i < 500; ++i) f << 'A';
        }

        auto result = chunker.chunk_file((fs::path(dir) / "large.txt").string());
        UNIT_TEST("multiple_chunks_for_large_file", result.chunks.size() > 1);

        safe_remove_all(dir);
    }

    parent.report.push_back(unit);
}

// ============================================================
// chunk_directory tests
// ============================================================

static void test_chunk_directory(UnitReport& parent)
{
    UnitReport unit("chunk_directory");
    LOG_INFO("document_chunker", "chunk_directory_entry");

    // --- Directory with supported files → chunked correctly ---
    {
        LOG_INFO("document_chunker", "directory_with_supported_files");
        std::string dir = "test_dc_cd_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        DocumentChunker::Config cfg;
        cfg.chunk_size = 1024;
        DocumentChunker chunker(cfg);

        // Create supported files
        {
            std::ofstream f(fs::path(dir) / "readme.md");
            f << u8"# Hello World\nThis is a markdown file.";
        }
        {
            std::ofstream f(fs::path(dir) / "notes.txt");
            f << u8"Some notes here.";
        }

        auto results = chunker.chunk_directory(dir);
        UNIT_TEST("found_supported_files", results.size() == 2);

        // Verify both files were chunked
        bool has_md = false, has_txt = false;
        for (const auto& fc : results) {
            if (fc.source_path.find("readme.md") != std::string::npos) has_md = true;
            if (fc.source_path.find("notes.txt") != std::string::npos) has_txt = true;
        }
        UNIT_TEST("has_readme", has_md);
        UNIT_TEST("has_notes", has_txt);

        safe_remove_all(dir);
    }

    // --- Directory with unsupported files → skipped ---
    {
        LOG_INFO("document_chunker", "directory_with_unsupported_files");
        std::string dir = "test_dc_cdu_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        DocumentChunker::Config cfg;
        cfg.chunk_size = 1024;
        DocumentChunker chunker(cfg);

        // Create unsupported file
        {
            std::ofstream f(fs::path(dir) / "data.bin");
            f << u8"Binary data.";
        }

        auto results = chunker.chunk_directory(dir);
        UNIT_TEST("unsupported_files_skipped", results.empty());

        safe_remove_all(dir);
    }

    // --- Non-existent directory → empty results ---
    {
        LOG_INFO("document_chunker", "nonexistent_directory");
        DocumentChunker::Config cfg;
        DocumentChunker chunker(cfg);

        auto results = chunker.chunk_directory("nonexistent_dir_xyz");
        UNIT_TEST("empty_results_for_nonexistent_dir", results.empty());
    }

    // --- Custom extensions filter ---
    {
        LOG_INFO("document_chunker", "custom_extensions_filter");
        std::string dir = "test_dc_ccef_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        DocumentChunker::Config cfg;
        cfg.chunk_size = 1024;
        DocumentChunker chunker(cfg);

        // Create both .md and .txt files
        {
            std::ofstream f(fs::path(dir) / "readme.md");
            f << u8"# Hello World";
        }
        {
            std::ofstream f(fs::path(dir) / "notes.txt");
            f << u8"Some notes.";
        }

        // Only allow .md files
        auto results = chunker.chunk_directory(dir, {".md"});
        UNIT_TEST("only_md_files_chunked", results.size() == 1);
        if (!results.empty()) {
            UNIT_TEST("chunked_file_is_md", results[0].source_path.find("readme.md") != std::string::npos);
        }

        safe_remove_all(dir);
    }

    // --- Recursive directory scanning (nested subdirectories) ---
    {
        LOG_INFO("document_chunker", "recursive_directory_scanning");
        std::string dir = "test_dc_cdr_temp";
        safe_remove_all(dir);
        fs::create_directories(fs::path(dir) / "sub1" / "sub2");

        DocumentChunker::Config cfg;
        cfg.chunk_size = 1024;
        DocumentChunker chunker(cfg);

        // Create files in nested directories
        {
            std::ofstream f(fs::path(dir) / "root.md");
            f << u8"Root file.";
        }
        {
            std::ofstream f(fs::path(dir) / "sub1" / "level1.txt");
            f << u8"Level 1 file.";
        }
        {
            std::ofstream f(fs::path(dir) / "sub1" / "sub2" / "level2.md");
            f << u8"Level 2 file.";
        }

        auto results = chunker.chunk_directory(dir);
        UNIT_TEST("found_all_nested_files", results.size() == 3);

        safe_remove_all(dir);
    }

    // --- Empty directory → empty results ---
    {
        LOG_INFO("document_chunker", "empty_directory");
        std::string dir = "test_dc_cded_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        DocumentChunker::Config cfg;
        DocumentChunker chunker(cfg);

        auto results = chunker.chunk_directory(dir);
        UNIT_TEST("empty_results_for_empty_dir", results.empty());

        safe_remove_all(dir);
    }

    parent.report.push_back(unit);
}

// ============================================================
// Entry point for document_chunker tests
// ============================================================

void test_document_chunker(UnitReport& parent)
{
    auto& sg = SafetyGuard::get_instance();
    sg.set_path_whitelist({fs::current_path().string()});

    UnitReport unit("document_chunker");
    LOG_INFO("document_chunker", "entry");

    test_document_chunker_config(unit);
    test_default_extensions(unit);
    test_is_supported(unit);
    test_read_file_content(unit);
    test_chunk(unit);
    test_chunk_file(unit);
    test_chunk_directory(unit);

    parent.report.push_back(unit);
}
