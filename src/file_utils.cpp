#include "pch.h"
#include "file_utils.h"
#include "tui.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>

namespace agent {

bool ReadFileLines(const std::string& path, int startLine, int endLine,
                   std::vector<std::pair<int, std::string>>& out) {
    if (startLine <= 0 || endLine < startLine) return false;

    std::ifstream file(path);
    if (!file.is_open()) return false;

    out.clear();
    std::string line;
    int current = 1;

    // Skip lines before the range
    while (current < startLine && std::getline(file, line)) {
        ++current;
    }

    // Read the requested range
    while (current <= endLine && std::getline(file, line)) {
        out.emplace_back(current, line);
        ++current;
    }

    return !out.empty();
}

std::string ReadFileLinesAsString(const std::string& path, int startLine, int endLine) {
    // Count total lines for proper width
    std::ifstream file(path);
    if (!file.is_open()) return "";
    int total_lines = 0;
    std::string dummy;
    while (std::getline(file, dummy)) total_lines++;
    file.close();
    return ReadFileLinesAsString(path, startLine, endLine, total_lines);
}

std::string ReadFileLinesAsString(const std::string& path, int startLine, int endLine, int totalLines) {
    std::vector<std::pair<int, std::string>> lines;
    if (!ReadFileLines(path, startLine, endLine, lines)) return "";

    // Calculate width needed for line numbers based on total file length
    int width = static_cast<int>(std::to_string(totalLines).size());

    std::ostringstream oss;
    for (const auto& [num, content] : lines) {
        oss << std::setw(width) << num << " " << content << "\n";
    }
    return oss.str();
}

// ── read_file_lines / write_file_lines ───────────────────

bool read_file_lines(const std::string& path,
                     std::vector<std::string>& out_lines,
                     bool* has_trailing_newline) {
    std::ifstream infile(path);
    if (!infile.is_open()) return false;
    std::string line;
    while (std::getline(infile, line)) {
        out_lines.push_back(line);
    }
    // Detect whether the file ends with a newline by reading the last byte
    if (has_trailing_newline) {
        infile.clear();
        std::streampos fsize = infile.tellg();
        if (fsize > 0) {
            infile.seekg(-1, std::ios::end);
            char last;
            infile.get(last);
            *has_trailing_newline = (last == '\n');
        } else {
            *has_trailing_newline = false;
        }
    }
    infile.close();
    return true;
}

bool write_file_lines(const std::string& path,
                      const std::vector<std::string>& lines,
                      bool has_trailing_newline) {
    std::ofstream outfile(path, std::ios::trunc);
    if (!outfile.is_open()) return false;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        outfile << lines[i];
        if (i < static_cast<int>(lines.size()) - 1)
            outfile << '\n';
    }
    // Restore trailing newline if the original file had one
    if (has_trailing_newline && !lines.empty())
        outfile << '\n';
    outfile.close();
    return true;
}

// ── EditLines ────────────────────────────────────────────

bool EditLines::read_file(const std::string& path) {
    lines.clear();
    has_trailing_newline = false;
    return read_file_lines(path, lines, &has_trailing_newline);
}

void EditLines::parse(const std::string& text) {
    lines.clear();
    if (text.empty()) return;

    // If the entire content is newlines, each newline represents one empty line
    bool all_newlines = true;
    for (char c : text) {
        if (c != '\n') { all_newlines = false; break; }
    }
    if (all_newlines) {
        lines.resize(text.size());
        return;
    }

    // Remove trailing newline so getline doesn't produce an extra empty element
    std::string trimmed = text;
    if (!trimmed.empty() && trimmed.back() == '\n')
        trimmed.pop_back();

    std::istringstream iss(trimmed);
    std::string line;
    while (std::getline(iss, line)) {
        lines.push_back(line);
    }
}

bool EditLines::write_file(const std::string& path) const {
    return write_file_lines(path, lines, has_trailing_newline);
}

std::string EditLines::to_string() const {
    std::string s;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        if (i > 0) s += '\n';
        s += lines[i];
    }
    return s;
}

// ── EditFile implementation ───────────────────────────────

static std::vector<std::string> split_edit_lines(const std::string& text) {
    EditLines el;
    el.parse(text);
    return el.lines;
}

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
    // Reconstruct content and find all occurrences of old_text.
    // Returns (line_number_of_start, content_position) for each match.
    std::string content;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        if (i > 0) content += '\n';
        content += lines[i];
    }

    std::vector<std::pair<int, size_t>> results;
    if (old_text.empty()) return results;

    size_t search_pos = 0;
    while (true) {
        auto found = content.find(old_text, search_pos);
        if (found == std::string::npos) break;
        int line_num = pos_to_line(lines, found);
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
            auto insert_lines = split_edit_lines(block.new_content);
            for (const auto& il : insert_lines)
                result.push_back(il);
        } else {
            if (block.end < 1 || block.end > total_lines)
                return;
            auto replace_lines = split_edit_lines(block.new_content);
            for (const auto& rl : replace_lines)
                result.push_back(rl);
            current_line = block.end + 1;
        }
    }

    while (current_line <= total_lines) {
        result.push_back(lines[current_line - 1]);
        ++current_line;
    }

    out_lines.lines = std::move(result);
    out_lines.has_trailing_newline = has_trailing_newline;
}

} // namespace agent

namespace agent {

// ── Helpers for DiffEdit ───────────────────────────────

static std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> lines;
    if (s.empty()) return lines;
    std::istringstream iss(s);
    std::string line;
    while (std::getline(iss, line)) {
        // Remove trailing \r for Windows compatibility
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

std::string DiffEdit(const std::string& old_text,
                     const std::string& new_text,
                     int start_line) {
    auto old_lines = splitLines(old_text);
    auto new_lines = splitLines(new_text);

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

// ── Helpers for GenerateFileOutline ───────────────────────

static std::string trim_outline(const std::string& s) {
    size_t start = 0, end = s.size();
    while (start < end && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r')) start++;
    while (end > start && (s[end-1] == ' ' || s[end-1] == '\t' || s[end-1] == '\r')) end--;
    return s.substr(start, end - start);
}

static std::string get_extension_outline(const std::string& path) {
    auto pos = path.rfind('.');
    if (pos == std::string::npos) return "";
    std::string ext = path.substr(pos);
    for (auto& c : ext) c = static_cast<char>(std::tolower(c));
    return ext;
}

static bool is_in_comment_outline(const std::string& line) {
    auto trimmed = trim_outline(line);
    if (trimmed.empty()) return false;
    if (trimmed[0] == '/' && trimmed.size() >= 2 && (trimmed[1] == '/' || trimmed[1] == '*'))
        return true;
    if (trimmed[0] == '#')
        return true;
    return false;
}

static bool is_keyword_outline(const std::string& s) {
    static const char* keywords[] = {
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
    for (const auto* kw : keywords) {
        if (s == kw) return true;
    }
    return false;
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
    int depth;       // nesting level at time of discovery
};

static void parse_cpp_outline(const std::vector<std::string>& lines,
                              const std::string& ext,
                              std::vector<RawSymbol>& out) {
    (void)ext;
    int brace_depth = 0;
    for (int i = 0; i < static_cast<int>(lines.size()); i++) {
        std::string trimmed = trim_outline(lines[i]);
        if (trimmed.empty() || is_in_comment_outline(trimmed)) continue;

        // namespace Name {
        auto ns_pos = trimmed.find("namespace ");
        if (ns_pos != std::string::npos) {
            std::string name = extract_identifier_outline(trimmed, ns_pos + 10);
            if (!name.empty()) {
                out.push_back({i + 1, "namespace", name, brace_depth});
            }
            for (char c : trimmed) {
                if (c == '{') brace_depth++;
                else if (c == '}') brace_depth = std::max(0, brace_depth - 1);
            }
            continue;
        }

        // class Name { or struct Name {
        for (const auto& kw : {std::string("class "), std::string("struct ")}) {
            size_t pos = trimmed.find(kw);
            if (pos != std::string::npos) {
                std::string name = extract_identifier_outline(trimmed, pos + kw.size());
                if (!name.empty()) {
                    out.push_back({i + 1, kw.substr(0, kw.size()-1), name, brace_depth});
                }
                for (char c : trimmed) {
                    if (c == '{') brace_depth++;
                    else if (c == '}') brace_depth = std::max(0, brace_depth - 1);
                }
                goto next_line_cpp;
            }
        }

        // Function: type name(...)
        size_t paren = trimmed.find('(');
        if (paren != std::string::npos && paren > 0) {
            std::string before_paren = trim_outline(trimmed.substr(0, paren));
            if (!before_paren.empty() && !is_keyword_outline(before_paren)) {
                size_t last_space = before_paren.find_last_of(' ');
                std::string fname;
                if (last_space != std::string::npos && last_space > 0) {
                    fname = trim_outline(before_paren.substr(last_space + 1));
                } else {
                    fname = before_paren;
                }
                if (!fname.empty() && !is_keyword_outline(fname)) {
                    out.push_back({i + 1, "function", fname, brace_depth});
                }
            }
        }

        // Count braces on this line (for non-symbol lines too)
        for (char c : trimmed) {
            if (c == '{') brace_depth++;
            else if (c == '}') brace_depth = std::max(0, brace_depth - 1);
        }

    next_line_cpp:;
    }
}

static void parse_python_outline(const std::vector<std::string>& lines,
                                  const std::string& ext,
                                  std::vector<RawSymbol>& out) {
    (void)ext;
    for (int i = 0; i < static_cast<int>(lines.size()); i++) {
        std::string trimmed = trim_outline(lines[i]);
        if (trimmed.empty() || is_in_comment_outline(trimmed)) continue;

        int indent = 0;
        for (char c : lines[i]) {
            if (c == ' ') indent++;
            else if (c == '\t') indent += 4;
            else break;
        }
        int depth = indent / 4;

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
    std::vector<OutlineSymbol> symbols;

    for (int i = 0; i < static_cast<int>(raw_symbols.size()); i++) {
        OutlineSymbol sym;
        sym.start_line = raw_symbols[i].start_line;
        sym.kind = raw_symbols[i].kind;
        sym.name = raw_symbols[i].name;
        sym.depth = raw_symbols[i].depth;

        int end = total_lines; // default to last line
        for (int j = i + 1; j < static_cast<int>(raw_symbols.size()); j++) {
            if (raw_symbols[j].depth <= raw_symbols[i].depth) {
                end = raw_symbols[j].start_line - 1;
                break;
            }
        }

        sym.end_line = end;
        symbols.push_back(sym);
    }

    // Format output with line numbers
    std::ostringstream oss;
    int width = static_cast<int>(std::to_string(total_lines).size());
    oss << "# File outline for " << path << " (" << total_lines << ")\n";

    for (const auto& sym : symbols) {
        // Only show symbols whose start line falls within the requested range
        if (startLine > 0 && sym.start_line < startLine) continue;
        if (endLine > 0 && sym.start_line > endLine) break;

        oss << std::setw(width) << sym.start_line << " ";

        // Indentation: two spaces per depth level
        for (int d = 0; d < sym.depth; d++) {
            oss << "  ";
        }

        if (sym.kind == "function") {
            oss << sym.name << "()\n";
        } else {
            oss << sym.kind << " " << sym.name << "\n";
        }
    }

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
