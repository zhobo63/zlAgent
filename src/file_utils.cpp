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

} // namespace agent
