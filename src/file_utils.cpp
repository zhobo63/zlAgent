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
    std::vector<std::pair<int, std::string>> lines;
    if (!ReadFileLines(path, startLine, endLine, lines)) return "";

    // Calculate width needed for line numbers (e.g., 5 chars for up to 99999)
    int width = static_cast<int>(std::to_string(endLine).size());

    std::ostringstream oss;
    for (const auto& [num, content] : lines) {
        oss << std::setw(width) << num << " | " << content << "\n";
    }
    return oss.str();
}

}

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

std::string DiffEdit(const std::string& old_text, const std::string& new_text) {
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

    // Build diff output
    std::ostringstream oss;
    int oi = 0, ni = 0, li = 0;

    while (oi < m || ni < n) {
        // Emit removed lines (old only)
        while (oi < m &&
               (li >= static_cast<int>(lcs_lines.size()) || old_lines[oi] != lcs_lines[li])) {
            oss << TUI::ANSI_FG_RED << "-" << old_lines[oi] << TUI::ANSI_RESET << "\n";
            ++oi;
        }
        // Emit added lines (new only)
        while (ni < n &&
               (li >= static_cast<int>(lcs_lines.size()) || new_lines[ni] != lcs_lines[li])) {
            oss << TUI::ANSI_FG_GREEN << "+" << new_lines[ni] << TUI::ANSI_RESET << "\n";
            ++ni;
        }
        // Emit context line (LCS match)
        if (li < static_cast<int>(lcs_lines.size())) {
            oss << " " << lcs_lines[li] << "\n";
            ++oi; ++ni; ++li;
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
        if (trimmed.substr(0, 7) == "package ") {
            std::string name = trim_outline(trimmed.substr(8));
            if (!name.empty()) {
                out.push_back({i + 1, "package", name, 0});
            }
        }

        // func Name(...)
        if (trimmed.substr(0, 4) == "func ") {
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
        if (trimmed.substr(0, 4) == "type ") {
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
        if (trimmed.substr(0, 3) == "mod ") {
            size_t end = trimmed.find_first_of("{;", 4);
            if (end != std::string::npos && end > 4) {
                std::string name = trim_outline(trimmed.substr(4, end - 4));
                if (!name.empty()) {
                    out.push_back({i + 1, "mod", name, brace_depth});
                }
            }
        }

        // fn name(...)
        if (trimmed.substr(0, 2) == "fn ") {
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
    std::ifstream file(path);
    if (!file.is_open()) return "";

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    file.close();

    if (lines.empty()) return "File is empty.";

    std::string ext = get_extension_outline(path);

    // Parse symbols with depth tracking
    std::vector<RawSymbol> raw_symbols;

    if (ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".cc" || ext == ".cxx") {
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

    if (raw_symbols.empty()) return "No symbols found in '" + path + "'.";

    // Compute end_line for each symbol:
    // A symbol's range ends just before the next symbol at same or shallower depth.
    std::vector<OutlineSymbol> symbols;
    int total_lines = static_cast<int>(lines.size());

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

    // Format output
    std::ostringstream oss;
    oss << "# File outline for " << path << "\n\n";

    for (const auto& sym : symbols) {
        // Indentation: one space per depth level
        for (int d = 0; d < sym.depth; d++) {
            oss << " ";
        }

        if (sym.kind == "function") {
            oss << sym.name << "()";
        } else {
            oss << sym.kind << " " << sym.name;
        }

        // Range: show [Lstart-end] for multi-line, [Lline] for single-line
        if (sym.end_line > sym.start_line) {
            oss << " [L" << sym.start_line << "-" << sym.end_line << "]";
        } else {
            oss << " [L" << sym.start_line << "]";
        }

        oss << "\n";
    }

    return oss.str();
}

} // namespace agent
