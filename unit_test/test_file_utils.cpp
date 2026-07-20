#include "pch.h"
#include "unit_test.h"
#include "file_utils.h"
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
// read_file_lines / write_file_lines tests
// ============================================================

static void test_read_write_file_lines(UnitReport& parent)
{
    UnitReport unit("read_write_file_lines");
    LOG_INFO("file_utils", "read_write_file_lines");

    // --- Test 1: Read file with trailing newline ---
    {
        LOG_INFO("file_utils", "read_with_trailing_newline");
        std::string dir = "test_fu_rwfl_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.txt", std::ios::binary);
            f << "line1\nline2\nline3\n";
        }

        // split_lines: "line1\nline2\nline3\n" → ["line1", "line2", "line3", ""]
        std::vector<std::string> lines;
        bool ok = read_file_lines((fs::path(dir) / "test.txt").string(), lines);
        UNIT_TEST("read_success", ok);
        UNIT_TEST("four_elements", lines.size() == 4u);
        UNIT_TEST("line1_content", lines[0] == "line1");
        UNIT_TEST("line2_content", lines[1] == "line2");
        UNIT_TEST("line3_content", lines[2] == "line3");
        UNIT_TEST("trailing_empty", lines[3].empty());

        safe_remove_all(dir);
    }

    // --- Test 2: Read file without trailing newline ---
    {
        LOG_INFO("file_utils", "read_without_trailing_newline");
        std::string dir = "test_fu_rwfl2_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.txt", std::ios::binary);
            f << "line1\nline2\nline3";  // no trailing newline
        }

        // split_lines: "line1\nline2\nline3" → ["line1", "line2", "line3"]
        std::vector<std::string> lines;
        bool ok = read_file_lines((fs::path(dir) / "test.txt").string(), lines);
        UNIT_TEST("read_success", ok);
        UNIT_TEST("three_elements", lines.size() == 3u);

        safe_remove_all(dir);
    }

    // --- Test 3: Read empty file ---
    {
        LOG_INFO("file_utils", "read_empty_file");
        std::string dir = "test_fu_rwfl3_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "empty.txt", std::ios::binary);
        }

        // split_lines: "" → [""]
        std::vector<std::string> lines;
        bool ok = read_file_lines((fs::path(dir) / "empty.txt").string(), lines);
        UNIT_TEST("read_success", ok);
        UNIT_TEST("one_empty_element", lines.size() == 1u && lines[0].empty());

        safe_remove_all(dir);
    }

    // --- Test 4: Read non-existent file ---
    {
        LOG_INFO("file_utils", "read_nonexistent_file");
        std::string dir = "test_fu_rwfl4_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        std::vector<std::string> lines;
        bool ok = read_file_lines((fs::path(dir) / "nonexistent.txt").string(), lines);
        UNIT_TEST("read_failed", !ok);

        safe_remove_all(dir);
    }

    // --- Test 5: Write file with trailing newline (encoded as extra empty element) ---
    {
        LOG_INFO("file_utils", "write_with_trailing_newline");
        std::string dir = "test_fu_rwfl5_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        // ["hello", "world", ""] → join with \n → "hello\nworld\n"
        std::vector<std::string> lines = {"hello", "world", ""};
        bool ok = write_file_lines((fs::path(dir) / "out.txt").string(), lines);
        UNIT_TEST("write_success", ok);

        {
            std::ifstream f(fs::path(dir) / "out.txt");
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            UNIT_TEST("content_correct", content == "hello\nworld\n");
        }

        safe_remove_all(dir);
    }

    // --- Test 6: Write file without trailing newline ---
    {
        LOG_INFO("file_utils", "write_without_trailing_newline");
        std::string dir = "test_fu_rwfl6_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        // ["hello", "world"] → join with \n → "hello\nworld"
        std::vector<std::string> lines = {"hello", "world"};
        bool ok = write_file_lines((fs::path(dir) / "out.txt").string(), lines);
        UNIT_TEST("write_success", ok);

        {
            std::ifstream f(fs::path(dir) / "out.txt");
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            UNIT_TEST("content_correct", content == "hello\nworld");
        }

        safe_remove_all(dir);
    }

    // --- Test 7: Write empty lines vector ---
    {
        LOG_INFO("file_utils", "write_empty_lines");
        std::string dir = "test_fu_rwfl7_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        // [] → nothing to write → empty file
        std::vector<std::string> lines;
        bool ok = write_file_lines((fs::path(dir) / "out.txt").string(), lines);
        UNIT_TEST("write_success", ok);

        {
            std::ifstream f(fs::path(dir) / "out.txt");
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            UNIT_TEST("content_empty", content.empty());
        }

        safe_remove_all(dir);
    }

    // --- Test 8: Write single line without trailing newline ---
    {
        LOG_INFO("file_utils", "write_single_line_no_newline");
        std::string dir = "test_fu_rwfl8_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        // ["only_one"] → "only_one"
        std::vector<std::string> lines = {"only_one"};
        bool ok = write_file_lines((fs::path(dir) / "out.txt").string(), lines);
        UNIT_TEST("write_success", ok);

        {
            std::ifstream f(fs::path(dir) / "out.txt");
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            UNIT_TEST("content_correct", content == "only_one");
        }

        safe_remove_all(dir);
    }

    // --- Test 9: Write single line with trailing newline (encoded as extra empty element) ---
    {
        LOG_INFO("file_utils", "write_single_line_with_newline");
        std::string dir = "test_fu_rwfl9_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        // ["only_one", ""] → "only_one\n"
        std::vector<std::string> lines = {"only_one", ""};
        bool ok = write_file_lines((fs::path(dir) / "out.txt").string(), lines);
        UNIT_TEST("write_success", ok);

        {
            std::ifstream f(fs::path(dir) / "out.txt");
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            UNIT_TEST("content_correct", content == "only_one\n");
        }

        safe_remove_all(dir);
    }

    // --- Test 10: Round-trip read/write with trailing newline ---
    {
        LOG_INFO("file_utils", "roundtrip_with_trailing_newline");
        std::string dir = "test_fu_rwfl10_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        // Write: ["a", "b", "c", ""] → "a\nb\nc\n"
        std::vector<std::string> orig_lines = {"a", "b", "c", ""};
        write_file_lines((fs::path(dir) / "rt.txt").string(), orig_lines);

        // Read back: should get same lines
        std::vector<std::string> read_lines;
        read_file_lines((fs::path(dir) / "rt.txt").string(), read_lines);

        // Write again — round-trip preserves content exactly
        write_file_lines((fs::path(dir) / "rt2.txt").string(), read_lines);

        {
            std::ifstream f(fs::path(dir) / "rt2.txt");
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            UNIT_TEST("roundtrip_correct", content == "a\nb\nc\n");
        }

        safe_remove_all(dir);
    }

    // --- Test 11: Round-trip read/write without trailing newline ---
    {
        LOG_INFO("file_utils", "roundtrip_without_trailing_newline");
        std::string dir = "test_fu_rwfl11_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        // Write: ["a", "b", "c"] → "a\nb\nc"
        std::vector<std::string> orig_lines = {"a", "b", "c"};
        write_file_lines((fs::path(dir) / "rt.txt").string(), orig_lines);

        // Read back: should get same lines
        std::vector<std::string> read_lines;
        read_file_lines((fs::path(dir) / "rt.txt").string(), read_lines);

        write_file_lines((fs::path(dir) / "rt2.txt").string(), read_lines);

        {
            std::ifstream f(fs::path(dir) / "rt2.txt");
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            UNIT_TEST("roundtrip_correct", content == "a\nb\nc");
        }

        safe_remove_all(dir);
    }

    parent.report.push_back(unit);
}

// ============================================================
// EditLines tests
// ============================================================

static void test_edit_lines(UnitReport& parent)
{
    UnitReport unit("edit_lines");
    LOG_INFO("file_utils", "edit_lines");

    // --- Test 1: read_file and write_file round-trip ---
    {
        LOG_INFO("file_utils", "editlines_read_write_roundtrip");
        std::string dir = "test_fu_el_rw_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.txt", std::ios::binary);
            f << "line1\nline2\nline3\n";
        }

        EditLines el;
        bool ok = el.read_file((fs::path(dir) / "test.txt").string());
        UNIT_TEST("read_success", ok);
        // split_lines: "line1\nline2\nline3\n" → ["line1", "line2", "line3", ""]
        UNIT_TEST("four_elements", el.lines.size() == 4u);

        // Modify and write back
        el.lines[1] = "modified";
        bool w_ok = el.write_file((fs::path(dir) / "test2.txt").string());
        UNIT_TEST("write_success", w_ok);

        {
            std::ifstream f(fs::path(dir) / "test2.txt");
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            UNIT_TEST("content_correct", content == "line1\nmodified\nline3\n");
        }

        safe_remove_all(dir);
    }

    // --- Test 2: parse with trailing newline ---
    {
        LOG_INFO("file_utils", "editlines_parse_with_newline");
        EditLines el;
        el.parse("a\nb\nc\n");
        UNIT_TEST("four_elements", el.lines.size() == 4u);
        UNIT_TEST("line1", el.lines[0] == "a");
        UNIT_TEST("line2", el.lines[1] == "b");
        UNIT_TEST("line3", el.lines[2] == "c");
        UNIT_TEST("trailing_empty", el.lines[3].empty());
    }

    // --- Test 3: parse without trailing newline ---
    {
        LOG_INFO("file_utils", "editlines_parse_without_newline");
        EditLines el;
        el.parse("a\nb\nc");
        UNIT_TEST("three_lines", el.lines.size() == 3u);
        UNIT_TEST("line1", el.lines[0] == "a");
    }

    // --- Test 4: parse empty string ---
    {
        LOG_INFO("file_utils", "editlines_parse_empty");
        EditLines el;
        el.parse("");
        UNIT_TEST("zero_lines", el.lines.empty());
    }

    // --- Test 5: to_string with multiple lines ---
    {
        LOG_INFO("file_utils", "editlines_to_string_multi");
        EditLines el;
        el.lines = {"a", "b", "c"};
        std::string s = el.to_string();
        UNIT_TEST("to_string_correct", s == "a\nb\nc");
    }

    // --- Test 6: to_string with single line ---
    {
        LOG_INFO("file_utils", "editlines_to_string_single");
        EditLines el;
        el.lines = {"only_one"};
        std::string s = el.to_string();
        UNIT_TEST("to_string_correct", s == "only_one");
    }

    // --- Test 7: to_string with empty lines ---
    {
        LOG_INFO("file_utils", "editlines_to_string_empty");
        EditLines el;
        std::string s = el.to_string();
        UNIT_TEST("to_string_correct", s.empty());
    }

    // --- Test 8: read_file non-existent file ---
    {
        LOG_INFO("file_utils", "editlines_read_nonexistent");
        EditLines el;
        bool ok = el.read_file("nonexistent_file.txt");
        UNIT_TEST("read_failed", !ok);
    }

    // --- Test 9: parse with only newline ---
    {
        LOG_INFO("file_utils", "editlines_parse_only_newline");
        EditLines el;
        el.parse("\n");
        UNIT_TEST("two_empty_lines", el.lines.size() == 2u);
        UNIT_TEST("both_empty", el.lines[0].empty() && el.lines[1].empty());
    }

    // --- Test 10: parse with multiple empty lines ---
    {
        LOG_INFO("file_utils", "editlines_parse_multiple_empty_lines");
        EditLines el;
        el.parse("\n\n\n");
        UNIT_TEST("four_empty_lines", el.lines.size() == 4u);
    }

    parent.report.push_back(unit);
}

// ============================================================
// EditFile tests
// ============================================================

static void test_edit_file(UnitReport& parent)
{
    UnitReport unit("edit_file");
    LOG_INFO("file_utils", "edit_file");

    // --- Test 1: replace_line_range ---
    {
        LOG_INFO("file_utils", "editfile_replace_line_range");
        std::string dir = "test_fu_ef_rlr_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.txt", std::ios::binary);
            f << "line1\nline2\nline3\nline4\nline5\n";
        }

        EditFile ef;
        ef.read_file((fs::path(dir) / "test.txt").string());
        ef.replace_line_range(2, 3, "replaced_a\nreplaced_b");

        std::string err;
        UNIT_TEST("validate_ok", ef.validate_blocks(err));

        EditLines out;
        ef.apply_blocks(out);
        // split_lines: ["line1","line2","line3","line4","line5",""]
        // replace 2-3 with ["replaced_a","replaced_b"]
        // → "line1\nreplaced_a\nreplaced_b\nline4\nline5\n"
        UNIT_TEST("content_correct", out.to_string() == "line1\nreplaced_a\nreplaced_b\nline4\nline5\n");

        safe_remove_all(dir);
    }

    // --- Test 2: insert_before_line ---
    {
        LOG_INFO("file_utils", "editfile_insert_before_line");
        std::string dir = "test_fu_ef_ibl_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.txt", std::ios::binary);
            f << "line1\nline2\nline3\n";
        }

        EditFile ef;
        ef.read_file((fs::path(dir) / "test.txt").string());
        ef.insert_before_line(2, "inserted1\ninserted2");

        std::string err;
        UNIT_TEST("validate_ok", ef.validate_blocks(err));

        EditLines out;
        ef.apply_blocks(out);
        // split_lines: ["line1","line2","line3",""]
        // insert_before_line(2, "inserted1","inserted2") → ["line1","inserted1","inserted2","line2","line3",""]
        UNIT_TEST("content_correct", out.to_string() == "line1\ninserted1\ninserted2\nline2\nline3\n");
    }
    {
        LOG_INFO("file_utils", "editfile_insert_after_line");
        std::string dir = "test_fu_ef_ial_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.txt", std::ios::binary);
            f << "line1\nline2\nline3\n";
        }

        EditFile ef;
        ef.read_file((fs::path(dir) / "test.txt").string());
        ef.insert_after_line(1, "inserted");

        std::string err;
        UNIT_TEST("validate_ok", ef.validate_blocks(err));

        EditLines out;
        ef.apply_blocks(out);
        // split_lines: ["line1","line2","line3",""]
        // insert_after_line(1, "inserted") → ["line1","inserted","line2","line3",""]
        UNIT_TEST("content_correct", out.to_string() == "line1\ninserted\nline2\nline3\n");
    }
    {
        LOG_INFO("file_utils", "editfile_delete_lines");
        std::string dir = "test_fu_ef_dl_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.txt", std::ios::binary);
            f << "line1\nline2\nline3\nline4\nline5\n";
        }

        EditFile ef;
        ef.read_file((fs::path(dir) / "test.txt").string());
        ef.delete_lines(2, 4);

        std::string err;
        UNIT_TEST("validate_ok", ef.validate_blocks(err));

        EditLines out;
        ef.apply_blocks(out);
        // split_lines: ["line1","line2","line3","line4","line5",""]
        // delete lines 2-4 → ["line1","line5",""] → "line1\nline5\n"
        auto res = out.to_string();
        UNIT_TEST("content_correct", res == "line1\nline5\n");

        safe_remove_all(dir);
    }

    // --- Test 5: validate_blocks - overlapping blocks ---
    {
        LOG_INFO("file_utils", "editfile_validate_overlapping");
        EditFile ef;
        ef.lines = {"a", "b", "c", "d", "e"};
        ef.replace_line_range(1, 3, "x");
        ef.replace_line_range(2, 4, "y");

        std::string err;
        UNIT_TEST("validate_failed", !ef.validate_blocks(err));
        UNIT_TEST("error_message", err.find("Overlapping") != std::string::npos);
    }

    // --- Test 6: validate_blocks - out of range ---
    {
        LOG_INFO("file_utils", "editfile_validate_out_of_range");
        EditFile ef;
        ef.lines = {"a", "b", "c"};
        ef.replace_line_range(1, 5, "x");

        std::string err;
        UNIT_TEST("validate_failed", !ef.validate_blocks(err));
    }

    // --- Test 7: validate_blocks - insert at valid position ---
    {
        LOG_INFO("file_utils", "editfile_validate_insert_valid");
        EditFile ef;
        ef.lines = {"a", "b", "c"};
        ef.insert_before_line(2, "x");

        std::string err;
        UNIT_TEST("validate_ok", ef.validate_blocks(err));
    }

    // --- Test 8: validate_blocks - insert at end (total+1) ---
    {
        LOG_INFO("file_utils", "editfile_validate_insert_at_end");
        EditFile ef;
        ef.lines = {"a", "b", "c"};
        // insert_before_line(4) means start=4, total=3, so 4 == total+1 is valid
        ef.insert_before_line(4, "x");

        std::string err;
        UNIT_TEST("validate_ok", ef.validate_blocks(err));
    }

    // --- Test 9: validate_blocks - insert out of range ---
    {
        LOG_INFO("file_utils", "editfile_validate_insert_out_of_range");
        EditFile ef;
        ef.lines = {"a", "b", "c"};
        // insert_before_line(5) means start=5, total=3, so 5 > total+1 is invalid
        ef.insert_before_line(5, "x");

        std::string err;
        UNIT_TEST("validate_failed", !ef.validate_blocks(err));
    }

    // --- Test 10: validate_blocks - insert at line 0 (invalid) ---
    {
        LOG_INFO("file_utils", "editfile_validate_insert_line_0");
        EditFile ef;
        ef.lines = {"a", "b", "c"};
        // Manually set block to start=0
        ef.blocks.push_back({0, -1, "x", true});

        std::string err;
        UNIT_TEST("validate_failed", !ef.validate_blocks(err));
    }

    // --- Test 11: find_occurrences single match ---
    {
        LOG_INFO("file_utils", "editfile_find_occurrences_single");
        std::vector<std::string> lines = {"hello world", "foo bar"};
        auto results = EditFile::find_occurrences(lines, "world");
        UNIT_TEST("one_match", results.size() == 1u);
        UNIT_TEST("line_number", results[0].first == 1);
    }

    // --- Test 12: find_occurrences multiple matches ---
    {
        LOG_INFO("file_utils", "editfile_find_occurrences_multiple");
        std::vector<std::string> lines = {"hello world", "world again", "no match"};
        auto results = EditFile::find_occurrences(lines, "world");
        UNIT_TEST("two_matches", results.size() == 2u);
    }

    // --- Test 13: find_occurrences no match ---
    {
        LOG_INFO("file_utils", "editfile_find_occurrences_none");
        std::vector<std::string> lines = {"hello world", "foo bar"};
        auto results = EditFile::find_occurrences(lines, "xyz");
        UNIT_TEST("no_match", results.empty());
    }

    // --- Test 14: find_occurrences empty text ---
    {
        LOG_INFO("file_utils", "editfile_find_occurrences_empty_text");
        std::vector<std::string> lines = {"hello world"};
        auto results = EditFile::find_occurrences(lines, "");
        UNIT_TEST("no_match", results.empty());
    }

    // --- Test 15: pos_to_line basic ---
    {
        LOG_INFO("file_utils", "editfile_pos_to_line_basic");
        std::vector<std::string> lines = {"hello", "world"};
        int line = EditFile::pos_to_line(lines, 0);
        UNIT_TEST("line1", line == 1);

        line = EditFile::pos_to_line(lines, 4);  // last char of "hello"
        UNIT_TEST("still_line1", line == 1);

        line = EditFile::pos_to_line(lines, 5);  // newline after "hello"
        UNIT_TEST("line2", line == 2);
    }

    // --- Test 16: text_range_to_lines single line ---
    {
        LOG_INFO("file_utils", "editfile_text_range_single_line");
        std::vector<std::string> lines = {"hello world", "foo bar"};
        auto range = EditFile::text_range_to_lines(lines, 0, 5);  // "hello"
        UNIT_TEST("start_line", range.first == 1);
        UNIT_TEST("end_line", range.second == 1);
    }

    // --- Test 17: text_range_to_lines multi line ---
    {
        LOG_INFO("file_utils", "editfile_text_range_multi_line");
        std::vector<std::string> lines = {"hello world", "foo bar"};
        auto range = EditFile::text_range_to_lines(lines, 6, 10);  // "world\nfoo"
        UNIT_TEST("start_line", range.first == 1);
        UNIT_TEST("end_line", range.second == 2);
    }

    // --- Test 18: apply_blocks with multiple operations (non-overlapping) ---
    {
        LOG_INFO("file_utils", "editfile_apply_multiple_ops");
        std::string dir = "test_fu_ef_amo_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.txt", std::ios::binary);
            f << "line1\nline2\nline3\nline4\nline5\n";
        }

        EditFile ef;
        ef.read_file((fs::path(dir) / "test.txt").string());
        ef.replace_line_range(1, 1, "replaced_1");
        ef.delete_lines(3, 4);

        std::string err;
        UNIT_TEST("validate_ok", ef.validate_blocks(err));

        EditLines out;
        ef.apply_blocks(out);
        // split_lines: ["line1","line2","line3","line4","line5",""]
        // replace line 1 → ["replaced_1"] + delete lines 3-4
        // → ["replaced_1","line2","line5",""] → "replaced_1\nline2\nline5\n"
        UNIT_TEST("content_correct", out.to_string() == "replaced_1\nline2\nline5\n");

        safe_remove_all(dir);
    }

    // --- Test 19: ModifiedBlock::is_overlay ---
    {
        LOG_INFO("file_utils", "editfile_is_overlay_overlapping");
        EditFile::ModifiedBlock a{1, 3, "x", false};
        EditFile::ModifiedBlock b{2, 4, "y", false};
        UNIT_TEST("overlapping_blocks", a.is_overlay(b));
    }

    // --- Test 20: ModifiedBlock::is_overlay non-overlapping ---
    {
        LOG_INFO("file_utils", "editfile_is_overlay_non_overlapping");
        EditFile::ModifiedBlock a{1, 2, "x", false};
        EditFile::ModifiedBlock b{3, 4, "y", false};
        UNIT_TEST("not_overlapping", !a.is_overlay(b));
    }

    // --- Test 21: ModifiedBlock::is_overlay two inserts (never overlap) ---
    {
        LOG_INFO("file_utils", "editfile_is_overlay_two_inserts");
        EditFile::ModifiedBlock a{2, -1, "x", true};
        EditFile::ModifiedBlock b{3, -1, "y", true};
        UNIT_TEST("inserts_not_overlapping", !a.is_overlay(b));
    }

    // --- Test 22: ModifiedBlock::is_overlay insert within replace range ---
    {
        LOG_INFO("file_utils", "editfile_is_overlay_insert_in_replace");
        EditFile::ModifiedBlock a{1, 3, "x", false};
        EditFile::ModifiedBlock b{2, -1, "y", true};
        UNIT_TEST("insert_in_range_overlaps", a.is_overlay(b));
    }

    // --- Test 23: apply_blocks preserves trailing newline ---
    {
        LOG_INFO("file_utils", "editfile_apply_preserves_trailing_newline");
        std::string dir = "test_fu_ef_atn_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.txt", std::ios::binary);
            f << "a\nb\nc\n";  // with trailing newline
        }

        EditFile ef;
        ef.read_file((fs::path(dir) / "test.txt").string());
        // split_lines: trailing \n → extra empty element
        UNIT_TEST("four_elements", ef.lines.size() == 4u);

        ef.replace_line_range(2, 2, "modified");
        EditLines out;
        ef.apply_blocks(out);
        // Trailing empty element preserved through apply_blocks
        UNIT_TEST("trailing_empty_preserved", !out.lines.empty() && out.lines.back().empty());

        safe_remove_all(dir);
    }

    parent.report.push_back(unit);
}

// ============================================================
// ReadFileLines / ReadFileLinesAsString tests
// ============================================================

static void test_read_file_lines(UnitReport& parent)
{
    UnitReport unit("read_file_lines_api");
    LOG_INFO("file_utils", "read_file_lines_api");

    // --- Test 1: ReadFileLines basic range ---
    {
        LOG_INFO("file_utils", "rfl_basic_range");
        std::string dir = "test_fu_rfl_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.txt", std::ios::binary);
            for (int i = 1; i <= 5; ++i)
                f << "line" << i << "\n";
        }

        std::vector<std::pair<int, std::string>> out;
        bool ok = ReadFileLines((fs::path(dir) / "test.txt").string(), 2, 4, out);
        UNIT_TEST("read_success", ok);
        UNIT_TEST("three_lines", out.size() == 3u);
        UNIT_TEST("line_num_1", out[0].first == 2);
        UNIT_TEST("line_content_1", out[0].second == "line2");
        UNIT_TEST("line_num_2", out[1].first == 3);
        UNIT_TEST("line_content_2", out[1].second == "line3");

        safe_remove_all(dir);
    }

    // --- Test 2: ReadFileLines single line ---
    {
        LOG_INFO("file_utils", "rfl_single_line");
        std::string dir = "test_fu_rfl2_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.txt", std::ios::binary);
            f << "a\nb\nc\n";
        }

        std::vector<std::pair<int, std::string>> out;
        bool ok = ReadFileLines((fs::path(dir) / "test.txt").string(), 2, 2, out);
        UNIT_TEST("read_success", ok);
        UNIT_TEST("one_line", out.size() == 1u);
        UNIT_TEST("line_num", out[0].first == 2);
        UNIT_TEST("line_content", out[0].second == "b");

        safe_remove_all(dir);
    }

    // --- Test 3: ReadFileLines invalid range (start > end) ---
    {
        LOG_INFO("file_utils", "rfl_invalid_range");
        std::string dir = "test_fu_rfl3_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.txt", std::ios::binary);
            f << "a\nb\nc\n";
        }

        std::vector<std::pair<int, std::string>> out;
        bool ok = ReadFileLines((fs::path(dir) / "test.txt").string(), 3, 1, out);
        UNIT_TEST("read_failed", !ok);

        safe_remove_all(dir);
    }

    // --- Test 4: ReadFileLines startLine <= 0 ---
    {
        LOG_INFO("file_utils", "rfl_start_line_zero");
        std::string dir = "test_fu_rfl4_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.txt", std::ios::binary);
            f << "a\nb\nc\n";
        }

        std::vector<std::pair<int, std::string>> out;
        bool ok = ReadFileLines((fs::path(dir) / "test.txt").string(), 0, 2, out);
        UNIT_TEST("read_failed", !ok);

        safe_remove_all(dir);
    }

    // --- Test 5: ReadFileLinesAsString basic ---
    {
        LOG_INFO("file_utils", "rfla_basic");
        std::string dir = "test_fu_rfla_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.txt", std::ios::binary);
            f << "hello\nworld\nfoo\nbar\nbaz\n";
        }

        std::string result = ReadFileLinesAsString((fs::path(dir) / "test.txt").string(), 2, 4);
        UNIT_TEST("not_empty", !result.empty());
        UNIT_TEST("contains_line2", result.find("hello") != std::string::npos || result.find("world") != std::string::npos);

        safe_remove_all(dir);
    }

    // --- Test 6: ReadFileLinesAsString with totalLines parameter ---
    {
        LOG_INFO("file_utils", "rfla_with_total_lines");
        std::string dir = "test_fu_rfla2_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.txt", std::ios::binary);
            f << "hello\nworld\nfoo\nbar\nbaz\n";
        }

        std::string result = ReadFileLinesAsString((fs::path(dir) / "test.txt").string(), 1, 3, 5);
        UNIT_TEST("not_empty", !result.empty());

        safe_remove_all(dir);
    }

    // --- Test 7: ReadFileLinesAsString non-existent file ---
    {
        LOG_INFO("file_utils", "rfla_nonexistent_file");
        std::string result = ReadFileLinesAsString("nonexistent.txt", 1, 5);
        UNIT_TEST("empty_result", result.empty());
    }

    // --- Test 8: ReadFileLines range beyond file length ---
    {
        LOG_INFO("file_utils", "rfl_beyond_file_length");
        std::string dir = "test_fu_rfl5_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.txt", std::ios::binary);
            f << "a\nb\nc\n";
        }

        std::vector<std::pair<int, std::string>> out;
        bool ok = ReadFileLines((fs::path(dir) / "test.txt").string(), 1, 100, out);
        UNIT_TEST("read_success", ok);
        // split_lines: "a\nb\nc\n" → ["a","b","c",""]
        UNIT_TEST("four_lines_including_trailing_empty", out.size() == 4u);

        safe_remove_all(dir);
    }

    parent.report.push_back(unit);
}

// ============================================================
// DiffEdit tests
// ============================================================

static void test_diff_edit(UnitReport& parent)
{
    UnitReport unit("diff_edit");
    LOG_INFO("file_utils", "diff_edit");

    // --- Test 1: No diff (same text) ---
    {
        LOG_INFO("file_utils", "diffedit_no_diff");
        std::string result = DiffEdit("hello\nworld\n", "hello\nworld\n", 1);
        UNIT_TEST("empty_result", result.empty());
    }

    // --- Test 2: Simple removal ---
    {
        LOG_INFO("file_utils", "diffedit_simple_removal");
        std::string result = DiffEdit("a\nb\nc\n", "a\nc\n", 1);
        UNIT_TEST("not_empty", !result.empty());
        UNIT_TEST("contains_minus_b", result.find("-b") != std::string::npos || result.find("-") != std::string::npos);
    }

    // --- Test 3: Simple addition ---
    {
        LOG_INFO("file_utils", "diffedit_simple_addition");
        std::string result = DiffEdit("a\nc\n", "a\nb\nc\n", 1);
        UNIT_TEST("not_empty", !result.empty());
        UNIT_TEST("contains_plus_b", result.find("+b") != std::string::npos || result.find("+") != std::string::npos);
    }

    // --- Test 4: Modification (remove + add) ---
    {
        LOG_INFO("file_utils", "diffedit_modification");
        std::string result = DiffEdit("a\nb\nc\n", "a\nx\nc\n", 1);
        UNIT_TEST("not_empty", !result.empty());
    }

    // --- Test 5: Empty old text ---
    {
        LOG_INFO("file_utils", "diffedit_empty_old");
        std::string result = DiffEdit("", "hello\n", 1);
        UNIT_TEST("not_empty", !result.empty());
    }

    // --- Test 6: Empty new text ---
    {
        LOG_INFO("file_utils", "diffedit_empty_new");
        std::string result = DiffEdit("hello\n", "", 1);
        UNIT_TEST("not_empty", !result.empty());
    }

    // --- Test 7: Both empty ---
    {
        LOG_INFO("file_utils", "diffedit_both_empty");
        std::string result = DiffEdit("", "", 1);
        UNIT_TEST("empty_result", result.empty());
    }

    // --- Test 8: start_line <= 0 (no line numbers) ---
    {
        LOG_INFO("file_utils", "diffedit_no_line_numbers");
        std::string result = DiffEdit("a\nb\nc\n", "a\nx\nc\n", 0);
        UNIT_TEST("not_empty", !result.empty());
    }

    // --- Test 9: Large diff with context compression (--- separator) ---
    {
        LOG_INFO("file_utils", "diffedit_context_compression");
        std::string old_text, new_text;
        for (int i = 0; i < 10; ++i) {
            old_text += "line" + std::to_string(i) + "\n";
            new_text += "line" + std::to_string(i) + "\n";
        }
        // Change line 2 and line 9
        old_text = "a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\nm\nn\no\np\nq\nr\ns\nt\nu\nv\nw\nx\ny\nz\n";
        new_text  = "a\nB\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\nm\nn\no\np\nQ\nr\ns\nt\nu\nv\nw\nx\ny\nz\n";

        std::string result = DiffEdit(old_text, new_text, 1);
        UNIT_TEST("not_empty", !result.empty());
        // Two distant changes should produce a "---" separator
        UNIT_TEST("has_separator", result.find("---") != std::string::npos);
    }

    parent.report.push_back(unit);
}

// ============================================================
// GenerateFileOutline tests
// ============================================================

static void test_generate_file_outline(UnitReport& parent)
{
    UnitReport unit("generate_file_outline");
    LOG_INFO("file_utils", "generate_file_outline");

    // --- Test 1: C++ file outline (exact output) ---
    {
        LOG_INFO("file_utils", "outline_cpp_basic");
        std::string dir = "test_fu_go_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        std::string src= R"(namespace agent {
class MyClass {
public:
    void foo();
};
void bar() {}
};)";
        {
            std::ofstream f(fs::path(dir) / "test.cpp", std::ios::binary);
            f << src;
        }

        std::string result = GenerateFileOutline((fs::path(dir) / "test.cpp").string());

        // Expected outline (7 lines)
        // # File outline for test_fu_go_temp/test.cpp (7)
        // 1 namespace agent
        // 2   class MyClass
        // 6 bar()
        std::string expected = "# File outline for " + (fs::path(dir) / "test.cpp").string() + " (7)\n";
        expected += "1 namespace agent\n"
                    "2   ├ class MyClass\n"
                    "4   │ └ foo()\n"
                    "6   └ bar()\n";

        std::cout << "\n" << src << "\n";
        std::cout << "\n" << result << "\n";
        UNIT_TEST("cpp_basic_exact_output", result == expected);

        safe_remove_all(dir);
    }

    // --- Test 2: Nested class/struct/function outline (exact output) ---
    {
        LOG_INFO("file_utils", "outline_cpp_nested");
        std::string dir = "test_fu_go_nested_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        std::string src= R"(class Outer1 {
  struct Inner1 {
    void func1() {}
  };
};
class Outer2 {
  struct Inner2 {
    void func2() {}
  };
  void func3() {}
};
void func4() {})";
        {
            std::ofstream f(fs::path(dir) / "nested.cpp", std::ios::binary);
            f << src;
        }

        std::string result = GenerateFileOutline((fs::path(dir) / "nested.cpp").string());

        // Expected outline (12 lines, width=2 so single-digit line numbers are right-aligned)
        // Parser counts '}' before symbol detection, so inline {} functions get lower depth
        // # File outline for test_fu_go_nested_temp/nested.cpp (12)
        //  1 class Outer1
        //  2   struct Inner1
        //  3   func1()
        //  6 class Outer2
        //  7   struct Inner2
        //  8   func2()
        // 10 func3()
        // 12 func4()
        std::string expected = "# File outline for " + (fs::path(dir) / "nested.cpp").string() + " (12)\n";
        expected += " 1 class Outer1\n";
        expected += " 2 │ └ struct Inner1\n";
        expected += " 3 │   └ func1()\n";
        expected += " 6 class Outer2\n";
        expected += " 7 │ ├ struct Inner2\n";
        expected += " 8 │ │ └ func2()\n";
        expected += "10 │ └ func3()\n";
        expected += "12 func4()\n";

        std::cout << "\n" << src << "\n";
        std::cout << "\n" << result << "\n";

        UNIT_TEST("cpp_nested_exact_output", result == expected);

        safe_remove_all(dir);
    }

    // --- Test 2: Python file outline ---
    {
        LOG_INFO("file_utils", "outline_python_basic");
        std::string dir = "test_fu_go_py_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.py", std::ios::binary);
            f << R"(def hello():
    pass

class MyClass:
    def method(self):
        pass
)";
        }

        std::string result = GenerateFileOutline((fs::path(dir) / "test.py").string());
        UNIT_TEST("not_empty", !result.empty());
        UNIT_TEST("has_function", result.find("hello") != std::string::npos);
        UNIT_TEST("has_class", result.find("MyClass") != std::string::npos);

        safe_remove_all(dir);
    }

    // --- Test 3: JavaScript file outline ---
    {
        LOG_INFO("file_utils", "outline_js_basic");
        std::string dir = "test_fu_go_js_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.js", std::ios::binary);
            f << R"(function hello() {}
class MyClass {}
)";
        }

        std::string result = GenerateFileOutline((fs::path(dir) / "test.js").string());
        UNIT_TEST("not_empty", !result.empty());
        UNIT_TEST("has_function", result.find("hello") != std::string::npos);
        UNIT_TEST("has_class", result.find("MyClass") != std::string::npos);

        safe_remove_all(dir);
    }

    // --- Test 4: Go file outline ---
    {
        LOG_INFO("file_utils", "outline_go_basic");
        std::string dir = "test_fu_go_golang_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.go", std::ios::binary);
            f << R"(package main
func hello() {}
type MyStruct struct {}
)";
        }

        std::string result = GenerateFileOutline((fs::path(dir) / "test.go").string());
        UNIT_TEST("not_empty", !result.empty());
        UNIT_TEST("has_package", result.find("package") != std::string::npos);
        UNIT_TEST("has_function", result.find("hello") != std::string::npos);

        safe_remove_all(dir);
    }

    // --- Test 5: Rust file outline ---
    {
        LOG_INFO("file_utils", "outline_rust_basic");
        std::string dir = "test_fu_go_rs_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.rs", std::ios::binary);
            f << R"(mod my_mod {}
fn hello() {}
struct MyStruct {}
enum MyEnum { A, B }
trait MyTrait {}
impl MyTrait for MyStruct {}
)";
        }

        std::string result = GenerateFileOutline((fs::path(dir) / "test.rs").string());
        UNIT_TEST("not_empty", !result.empty());
        UNIT_TEST("has_mod", result.find("mod") != std::string::npos);
        UNIT_TEST("has_function", result.find("hello") != std::string::npos);

        safe_remove_all(dir);
    }

    // --- Test 6: Java file outline ---
    {
        LOG_INFO("file_utils", "outline_java_basic");
        std::string dir = "test_fu_go_java_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "Test.java", std::ios::binary);
            f << R"(package com.example;
class MyClass {
    public void hello() {}
}
interface MyInterface {}
enum MyEnum { A, B }
)";
        }

        std::string result = GenerateFileOutline((fs::path(dir) / "Test.java").string());
        UNIT_TEST("not_empty", !result.empty());
        UNIT_TEST("has_package", result.find("package") != std::string::npos);
        UNIT_TEST("has_class", result.find("class") != std::string::npos || result.find("MyClass") != std::string::npos);

        safe_remove_all(dir);
    }

    // --- Test 7: Markdown file outline ---
    {
        LOG_INFO("file_utils", "outline_markdown_basic");
        std::string dir = "test_fu_go_md_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.md", std::ios::binary);
            f << R"(# Title
## Section 1
### Subsection
## Section 2
)";
        }

        std::string result = GenerateFileOutline((fs::path(dir) / "test.md").string());
        UNIT_TEST("not_empty", !result.empty());
        UNIT_TEST("has_title", result.find("Title") != std::string::npos);
        UNIT_TEST("has_heading", result.find("heading") != std::string::npos);

        safe_remove_all(dir);
    }

    // --- Test 8: Non-existent file ---
    {
        LOG_INFO("file_utils", "outline_nonexistent_file");
        std::string result = GenerateFileOutline("nonexistent.cpp");
        UNIT_TEST("empty_result", result.empty());
    }

    // --- Test 9: Empty file ---
    {
        LOG_INFO("file_utils", "outline_empty_file");
        std::string dir = "test_fu_go_ef_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "empty.cpp", std::ios::binary);
        }

        std::string result = GenerateFileOutline((fs::path(dir) / "empty.cpp").string());
        UNIT_TEST("not_empty_header", !result.empty());
        UNIT_TEST("has_zero_lines", result.find("(0)") != std::string::npos);

        safe_remove_all(dir);
    }

    // --- Test 10: File with no recognizable symbols ---
    {
        LOG_INFO("file_utils", "outline_no_symbols");
        std::string dir = "test_fu_go_ns_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "plain.txt", std::ios::binary);
            f << "just some plain text\nno symbols here\n";
        }

        std::string result = GenerateFileOutline((fs::path(dir) / "plain.txt").string());
        UNIT_TEST("has_header", !result.empty());

        safe_remove_all(dir);
    }

    // --- Test 11: C++ outline with range (startLine, endLine) ---
    {
        LOG_INFO("file_utils", "outline_cpp_with_range");
        std::string dir = "test_fu_go_cr_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.cpp", std::ios::binary);
            f << R"(void foo() {}
void bar() {}
void baz() {}
)";
        }

        // Only show symbols starting from line 2
        std::string result = GenerateFileOutline((fs::path(dir) / "test.cpp").string(), 2, -1);
        UNIT_TEST("not_empty", !result.empty());
        UNIT_TEST("no_foo", result.find("foo") == std::string::npos);

        safe_remove_all(dir);
    }

    // --- Test 12: C++ outline range exceeds file length ---
    {
        LOG_INFO("file_utils", "outline_cpp_range_exceeds");
        std::string dir = "test_fu_go_re_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        {
            std::ofstream f(fs::path(dir) / "test.cpp", std::ios::binary);
            f << R"(void foo() {}
)";
        }

        std::string result = GenerateFileOutline((fs::path(dir) / "test.cpp").string(), 100, -1);
        UNIT_TEST("has_error", result.find("Error") != std::string::npos || result.empty());

        safe_remove_all(dir);
    }

    parent.report.push_back(unit);
}

// ============================================================
// match_glob tests
// ============================================================

static void test_match_glob(UnitReport& parent)
{
    UnitReport unit("match_glob");
    LOG_INFO("file_utils", "match_glob");

    // --- Test 1: Wildcard at start (*.cpp) ---
    {
        LOG_INFO("file_utils", "glob_wildcard_start");
        UNIT_TEST("matches_cpp", match_glob("main.cpp", "*.cpp"));
        UNIT_TEST("no_match_h", !match_glob("main.h", "*.cpp"));
        UNIT_TEST("nested_path", match_glob("src/main.cpp", "*.cpp"));
    }

    // --- Test 2: Wildcard at end (file.*) ---
    {
        LOG_INFO("file_utils", "glob_wildcard_end");
        UNIT_TEST("matches_any_ext", match_glob("file.txt", "file.*"));
        UNIT_TEST("no_match_wrong_name", !match_glob("other.txt", "file.*"));
    }

    // --- Test 3: No wildcard (exact match) ---
    {
        LOG_INFO("file_utils", "glob_exact_match");
        UNIT_TEST("exact_match", match_glob("main.cpp", "main.cpp"));
        UNIT_TEST("no_match", !match_glob("main.h", "main.cpp"));
    }

    // --- Test 4: Empty pattern (matches everything) ---
    {
        LOG_INFO("file_utils", "glob_empty_pattern");
        UNIT_TEST("empty_matches_anything", match_glob("anything.txt", ""));
    }

    // --- Test 5: Wildcard in middle (*.txt.bak) ---
    {
        LOG_INFO("file_utils", "glob_wildcard_middle");
        UNIT_TEST("matches_bak", match_glob("notes.txt.bak", "*.txt.bak"));
        UNIT_TEST("no_match_plain_txt", !match_glob("notes.txt", "*.txt.bak"));
    }

    // --- Test 6: Wildcard at start with no suffix (*.ext) edge case ---
    {
        LOG_INFO("file_utils", "glob_wildcard_start_no_suffix");
        UNIT_TEST("matches_anything_star", match_glob("anything.txt", "*"));
    }

    parent.report.push_back(unit);
}

// ============================================================
// Base64Decode tests
// ============================================================

static void test_base64_decode(UnitReport& parent)
{
    UnitReport unit("base64_decode");
    LOG_INFO("file_utils", "base64_decode");

    // --- Test 1: Decode simple string "Hello" (SGVsbG8=) ---
    {
        LOG_INFO("file_utils", "b64_hello");
        std::string result = Base64Decode("SGVsbG8=");
        UNIT_TEST("hello_decoded", result == "Hello");
    }

    // --- Test 2: Decode longer string "Hello, World!" (SGVsbG8sIFdvcmxkIQ==) ---
    {
        LOG_INFO("file_utils", "b64_hello_world");
        std::string result = Base64Decode("SGVsbG8sIFdvcmxkIQ==");
        UNIT_TEST("hello_world_decoded", result == "Hello, World!");
    }

    // --- Test 3: Decode empty string ---
    {
        LOG_INFO("file_utils", "b64_empty");
        std::string result = Base64Decode("");
        UNIT_TEST("empty_decoded", result.empty());
    }

    // --- Test 4: Decode with padding (1 byte remainder) ---
    {
        LOG_INFO("file_utils", "b64_one_byte_padding");
        std::string result = Base64Decode("QQ==");  // "A"
        UNIT_TEST("single_a_decoded", result == "A");
    }

    // --- Test 5: Decode with padding (2 bytes remainder) ---
    {
        LOG_INFO("file_utils", "b64_two_bytes_padding");
        std::string result = Base64Decode("QUI=");  // "AB"
        UNIT_TEST("ab_decoded", result == "AB");
    }

    // --- Test 6: Decode without padding (3 bytes, no pad) ---
    {
        LOG_INFO("file_utils", "b64_no_padding");
        std::string result = Base64Decode("QUJD");  // "ABC"
        UNIT_TEST("abc_decoded", result == "ABC");
    }

    // --- Test 7: Decode with whitespace (should skip) ---
    {
        LOG_INFO("file_utils", "b64_with_whitespace");
        std::string result = Base64Decode("SGVs bG8=");  // "Hello" with space
        UNIT_TEST("hello_with_space_decoded", result == "Hello");
    }

    // --- Test 8: Decode binary data (non-ASCII) ---
    {
        LOG_INFO("file_utils", "b64_binary_data");
        std::string result = Base64Decode("/w==");  // byte 0xFF
        UNIT_TEST("binary_decoded_length", result.size() == 1u);
        UNIT_TEST("binary_decoded_value", static_cast<unsigned char>(result[0]) == 0xFF);
    }

    parent.report.push_back(unit);
}

// ============================================================
// Entry point for file_utils tests
// ============================================================

void test_file_utils(UnitReport& parent)
{
    auto& sg = SafetyGuard::get_instance();
    sg.set_path_whitelist({fs::current_path().string()});

    UnitReport unit("file_utils");
    LOG_INFO("file_utils", "entry");

    test_read_write_file_lines(unit);
    test_edit_lines(unit);
    test_edit_file(unit);
    test_read_file_lines(unit);
    test_diff_edit(unit);
    test_generate_file_outline(unit);
    test_match_glob(unit);
    test_base64_decode(unit);

    parent.report.push_back(unit);
}
