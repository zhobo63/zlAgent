#include "file_utils.h"

#include <fstream>
#include <sstream>
#include <iomanip>

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

} // namespace agent
