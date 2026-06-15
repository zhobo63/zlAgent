#include "pch.h"

#include "language_detector.h"
#include "wide_string.h"

namespace agent {

std::string LanguageDetector::extension_to_language(const std::string& ext) {
    static const std::map<std::string, std::string> lang_map = {
        // C++
        {"cpp",  "cpp"}, {"cxx",  "cpp"}, {"cc",   "cpp"}, {"hpp",  "cpp"},
        {"hxx",  "cpp"}, {"hh",   "cpp"}, {"h",    "cpp"},
        // JavaScript
        {"js",   "js"},  {"jsx",  "js"},  {"mjs",  "js"},
        // TypeScript
        {"ts",   "ts"},  {"tsx",  "ts"},
        // Python
        {"py",   "python"}, {"pyw", "python"},
        // Rust
        {"rs",   "rust"},
        // Go
        {"go",   "go"},
        // Java
        {"java", "java"},
    };

    auto it = lang_map.find(ext);
    if (it != lang_map.end()) return it->second;
    return "";
}

std::string LanguageDetector::detect_directory(const std::string& dir_path) {
    std::map<std::string, int> lang_counts; // language -> file count
    int total_source_files = 0;

    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir_path)) {
            if (!entry.is_regular_file()) continue;

            const auto& path = entry.path();
            std::string ext = path.extension().string();

            // Strip leading dot and lowercase.
            if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            std::string lang = extension_to_language(ext);
            if (lang.empty()) continue; // not a known source file.

            total_source_files++;
            lang_counts[lang]++;
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "[LanguageDetector] Error scanning directory: " << e.what() << std::endl;
        return ""; // let config default take over.
    }

    if (total_source_files == 0) {
        return ""; // no source files found, use config default.
    }

    // If only one language is present, return it.
    if (lang_counts.size() == 1) {
        std::string detected = lang_counts.begin()->first;
        std::cout << "[LanguageDetector] Detected: " << detected
                  << " (" << lang_counts.begin()->second << " files)" << std::endl;
        return detected;
    }

    // Multiple languages - find the dominant one. If it has >60% of files, use it; otherwise multi.
    int max_count = 0;
    for (const auto& [lang, count] : lang_counts) {
        if (count > max_count) max_count = count;
    }

    double dominant_ratio = static_cast<double>(max_count) / total_source_files;
    std::cout << "[LanguageDetector] Found " << lang_counts.size() << " languages ("
              << total_source_files << " source files). ";

    if (dominant_ratio > 0.6) {
        // Find the dominant language name.
        for (const auto& [lang, count] : lang_counts) {
            if (count == max_count) {
                std::cout << "Dominant: " << lang << " (" << static_cast<int>(dominant_ratio * 100) << "%)" << std::endl;
                return lang;
            }
        }
    }

    std::cout << "Mixed project, using multi-language mode." << std::endl;
    return "multi";
}

} // namespace agent
