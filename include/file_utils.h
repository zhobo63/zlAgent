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

} // namespace agent
