#pragma once

#include <fstream>
#include <string>
#include <vector>

namespace agent {

/// Read all lines from a file. Detects whether the file ends with a newline.
/// has_trailing_newline is optional — pass nullptr if you don't need it.
bool read_file_lines(const std::string& path,
                     std::vector<std::string>& out_lines,
                     bool* has_trailing_newline = nullptr);

/// Write lines back to a file. Restores trailing newline if requested.
bool write_file_lines(const std::string& path,
                      const std::vector<std::string>& lines,
                      bool has_trailing_newline = false);

struct EditLines {
    std::vector<std::string> lines;
    bool has_trailing_newline = false;

    /// Read from file — detects trailing newline automatically.
    bool read_file(const std::string& path);

    /// Parse text into lines (e.g. new_text input). Trailing \n is stripped.
    void parse(const std::string& text);

    /// Write back to file — restores trailing newline if original had one.
    bool write_file(const std::string& path) const;
};

/// Read a specific line range from a file.
/// Lines are 1-based (startLine = 1 means the first line).
/// Returns true on success; out contains pairs of (line_number, content_without_newline).
/// Efficient: skips lines before startLine without loading them into memory.
bool ReadFileLines(const std::string& path, int startLine, int endLine,
                   std::vector<std::pair<int, std::string>>& out);

/// Convenience overload that returns the content as a single string with line numbers prefixed.
/// Format: " 10 some text\n" per line. Width based on total file length. Returns empty string on failure.
std::string ReadFileLinesAsString(const std::string& path, int startLine, int endLine);

/// Overload that accepts the total line count to determine proper width for line numbers.
std::string ReadFileLinesAsString(const std::string& path, int startLine, int endLine, int totalLines);

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

/// Overload: generate outline for a specific line range (1-based, inclusive).
/// If start_line <= 0 or end_line > total lines, scans the whole file.
std::string GenerateFileOutline(const std::string& path, int startLine, int endLine);

// -----------------------------------------------------------------------
// Glob pattern matching helpers
// -----------------------------------------------------------------------

/// Match a filename against a glob pattern (supports *.ext wildcard at start).
/// Returns true if the filename matches the pattern, or if pattern is empty.
bool match_glob(const std::string& filename, const std::string& pattern);

// -----------------------------------------------------------------------
// Base64 helpers
// -----------------------------------------------------------------------

/// Decode a base64-encoded string to raw bytes. Returns empty string on failure.
std::string Base64Decode(const std::string& input);

} // namespace agent
