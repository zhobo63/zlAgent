#include "pch.h"
#include "file_utils.h"
#include "tui.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <unordered_set>

namespace agent {

/// Split new_text into lines for replace_line_range / insert operations.
/// Unlike EditLines::parse, this does NOT pop the trailing \n — every \n is a
/// line separator, so trailing blank lines are preserved correctly.

std::vector<std::string> split_lines(const char* text) {
    // First pass: count lines to reserve capacity
    size_t count = 1; // at least one line (the remainder after last \n)
    const char* p = text;
    while (*p) {
        if (*p == '\n') ++count;
        ++p;
    }

    std::vector<std::string> result;
    result.reserve(count);

    // Second pass: extract lines
    p = text;
    const char* s = p;  // start of current line
    while (*p) {
        if (*p == '\n') {
            size_t len = static_cast<size_t>(p - s);
            // strip trailing \r from \r\n
            if (len > 0 && p[-1] == '\r')
                --len;
            result.emplace_back(s, len);
            s = p + 1;
        }
        ++p;
    }
    // push remaining content after the last \n
    if (s <= p)
        result.emplace_back(s, static_cast<size_t>(p - s));
    return std::move(result);
}

std::vector<std::string> split_lines(const std::string& text) {
    return std::move(split_lines(text.c_str()));
}

// ── read_file_lines / write_file_lines ───────────────────

bool read_file_lines(const std::string& path,
    std::vector<std::string>& out_lines) {
    std::ifstream infile(path, std::ios::binary);
    if (!infile.is_open()) return false;
    std::ostringstream ss;
    ss << infile.rdbuf();
    std::string content = ss.str();
    infile.close();
    out_lines = split_lines(content);
    return true;
}

bool write_file_lines(const std::string& path,
    const std::vector<std::string>& lines) {
    std::ofstream outfile(path, std::ios::trunc | std::ios::binary);
    if (!outfile.is_open()) return false;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        outfile << lines[i];
        if (i < static_cast<int>(lines.size()) - 1)
            outfile << '\n';
    }
    outfile.close();
    return true;
}


bool ReadFileLines(const std::string& path, int startLine, int endLine,
                   std::vector<std::pair<int, std::string>>& out) {
    if (startLine <= 0 || endLine < startLine) return false;

    EditLines el;
    if (!el.read_file(path)) return false;

    out.clear();
    int total = static_cast<int>(el.lines.size());
    for (int i = startLine - 1; i < endLine && i < total; ++i) {
        out.emplace_back(i + 1, el.lines[i]);
    }
    return !out.empty();
}

static std::string _format_lines(const EditLines& el, int startLine, int endLine, int totalLines) {
    if (startLine <= 0 || endLine < startLine) return "";

    int width = static_cast<int>(std::to_string(totalLines).size());
    int total = static_cast<int>(el.lines.size());

    std::ostringstream oss;
    for (int i = startLine - 1; i <= endLine && i < total; ++i) {
        oss << std::setw(width) << (i + 1) << " " << el.lines[i] << "\n";
    }
    return oss.str();
}

std::string ReadFileLinesAsString(const std::string& path, int startLine, int endLine) {
    EditLines el;
    if (!el.read_file(path)) return "";
    int total = static_cast<int>(el.lines.size());
    return _format_lines(el, startLine, endLine, total);
}

std::string ReadFileLinesAsString(const std::string& path, int startLine, int endLine, int totalLines) {
    EditLines el;
    if (!el.read_file(path)) return "";
    return _format_lines(el, startLine, endLine, totalLines);
}


// ── EditLines ────────────────────────────────────────────

bool EditLines::read_file(const std::string& path) {
    lines.clear();
    return read_file_lines(path, lines);
}

void EditLines::parse(const std::string& text) {
    lines.clear();
    if (text.empty()) return;
    lines = split_lines(text);
}

bool EditLines::write_file(const std::string& path) const {
    return write_file_lines(path, lines);
}

std::string EditLines::to_string() const {
    std::ostringstream iss;
    int lcount = static_cast<int>(lines.size());
    for (int i = 0; i < lcount; ++i) {
        iss << lines[i];
        if (i < lcount - 1)
            iss << "\n";
    }
    return iss.str();
}

// ── EditFile implementation ───────────────────────────────


bool EditFile::ModifiedBlock::is_overlay(const ModifiedBlock& b) const {
    if (is_insert && b.is_insert) return false;
    // Ensure a is the non-insertion one
    const ModifiedBlock* ta = this, *tb = &b;
    if (ta->is_insert) { std::swap(ta, tb); }
    int a_start = ta->start, a_end = ta->end;
    if (!tb->is_insert)
        return !(a_end < tb->start || tb->end < a_start);
    // tb is insertion — adjacent (tb.start == a_end+1) is OK
    return tb->start >= a_start && tb->start <= a_end;
}

void EditFile::replace_line_range(int start, int end, const std::string& new_text) {
    blocks.push_back({start, end, new_text, false});
}

void EditFile::insert_before_line(int start, const std::string& new_text) {
    blocks.push_back({start, -1, new_text, true});
}

void EditFile::insert_after_line(int start, const std::string& new_text) {
    blocks.push_back({start + 1, -1, new_text, true});
}

void EditFile::delete_lines(int start, int end) {
    blocks.push_back({start, end, "", false});
}

bool EditFile::validate_blocks(std::string& error_out) const {
    int total = static_cast<int>(lines.size());

    for (const auto& block : blocks) {
        if (block.is_insert) {
            if (block.start < 1 || block.start > total + 1) {
                error_out = "Line number out of range: insert at position " +
                            std::to_string(block.start) + " (file has " +
                            std::to_string(total) + " lines).";
                return false;
            }
        } else {
            if (block.start < 1 || block.end < block.start || block.end > total) {
                error_out = "Invalid line range: start=" +
                            std::to_string(block.start) + ", end=" + std::to_string(block.end) +
                            " in file with " + std::to_string(total) + " lines.";
                return false;
            }
        }
    }

    for (size_t i = 0; i < blocks.size(); ++i)
        for (size_t j = i + 1; j < blocks.size(); ++j)
            if (blocks[i].is_overlay(blocks[j])) {
                error_out = "Overlapping line operations detected.";
                return false;
            }

    return true;
}

int EditFile::pos_to_line(const std::vector<std::string>& lines, size_t pos) {
    // Find which 1-based line the content position falls on.
    size_t offset = 0;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        if (pos < offset + lines[i].size())
            return i + 1;
        offset += lines[i].size() + 1; // +1 for newline
    }
    return static_cast<int>(lines.size());
}

std::pair<int, int> EditFile::text_range_to_lines(
        const std::vector<std::string>& lines, size_t pos, size_t len) {
    int start_line = pos_to_line(lines, pos);
    size_t last_char_pos = pos + len - 1;
    // Find the line that contains the last character of the matched text
    size_t offset = 0;
    int end_line = static_cast<int>(lines.size());
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        size_t line_end = offset + lines[i].size();
        if (last_char_pos <= line_end) {
            end_line = i + 1;
            break;
        }
        offset += lines[i].size() + 1; // +1 for newline
    }
    return {start_line, end_line};
}

std::vector<std::pair<int, size_t>> EditFile::find_occurrences(
        const std::vector<std::string>& lines,
        const std::string& old_text) {
    // Reconstruct content and build line offset table for binary search.
    // Returns (line_number_of_start, content_position) for each match.
    if (old_text.empty()) return {};

    std::string content;
    std::vector<size_t> line_offsets; // starting position of each line in content
    line_offsets.reserve(lines.size());
    size_t offset = 0;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        line_offsets.push_back(offset);
        if (i > 0) content += '\n';
        content += lines[i];
        offset += lines[i].size() + 1;
    }

    // Binary search: find the 1-based line number for a given position.
    auto pos_to_line = [&](size_t pos) -> int {
        auto it = std::upper_bound(line_offsets.begin(), line_offsets.end(), pos);
        if (it == line_offsets.begin()) return 1;
        --it; // step back to the candidate line
        size_t idx = static_cast<size_t>(it - line_offsets.begin());
        if (pos < *it + lines[idx].size())
            return static_cast<int>(idx) + 1;
        // pos is at or past this line's content; belongs to next line
        if (idx + 1 < line_offsets.size())
            return static_cast<int>(idx) + 2;
        return static_cast<int>(lines.size());
    };

    std::vector<std::pair<int, size_t>> results;
    size_t search_pos = 0;
    while (true) {
        auto found = content.find(old_text, search_pos);
        if (found == std::string::npos) break;
        int line_num = pos_to_line(found);
        results.push_back({line_num, found});
        search_pos = found + 1;
    }
    return results;
}

void EditFile::apply_blocks(EditLines& out_lines) {
    std::vector<ModifiedBlock> sorted_blocks = blocks;
    std::sort(sorted_blocks.begin(), sorted_blocks.end(),
        [](const ModifiedBlock& a, const ModifiedBlock& b) {
            if (a.start != b.start) return a.start < b.start;
            if (a.is_insert && !b.is_insert) return true;
            return false;
        });

    int total_lines = static_cast<int>(lines.size());
    std::vector<std::string> result;
    int current_line = 1;

    for (const auto& block : sorted_blocks) {
        if (block.start < 1 || block.start > total_lines + 1)
            return;
        while (current_line < block.start) {
            result.push_back(lines[current_line - 1]);
            ++current_line;
        }
        if (block.is_insert) {
            if (!block.new_content.empty()) {
                auto insert_lines = split_lines(block.new_content);
                for (const auto& il : insert_lines)
                    result.push_back(il);
            }
        } else {
            if (block.end < 1 || block.end > total_lines)
                return;
            if (!block.new_content.empty()) {
                auto replace_lines = split_lines(block.new_content);
                for (auto& rl : replace_lines) {
                    result.push_back(rl);
                }
            }
            current_line = block.end + 1;
        }
    }

    while (current_line <= total_lines) {
        result.push_back(lines[current_line - 1]);
        ++current_line;
    }

    out_lines.lines = std::move(result);
}

} // namespace agent

namespace agent {

// ── Helpers for DiffEdit ───────────────────────────────

std::string DiffEdit(const std::string& old_text,
                     const std::string& new_text,
                     int start_line) {
    auto old_lines = split_lines(old_text);
    auto new_lines = split_lines(new_text);

    // Find LCS to identify common lines (context)
    int m = static_cast<int>(old_lines.size()), n = static_cast<int>(new_lines.size());
    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));
    for (int i = 1; i <= m; ++i)
        for (int j = 1; j <= n; ++j)
            if (old_lines[i - 1] == new_lines[j - 1])
                dp[i][j] = dp[i - 1][j - 1] + 1;
            else
                dp[i][j] = std::max(dp[i - 1][j], dp[i][j - 1]);

    // Backtrack to find LCS lines
    int i = m, j = n;
    std::vector<std::string> lcs_lines;
    while (i > 0 && j > 0) {
        if (old_lines[i - 1] == new_lines[j - 1]) {
            lcs_lines.push_back(old_lines[i - 1]);
            --i; --j;
        } else if (dp[i - 1][j] >= dp[i][j - 1]) {
            --i;
        } else {
            --j;
        }
    }
    std::reverse(lcs_lines.begin(), lcs_lines.end());

    // Collect diff operations: Context, Remove, Add
    enum class DiffOp { Context, Remove, Add };
    struct DiffEntry { DiffOp op; std::string line; };
    std::vector<DiffEntry> entries;

    int oi = 0, ni = 0, li = 0;
    while (oi < m || ni < n) {
        while (oi < m &&
               (li >= static_cast<int>(lcs_lines.size()) || old_lines[oi] != lcs_lines[li])) {
            entries.push_back({DiffOp::Remove, old_lines[oi]}); ++oi;
        }
        while (ni < n &&
               (li >= static_cast<int>(lcs_lines.size()) || new_lines[ni] != lcs_lines[li])) {
            entries.push_back({DiffOp::Add, new_lines[ni]}); ++ni;
        }
        if (li < static_cast<int>(lcs_lines.size())) {
            entries.push_back({DiffOp::Context, lcs_lines[li]});
            ++oi; ++ni; ++li;
        }
    }

    // Output with context compression: skip long runs of unchanged lines
    constexpr int CONTEXT_LINES = 3;
    std::ostringstream oss;

    if (entries.empty()) return "";

    // Compute old/new line numbers for each entry
    std::vector<int> old_line(entries.size(), -1); // -1 means no old-line mapping
    std::vector<int> new_line(entries.size(), -1);
    int ol = start_line, nl = start_line;
    for (int k = 0; k < static_cast<int>(entries.size()); ++k) {
        switch (entries[k].op) {
            case DiffOp::Context:
                old_line[k] = ol++; new_line[k] = nl++; break;
            case DiffOp::Remove:
                old_line[k] = ol++; break;
            case DiffOp::Add:
                new_line[k] = nl++; break;
        }
    }

    // Find indices of change entries (Remove or Add)
    std::vector<int> change_indices;
    for (int k = 0; k < static_cast<int>(entries.size()); ++k) {
        if (entries[k].op != DiffOp::Context)
            change_indices.push_back(k);
    }

    // Group nearby changes into hunks: merge groups whose output ranges overlap
    std::vector<std::pair<int, int>> groups; // [first_change_idx, last_change_idx] in change_indices
    for (int ci = 0; ci < static_cast<int>(change_indices.size()); ++ci) {
        if (!groups.empty()) {
            auto& g = groups.back();
            int prev_end = std::min(change_indices[g.second] + CONTEXT_LINES, static_cast<int>(entries.size()));
            int curr_start = std::max(change_indices[ci] - CONTEXT_LINES, 0);
            if (curr_start < prev_end) {
                g.second = ci; // merge
                continue;
            }
        }
        groups.push_back({ci, ci});
    }

    auto fmt_line_num = [&](int ln) -> std::string {
        if (start_line <= 0 || ln < 0) return "";
        char buf[16];
        int w = static_cast<int>(std::snprintf(buf, sizeof(buf), "%d", ln));
        return std::string(buf, w);
    };

    for (int gi = 0; gi < static_cast<int>(groups.size()); ++gi) {
        auto& [gfirst, glast] = groups[gi];
        int first_change = change_indices[gfirst];
        int last_change  = change_indices[glast];

        // Output range: CONTEXT_LINES before first change to CONTEXT_LINES after last change
        int start = std::max(first_change - CONTEXT_LINES, 0);
        int end   = std::min(last_change + CONTEXT_LINES, static_cast<int>(entries.size()) - 1);

        if (gi > 0) oss << "---\n";

        for (int k = start; k <= end; ++k) {
            const auto& e = entries[k];
            switch (e.op) {
                case DiffOp::Context: {
                    std::string ln = fmt_line_num(old_line[k]);
                    if (!ln.empty()) oss << TUI::ANSI_FG_WHITE << "\033[2m";
                    oss << ln << TUI::ANSI_RESET << " " << e.line << "\n";
                    break;
                }
                case DiffOp::Remove: {
                    std::string ln = fmt_line_num(old_line[k]);
                    if (!ln.empty()) oss << TUI::ANSI_FG_WHITE << "\033[2m";
                    oss << ln << TUI::ANSI_RESET << " " << TUI::ANSI_FG_RED << "-" << e.line << TUI::ANSI_RESET << "\n";
                    break;
                }
                case DiffOp::Add: {
                    std::string ln = fmt_line_num(new_line[k]);
                    if (!ln.empty()) oss << TUI::ANSI_FG_WHITE << "\033[2m";
                    oss << ln << TUI::ANSI_RESET << " " << TUI::ANSI_FG_GREEN << "+" << e.line << TUI::ANSI_RESET << "\n";
                    break;
                }
            }
        }
    }

    return oss.str();
}

// ── EditedLines: show the edited content with line numbers ───────────────────────

std::string EditedLines(const std::string& old_text,
                        const std::string& new_text,
                        int start_line) {
    auto old_lines = split_lines(old_text);
    auto new_lines = split_lines(new_text);

    // Find LCS to identify common lines (same algorithm as DiffEdit)
    int m = static_cast<int>(old_lines.size()), n = static_cast<int>(new_lines.size());
    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));
    for (int i = 1; i <= m; ++i)
        for (int j = 1; j <= n; ++j)
            if (old_lines[i - 1] == new_lines[j - 1])
                dp[i][j] = dp[i - 1][j - 1] + 1;
            else
                dp[i][j] = std::max(dp[i - 1][j], dp[i][j - 1]);

    // Backtrack to find LCS lines
    int i = m, j = n;
    std::vector<std::string> lcs_lines;
    while (i > 0 && j > 0) {
        if (old_lines[i - 1] == new_lines[j - 1]) {
            lcs_lines.push_back(old_lines[i - 1]);
            --i; --j;
        } else if (dp[i - 1][j] >= dp[i][j - 1]) {
            --i;
        } else {
            --j;
        }
    }
    std::reverse(lcs_lines.begin(), lcs_lines.end());

    // Collect diff operations: Context, Remove, Add
    enum class DiffOp { Context, Remove, Add };
    struct DiffEntry { DiffOp op; std::string line; };
    std::vector<DiffEntry> entries;

    int oi = 0, ni = 0, li = 0;
    while (oi < m || ni < n) {
        while (oi < m &&
               (li >= static_cast<int>(lcs_lines.size()) || old_lines[oi] != lcs_lines[li])) {
            entries.push_back({DiffOp::Remove, old_lines[oi]}); ++oi;
        }
        while (ni < n &&
               (li >= static_cast<int>(lcs_lines.size()) || new_lines[ni] != lcs_lines[li])) {
            entries.push_back({DiffOp::Add, new_lines[ni]}); ++ni;
        }
        if (li < static_cast<int>(lcs_lines.size())) {
            entries.push_back({DiffOp::Context, lcs_lines[li]});
            ++oi; ++ni; ++li;
        }
    }

    // If no changes, return empty
    bool has_changes = false;
    for (const auto& e : entries) {
        if (e.op != DiffOp::Context) { has_changes = true; break; }
    }
    if (!has_changes) return "";

    // Compute new line numbers for each entry
    std::vector<int> new_line(entries.size(), -1);
    int nl = start_line;
    for (int k = 0; k < static_cast<int>(entries.size()); ++k) {
        switch (entries[k].op) {
            case DiffOp::Context:
                new_line[k] = nl++; break;
            case DiffOp::Remove:
                // removed line has no new-line mapping
                break;
            case DiffOp::Add:
                new_line[k] = nl++; break;
        }
    }

    // Find the range of entries that contain changes (first change to last change)
    int first_change = -1, last_change = -1;
    for (int k = 0; k < static_cast<int>(entries.size()); ++k) {
        if (entries[k].op != DiffOp::Context) {
            if (first_change < 0) first_change = k;
            last_change = k;
        }
    }

    // Expand the output range to include context lines before and after.
    // This is important for pure deletions: without expansion, a Remove-only
    // change would produce no output since Remove entries are skipped.
    constexpr int CONTEXT_LINES = 3;
    int out_start = std::max(first_change - CONTEXT_LINES, 0);
    int out_end   = std::min(last_change + CONTEXT_LINES, static_cast<int>(entries.size()) - 1);

    // Output only the new content lines within the changed region,
    // including context lines that fall between changes.
    std::ostringstream oss;

    for (int k = out_start; k <= out_end; ++k) {
        const auto& e = entries[k];
        if (e.op == DiffOp::Remove)
            continue;  // skip removed lines, we only show the result

        char buf[16];
        int w = static_cast<int>(std::snprintf(buf, sizeof(buf), "%d", new_line[k]));
        std::string ln(buf, w);

        switch (e.op) {
            case DiffOp::Context:
            case DiffOp::Add:
                oss << " " << ln << " " << e.line << "\n";
                break;
            default:
                break;
        }
    }

    return oss.str();
}

// ── Helpers for GenerateFileOutline ───────────────────────

static bool is_delims(char c, const char* delimiter)
{
    while (delimiter[0]) {
        if (c == delimiter[0])
            return true;
        delimiter++;
    }
    return false;
}

std::vector<std::string> split(const std::string& s, const char *delimiter) {
    std::vector<std::string> tokens;
    size_t start = 0;
    for (size_t i = 0; i <= s.length(); i++) {
        if (i == s.length() || is_delims(s[i], delimiter)) {
            if (i > start)
                tokens.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return tokens;
}

static std::string trim_outline(const std::string& s) {

    size_t start = 0, end = s.size();
    while (start < end && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r')) start++;
    while (end > start && (s[end-1] == ' ' || s[end-1] == '\t' || s[end-1] == '\r')) end--;
    int comment = s.find("//");
    if (comment != std::string::npos && comment<end) {
        end = comment;
    }
    return s.substr(start, end - start);
}

static std::string get_extension_outline(const std::string& path) {
    auto pos = path.rfind('.');
    if (pos == std::string::npos) return "";
    std::string ext = path.substr(pos);
    for (auto& c : ext) c = static_cast<char>(std::tolower(c));
    return ext;
}

static bool is_in_comment_outline(const std::string& trimmed) {
    if (trimmed[0] == '/' && trimmed.size() >= 2 && trimmed[1] == '/')
        return true;
    if (trimmed[0] == '#')
        return true;
    return false;
}
static bool is_comment_begin(const std::string& trimmed) {
    return (trimmed.find("/*") != std::string::npos);
}

static bool is_keyword_outline(const std::string& s) {
    static const std::unordered_set<std::string> keywords = {
        "if", "else", "for", "while", "do", "switch", "case",
        "return", "break", "continue", "goto", "try", "catch",
        "throw", "new", "delete", "sizeof", "typeof",
        "int", "float", "double", "char", "bool", "void",
        "long", "short", "unsigned", "signed", "const", "static",
        "virtual", "override", "final", "inline", "explicit",
        "public", "private", "protected", "friend", "typedef",
        "using", "template", "typename", "auto", "constexpr",
        "nullptr", "true", "false", "namespace", "class", "struct"
    };
    return keywords.count(s) > 0;
}

static std::string extract_identifier_outline(const std::string& s, size_t pos) {
    if (pos >= s.size()) return "";
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) pos++;
    if (pos >= s.size()) return "";
    size_t start = pos;
    while (pos < s.size() && (std::isalnum(static_cast<unsigned char>(s[pos])) || s[pos] == '_'))
        pos++;
    return trim_outline(s.substr(start, pos - start));
}

struct RawSymbol {
    int start_line;  // 1-based
    std::string kind;
    std::string name;
    int depth = 0;       // nesting level at time of discovery
    std::string annotation;  // e.g. ": SubAgent", "[virtual]"
};

// Strip access specifiers from a base-list token: "public Base" → "Base"
static std::string strip_access_specifier(const std::string& s) {
    auto t = trim_outline(s);
    if (t.empty()) return t;
    static const char* specs[] = {"public", "private", "protected"};
    for (const auto* sp : specs) {
        size_t len = strlen(sp);
        if (t.size() > len && t.substr(0, len) == sp && t[len] == ' ') {
            return trim_outline(t.substr(len));
        }
    }
    return t;
}

static std::string normalize_cpp_name(std::string& _name)
{
    std::string name;
    auto toks = split(_name, " \t");
    for (auto tok : toks) {
        if (tok == "public" || tok == "private" || tok == "protected")
            continue;
        name += tok;
    }
    return name;
}

static void parse_cpp_outline(const std::vector<std::string>& lines,
                              const std::string& ext,
                              std::vector<RawSymbol>& out) {

    bool is_comment = false;
    bool is_require_braces_begin = false;
    int brace_depth = 0;

    std::vector<RawSymbol> cpp_stack;
    RawSymbol cur;

    for (int i = 0; i < static_cast<int>(lines.size()); i++) {
        auto line = trim_outline(lines[i]);
        if (line.empty())
            continue;
        if (is_in_comment_outline(line))
            continue;
        if (is_comment) {
            if (line.find("*/") != std::string::npos) {
                is_comment = false;
            }
            continue;
        }

        if (line.find("/*") != std::string::npos) {
            is_comment = true;
            continue;
        }
        bool has_braces_open = false;
        bool has_braces_close = false;
        bool has_round_braces = false;
        std::vector<int> braces_pos;
        int round_braces_open_pos = -1;
        int round_braces_close_pos = -1;
        bool func_declare = false;
        bool has_func = false;
        std::string func_name;

        for (int l = 0; l < line.length();l++) {
            auto c = line[l];
            switch (c) {
            case '{':
                brace_depth++;
                has_braces_open = true;
                braces_pos.push_back(l);
                break;
            case '}':
                brace_depth--;
                has_braces_close = true;
                break;
            case '(':
                has_round_braces = true;
                if (has_func || brace_depth > cpp_stack.size())
                    break;
                has_func = true;
                round_braces_open_pos = l;
                // extract func_name: scan backwards from '(' skipping whitespace
                {
                    int pos = l - 1;
                    while (pos >= 0 && std::isspace(line[pos])) pos--;
                    int end = pos + 1;
                    //while (pos >= 0 && (
                    //    std::isalnum(line[pos]) ||
                    //    line[pos] == '_' ||
                    //    line[pos] == '~' ||
                    //    line[pos] == ':')) pos--;
                    pos = 0;
                    func_name = line.substr(pos, end - pos);
                    auto toks = split(func_name, " \t");
                    func_name = "";
                    for (auto tok : toks) {
                        if (tok == "static" || tok == "inline" || tok == "const")
                            continue;
                        pos++;
                        if (pos == 1)
                            continue;
                        if (pos > 2)
                            func_name += " ";
                        func_name += tok;
                    }
                }
                break;
            case ')':
                round_braces_close_pos = l;
                break;
            case ';':
                if (round_braces_close_pos >= 0 && l > round_braces_close_pos) {
                    func_declare = true;
                }
                break;
            }
        }

        int start_pos = 0;
        if (cpp_stack.size() > 0) {
            if (brace_depth <= cur.depth) {
                cpp_stack.pop_back();
                if (cpp_stack.size() > 0) {
                    cur = cpp_stack[cpp_stack.size() - 1];
                }
            }
        }
        if (is_require_braces_begin) {
            if (has_braces_open) {
                cur.name += line.substr(start_pos, braces_pos[0] - start_pos);
                cur.name = normalize_cpp_name(cur.name);
                is_require_braces_begin = false;
                cur.depth = cpp_stack.size();
                cpp_stack.push_back(cur);
                out.push_back(cur);
            }
            else {
                cur.name += line.substr(start_pos);
            }
            auto semicolon = line.find(";");
            if (semicolon != std::string::npos) {
                is_require_braces_begin = false;
            }
        }
        else {
            if (line.rfind("namespace", 0) == 0) {
                is_require_braces_begin = true;
                cur = { i+1, "namespace" };
                start_pos = cur.kind.length() + 1;
            }
            else if (line.rfind("class", 0) == 0) {
                is_require_braces_begin = true;
                cur = { i+1, "class" };
                start_pos = cur.kind.length() + 1;
            }
            else if (line.rfind("struct", 0) == 0) {
                is_require_braces_begin = true;
                cur = { i+1, "struct" };
                start_pos = cur.kind.length() + 1;
            }
            else {
                // check function
                if (has_func && !func_name.empty()) {
                    out.push_back({
                        i + 1, "function", func_name , cpp_stack.size() > 0 ? cur.depth + 1 : 0
                        });
                }
            }
        }

        if (is_require_braces_begin) {
            if (has_braces_open) {
                cur.name = line.substr(start_pos, braces_pos[0] - start_pos);
                cur.name = normalize_cpp_name(cur.name);
                is_require_braces_begin = false;
                cur.depth = cpp_stack.size();
                cpp_stack.push_back(cur);
                out.push_back(cur);
            }
            else {
                cur.name = line.substr(start_pos);
            }
            auto semicolon = line.find(";");
            if (semicolon != std::string::npos) {
                is_require_braces_begin = false;
            }
        }
    }
}

// GCD helper for indent detection
static int _gcd(int a, int b) {
    while (b) { int t = b; b = a % b; a = t; }
    return a;
}

// Detect the file's indent unit by computing GCD of all non-zero leading whitespace lengths.
// Each tab or space counts as 1 unit, so:
//   - tab-only files → GCD=1, depth = tab_count / 1
//   - 4-space files  → GCD=4, depth = space_count / 4
//   - 2-space files  → GCD=2, depth = space_count / 2
static int detect_python_indent_width(const std::vector<std::string>& lines) {
    int gcd_val = 0;
    for (const auto& line : lines) {
        int len = 0;
        for (char c : line) {
            if (c == ' ' || c == '\t') len++;
            else break;
        }
        if (len > 0) {
            if (gcd_val == 0) gcd_val = len;
            else gcd_val = _gcd(gcd_val, len);
        }
    }
    return gcd_val > 0 ? gcd_val : 4; // default to 4
}

static void parse_python_outline(const std::vector<std::string>& lines,
                                  const std::string& ext,
                                  std::vector<RawSymbol>& out) {
    (void)ext;
    int indent_width = detect_python_indent_width(lines);

    for (int i = 0; i < static_cast<int>(lines.size()); i++) {
        std::string trimmed = trim_outline(lines[i]);
        if (trimmed.empty() || is_in_comment_outline(trimmed)) continue;

        int units = 0;
        for (char c : lines[i]) {
            if (c == ' ' || c == '\t') units++;
            else break;
        }
        int depth = units / indent_width;

        if (trimmed.substr(0, 4) == "def ") {
            size_t paren = trimmed.find('(');
            if (paren != std::string::npos) {
                std::string name = trim_outline(trimmed.substr(4, paren - 4));
                if (!name.empty()) {
                    out.push_back({i + 1, "function", name, depth});
                }
            }
        } else if (trimmed.substr(0, 6) == "class ") {
            size_t colon = trimmed.find(':');
            size_t paren = trimmed.find('(');
            size_t end = std::min((colon != std::string::npos ? colon : trimmed.size()),
                                  (paren != std::string::npos ? paren : trimmed.size()));
            if (end > 6) {
                std::string name = trim_outline(trimmed.substr(6, end - 6));
                if (!name.empty()) {
                    out.push_back({i + 1, "class", name, depth});
                }
            }
        }
    }
}

static void parse_js_outline(const std::vector<std::string>& lines,
                              const std::string& ext,
                              std::vector<RawSymbol>& out) {
    (void)ext;
    int brace_depth = 0;
    for (int i = 0; i < static_cast<int>(lines.size()); i++) {
        std::string trimmed = trim_outline(lines[i]);
        if (trimmed.empty() || is_in_comment_outline(trimmed)) continue;

        auto func_pos = trimmed.find("function ");
        if (func_pos != std::string::npos) {
            size_t paren = trimmed.find('(', func_pos);
            if (paren != std::string::npos && paren > func_pos + 9) {
                std::string name = trim_outline(trimmed.substr(func_pos + 9, paren - func_pos - 9));
                if (!name.empty()) {
                    out.push_back({i + 1, "function", name, brace_depth});
                }
            }
        } else if (trimmed.find("class ") != std::string::npos) {
            size_t pos = trimmed.find("class ");
            size_t brace = trimmed.find('{');
            size_t paren = trimmed.find('(');
            size_t end = trimmed.size();
            if (brace != std::string::npos) end = std::min(end, brace);
            if (paren != std::string::npos) end = std::min(end, paren);
            if (end > pos + 6) {
                std::string name = trim_outline(trimmed.substr(pos + 6, end - pos - 6));
                if (!name.empty()) {
                    out.push_back({i + 1, "class", name, brace_depth});
                }
            }
        }

        for (char c : trimmed) {
            if (c == '{') brace_depth++;
            else if (c == '}') brace_depth = std::max(0, brace_depth - 1);
        }
    }
}

// ── Go outline parser ───────────────────────────────

static void parse_go_outline(const std::vector<std::string>& lines,
                              std::vector<RawSymbol>& out) {
    int brace_depth = 0;
    for (int i = 0; i < static_cast<int>(lines.size()); i++) {
        std::string trimmed = trim_outline(lines[i]);
        if (trimmed.empty() || is_in_comment_outline(trimmed)) continue;

        // package name
        if (trimmed.substr(0, 8) == "package ") {
            std::string name = trim_outline(trimmed.substr(8));
            if (!name.empty()) {
                out.push_back({i + 1, "package", name, 0});
            }
        }

        // func Name(...)
        if (trimmed.substr(0, 5) == "func ") {
            size_t paren = trimmed.find('(');
            std::string name;
            if (paren != std::string::npos && paren > 5) {
                name = trim_outline(trimmed.substr(5, paren - 5));
            } else {
                name = trim_outline(trimmed.substr(5));
            }
            if (!name.empty()) {
                out.push_back({i + 1, "function", name, brace_depth});
            }
        }

        // type Name struct / interface
        if (trimmed.substr(0, 5) == "type ") {
            size_t pos = trimmed.find(' ', 5);
            std::string name;
            if (pos != std::string::npos && pos > 5) {
                name = trim_outline(trimmed.substr(5, pos - 5));
            } else {
                name = trim_outline(trimmed.substr(5));
            }
            if (!name.empty()) {
                out.push_back({i + 1, "type", name, brace_depth});
            }
        }

        for (char c : trimmed) {
            if (c == '{') brace_depth++;
            else if (c == '}') brace_depth = std::max(0, brace_depth - 1);
        }
    }
}

// ── Rust outline parser ───────────────────────────────

static void parse_rust_outline(const std::vector<std::string>& lines,
                                std::vector<RawSymbol>& out) {
    int brace_depth = 0;
    for (int i = 0; i < static_cast<int>(lines.size()); i++) {
        std::string trimmed = trim_outline(lines[i]);
        if (trimmed.empty() || is_in_comment_outline(trimmed)) continue;

        // mod name { or mod name;
        if (trimmed.substr(0, 4) == "mod ") {
            size_t end = trimmed.find_first_of("{;", 4);
            if (end != std::string::npos && end > 4) {
                std::string name = trim_outline(trimmed.substr(4, end - 4));
                if (!name.empty()) {
                    out.push_back({i + 1, "mod", name, brace_depth});
                }
            }
        }

        // fn name(...)
        if (trimmed.substr(0, 3) == "fn ") {
            size_t paren = trimmed.find('(');
            std::string name;
            if (paren != std::string::npos && paren > 3) {
                name = trim_outline(trimmed.substr(3, paren - 3));
            } else {
                name = trim_outline(trimmed.substr(3));
            }
            if (!name.empty()) {
                out.push_back({i + 1, "function", name, brace_depth});
            }
        }

        // struct Name, enum Name, trait Name, impl Name
        for (const auto& kw : {std::string("struct "), std::string("enum "),
                               std::string("trait "), std::string("impl ")}) {
            if (trimmed.substr(0, kw.size()) == kw) {
                size_t end = trimmed.find_first_of("{<;", kw.size());
                if (end != std::string::npos && end > kw.size()) {
                    std::string name = trim_outline(trimmed.substr(kw.size(), end - kw.size()));
                    if (!name.empty()) {
                        out.push_back({i + 1, kw.substr(0, kw.size()-1), name, brace_depth});
                    }
                } else {
                    std::string name = trim_outline(trimmed.substr(kw.size()));
                    if (!name.empty()) {
                        out.push_back({i + 1, kw.substr(0, kw.size()-1), name, brace_depth});
                    }
                }
            }
        }

        for (char c : trimmed) {
            if (c == '{') brace_depth++;
            else if (c == '}') brace_depth = std::max(0, brace_depth - 1);
        }
    }
}

// ── Java outline parser ───────────────────────────────

static void parse_java_outline(const std::vector<std::string>& lines,
                                std::vector<RawSymbol>& out) {
    int brace_depth = 0;
    for (int i = 0; i < static_cast<int>(lines.size()); i++) {
        std::string trimmed = trim_outline(lines[i]);
        if (trimmed.empty() || is_in_comment_outline(trimmed)) continue;

        // package name;
        if (trimmed.substr(0, 8) == "package ") {
            size_t semi = trimmed.find(';');
            std::string end_pos = (semi != std::string::npos && semi > 8)
                ? trimmed.substr(8, semi - 8)
                : trimmed.substr(8);
            if (!end_pos.empty()) {
                out.push_back({i + 1, "package", trim_outline(end_pos), 0});
            }
        }

        // class Name, interface Name, enum Name
        for (const auto& kw : {std::string("class "), std::string("interface "),
                               std::string("enum ")}) {
            if (trimmed.substr(0, kw.size()) == kw) {
                size_t end = trimmed.find_first_of("{<;", kw.size());
                if (end != std::string::npos && end > kw.size()) {
                    std::string name = trim_outline(trimmed.substr(kw.size(), end - kw.size()));
                    if (!name.empty()) {
                        out.push_back({i + 1, kw.substr(0, kw.size()-1), name, brace_depth});
                    }
                } else {
                    std::string name = trim_outline(trimmed.substr(kw.size()));
                    if (!name.empty()) {
                        out.push_back({i + 1, kw.substr(0, kw.size()-1), name, brace_depth});
                    }
                }
            }
        }

        // Method: return_type method_name(...)
        size_t paren = trimmed.find('(');
        if (paren != std::string::npos && paren > 0) {
            std::string before_paren = trim_outline(trimmed.substr(0, paren));
            if (!before_paren.empty()) {
                // Skip common keywords that aren't methods.
                static const std::vector<std::string> skip_kw = {
                    "if", "else", "for", "while", "switch", "catch",
                    "new", "return", "throw", "try"
                };
                bool is_method = true;
                for (const auto& k : skip_kw) {
                    if (before_paren == k) { is_method = false; break; }
                }
                // Also skip lines that start with access modifiers + type but have no name
                // (e.g. "public static void main" — we want "main", not "void").
                if (is_method) {
                    size_t last_space = before_paren.find_last_of(' ');
                    std::string mname;
                    if (last_space != std::string::npos && last_space > 0) {
                        mname = trim_outline(before_paren.substr(last_space + 1));
                    } else {
                        mname = before_paren;
                    }
                    if (!mname.empty() && !is_keyword_outline(mname)) {
                        out.push_back({i + 1, "method", mname, brace_depth});
                    }
                }
            }
        }

        for (char c : trimmed) {
            if (c == '{') brace_depth++;
            else if (c == '}') brace_depth = std::max(0, brace_depth - 1);
        }
    }
}

static void parse_markdown_outline(const std::vector<std::string>& lines,
                                    std::vector<RawSymbol>& out) {
    for (int i = 0; i < static_cast<int>(lines.size()); i++) {
        const auto& raw = lines[i];
        // Match ATX headings: one or more '#' followed by a space, then the title.
        if (raw.empty() || raw[0] != '#') continue;

        int depth = 0;
        while (depth < static_cast<int>(raw.size()) && raw[depth] == '#') {
            depth++;
        }

        // Must have a space after the '#' characters.
        if (depth >= static_cast<int>(raw.size()) || raw[depth] != ' ') continue;

        std::string title = trim_outline(raw.substr(depth + 1));
        if (title.empty()) continue;

        out.push_back({i + 1, "heading", title, depth - 1});
    }
}

std::string GenerateFileOutline(const std::string& path) {
    return GenerateFileOutline(path, 0, -1);
}

std::string GenerateFileOutline(const std::string& path, int startLine, int endLine) {
    std::ifstream file(path);
    if (!file.is_open()) return "";

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    file.close();

    int total_lines = static_cast<int>(lines.size());
    if (total_lines == 0) return "# File outline for " + path + " (0)\n";

    // Determine range to scan (1-based → 0-based)
    int from = startLine > 0 ? startLine - 1 : 0;
    int to   = endLine > 0 && endLine <= static_cast<int>(lines.size())
                 ? endLine
                 : static_cast<int>(lines.size());

    if (from >= total_lines) {
        return "# File outline for " + path + " Error: start_line exceeds file length (" + std::to_string(total_lines) + " lines)";
    }

    std::string ext = get_extension_outline(path);

    // Parse symbols with depth tracking
    std::vector<RawSymbol> raw_symbols;

    if (ext == ".c" || ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".cc" || ext == ".cxx") {
        parse_cpp_outline(lines, ext, raw_symbols);
    } else if (ext == ".py") {
        parse_python_outline(lines, ext, raw_symbols);
    } else if (ext == ".js" || ext == ".ts" || ext == ".jsx" || ext == ".tsx") {
        parse_js_outline(lines, ext, raw_symbols);
    } else if (ext == ".go") {
        parse_go_outline(lines, raw_symbols);
    } else if (ext == ".rs") {
        parse_rust_outline(lines, raw_symbols);
    } else if (ext == ".java") {
        parse_java_outline(lines, raw_symbols);
    } else if (ext == ".md") {
        parse_markdown_outline(lines, raw_symbols);
    }

    if (raw_symbols.empty()) {
        std::ostringstream oss;
        oss << "# File outline for " << path << " (" << total_lines << ")\n";
        return oss.str();
    }

    // Compute end_line for each symbol:
    // A symbol's range ends just before the next symbol at same or shallower depth.
    // O(n) via reverse scan + monotonic stack of (depth, start_line).
    std::vector<OutlineSymbol> symbols;
    symbols.reserve(raw_symbols.size());

    int n = static_cast<int>(raw_symbols.size());
    std::vector<int> end_lines(n); // [i] = end_line for symbol i
    // Stack: (depth, start_line), depths strictly increasing from bottom to top.
    std::vector<std::pair<int, int>> stack;

    for (int i = n - 1; i >= 0; --i) {
        int d = raw_symbols[i].depth;
        // Pop deeper entries — they are children of the current symbol
        while (!stack.empty() && stack.back().first > d)
            stack.pop_back();
        if (stack.empty())
            end_lines[i] = total_lines;  // no sibling at same/shallower depth ahead
        else
            end_lines[i] = stack.back().second - 1;
        stack.push_back({d, raw_symbols[i].start_line});
    }

    for (int i = 0; i < n; ++i) {
        OutlineSymbol sym;
        sym.start_line = raw_symbols[i].start_line;
        sym.kind = raw_symbols[i].kind;
        sym.name = raw_symbols[i].name;
        sym.depth = raw_symbols[i].depth;
        sym.end_line = end_lines[i];
        sym.annotation = raw_symbols[i].annotation;
        symbols.push_back(sym);
    }

    // Format output with line numbers and tree-drawing characters
    std::ostringstream oss;
    int width = static_cast<int>(std::to_string(total_lines).size());
    oss << "# File outline for " << path << " (" << std::to_string(total_lines) << ")\n";

    // Determine which symbols are the last child at each depth.
    // A symbol is a last sibling if no later symbol shares its depth.
    std::vector<bool> is_last(symbols.size(), true);
    for (size_t i = 0; i + 1 < symbols.size(); ++i) {
        int d = symbols[i].depth;
        // Scan forward past children to find the next sibling at same or shallower depth.
        size_t j = i + 1;
        while (j < symbols.size() && symbols[j].depth > d)
            j++;
        if (j < symbols.size() && symbols[j].depth == d)
            is_last[i] = false;  // another sibling exists
    }

    // Track which ancestor levels are "last" so we draw │ vs blank.
    std::vector<bool> ancestor_is_last(symbols.size(), true);
    int prev_depth = 0;

    for (size_t i = 0; i < symbols.size(); ++i) {
        const auto& sym = symbols[i];
        // Only show symbols whose start line falls within the requested range
        if (startLine > 0 && sym.start_line < startLine) continue;
        if (endLine > 0 && sym.start_line > endLine) break;

        // When moving back up in depth, clear stale ancestor_is_last entries.
        // e.g. after "func1()" at depth 2 → "Outer2" at depth 0,
        // the old ancestor_is_last[0..1] from Outer1/Inner1 must be cleared
        // so they don't affect Outer2's children.
        if (sym.depth < prev_depth) {
            for (int d = sym.depth; d < prev_depth; d++)
                ancestor_is_last[static_cast<size_t>(d)] = true;
        }
        prev_depth = sym.depth;

        oss << std::setw(width) << sym.start_line << " ";

        // Tree-drawing indentation
        for (int d = 0; d < sym.depth; d++) {
            if (ancestor_is_last[static_cast<size_t>(d)])
                oss << "  ";  // ancestor is last child → blank space
            else
                oss << "│ ";  // ancestor has more siblings below → vertical line
        }

        if (sym.depth > 0) {
            if (is_last[i])
                oss << "└ ";
            else
                oss << "├ ";
        }

        if (sym.kind == "function") {
            oss << sym.name << "()";
            if (!sym.annotation.empty())
                oss << " " << sym.annotation;
            oss << "\n";
        } else {
            oss << sym.kind << " " << sym.name;
            if (!sym.annotation.empty())
                oss << " " << sym.annotation;
            oss << "\n";
        }

        // Update ancestor_is_last for children of this symbol
        ancestor_is_last[static_cast<size_t>(sym.depth)] = is_last[i];
    }

    return oss.str();
}

// -----------------------------------------------------------------------
// Directory listing helper
// -----------------------------------------------------------------------

std::string GenerateDirectoryListing(const std::string& path) {
    namespace fs = std::filesystem;
    if (!fs::exists(path) || !fs::is_directory(path)) return "";

    std::string folders, files;
    int folder_count = 0, file_count = 0;

    for (const auto& entry : fs::directory_iterator(path)) {
        std::string name = entry.path().filename().string();
        if (name == "." || name == "..") continue;

        if (entry.is_directory()) {
            folders += "  " + name + "/\n";
            folder_count++;
        } else {
            files += "  " + name + "\n";
            file_count++;
        }
    }

    std::ostringstream oss;
    oss << "# Folders:\n";
    if (folder_count > 0) oss << folders;
    oss << "\n# Files:\n";
    if (file_count > 0) oss << files;
    oss << "\n(" << folder_count << " directories, " << file_count << " files)";

    return oss.str();
}

// -----------------------------------------------------------------------
// Glob pattern matching helpers
// -----------------------------------------------------------------------

bool match_glob(const std::string& filename, const std::string& pattern) {
    if (pattern.empty()) return true;

    // Handle wildcard at start: *.ext
    size_t star = pattern.find('*');
    if (star == 0 && star + 1 < pattern.size()) {
        std::string suffix = pattern.substr(star + 1);
        size_t fpos = filename.rfind(suffix);
        return fpos != std::string::npos && fpos + suffix.size() == filename.size();
    }

    // Handle wildcard at end: file.*
    if (star != std::string::npos && star + 1 == pattern.size()) {
        std::string prefix = pattern.substr(0, star);
        if (prefix.empty()) return true; // "*" matches anything
        return filename.rfind(prefix) == 0;
    }

    // Handle wildcard in middle: *.txt.bak
    if (star != std::string::npos && star + 1 < pattern.size()) {
        std::string before_star = pattern.substr(0, star);
        std::string after_star = pattern.substr(star + 1);
        size_t pos_before = filename.rfind(before_star);
        if (pos_before == std::string::npos) return false;
        size_t pos_after = filename.find(after_star, pos_before + before_star.size());
        return pos_after != std::string::npos && pos_after == pos_before + before_star.size();
    }

    // No wildcard: exact match
    return filename == pattern;
}

// -----------------------------------------------------------------------
// Base64 helpers
// -----------------------------------------------------------------------

std::string Base64Decode(const std::string& input) {
    static const unsigned char decode_table[] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,62,0,0,0,63,
        52,53,54,55,56,57,58,59,60,61,0,0,0,0,0,0,
        0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,0,0,0,0,0,
        0,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,0,0,0,0,0,
    };

    std::string output;
    if (input.empty()) return output;

    size_t i = 0;
    while (i < input.size()) {
        // Skip whitespace and padding characters
        if (input[i] == '=' || input[i] <= ' ') { ++i; continue; }

        unsigned char a = (i + 3 < input.size() && decode_table[static_cast<unsigned char>(input[i])] != 0) ? decode_table[static_cast<unsigned char>(input[i])] : 0;
        unsigned char b = (i + 1 < input.size() && decode_table[static_cast<unsigned char>(input[i+1])] != 0) ? decode_table[static_cast<unsigned char>(input[i+1])] : 0;
        unsigned char c = (i + 2 < input.size() && decode_table[static_cast<unsigned char>(input[i+2])] != 0) ? decode_table[static_cast<unsigned char>(input[i+2])] : 0;
        unsigned char d = (i + 3 < input.size() && decode_table[static_cast<unsigned char>(input[i+3])] != 0) ? decode_table[static_cast<unsigned char>(input[i+3])] : 0;

        output += static_cast<char>((a << 2) | (b >> 4));
        if (i + 2 < input.size() && input[i + 2] != '=') {
            output += static_cast<char>(((b & 0xF) << 4) | (c >> 2));
        }
        if (i + 3 < input.size() && input[i + 3] != '=') {
            output += static_cast<char>(((c & 0x3) << 6) | d);
        }

        i += 4;
    }

    return output;
}

} // namespace agent
