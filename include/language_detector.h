#pragma once

#include <string>
#include <map>

namespace agent {

/**
 * Detects the primary programming language of a directory by scanning file extensions.
 * Returns "multi" if multiple languages are found, or the dominant single language.
 */
class LanguageDetector {
public:
    // Map a file extension (lowercase, without dot) to a language identifier.
    static std::string extension_to_language(const std::string& ext);

    // Scan a directory recursively and return the detected primary language.
    // Returns "multi" if multiple languages are present, or the dominant one.
    // If no known source files found, returns "unknown".
    static std::string detect_directory(const std::string& dir_path = ".");

private:
    LanguageDetector() = default;
};

} // namespace agent
