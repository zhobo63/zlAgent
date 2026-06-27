#pragma once

#include <string>
#include <vector>

namespace agent {

/// Read a specific line range from a file.
/// Lines are 1-based (startLine = 1 means the first line).
/// Returns true on success; out contains pairs of (line_number, content_without_newline).
/// Efficient: skips lines before startLine without loading them into memory.
bool ReadFileLines(const std::string& path, int startLine, int endLine,
                   std::vector<std::pair<int, std::string>>& out);

/// Convenience overload that returns the content as a single string with line numbers prefixed.
/// Format: " 10 | some text\n" per line. Returns empty string on failure.
std::string ReadFileLinesAsString(const std::string& path, int startLine, int endLine);

/// Generate a colored unified diff between old_text and new_text.
/// Returns ANSI-colored string with red for removed lines (-) and green for added lines (+).
/// Context lines (unchanged) are shown without color.
std::string DiffEdit(const std::string& old_text, const std::string& new_text);

// -----------------------------------------------------------------------
// File outline helpers
// -----------------------------------------------------------------------

struct OutlineSymbol {
    int start_line;      // 1-based
    int end_line;        // 0 if single-line item, otherwise 1-based inclusive
    std::string kind;    // "namespace", "class", "struct", "function"
    std::string name;
    int depth;           // nesting level (0 = top-level)
};

/// Generate a file outline with symbol names and line ranges.
/// Returns empty string on failure or if the file has no recognizable symbols.
std::string GenerateFileOutline(const std::string& path);

} // namespace agent
