#include "document_chunker.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

namespace agent {
namespace fs = std::filesystem;

DocumentChunker::DocumentChunker(const Config& cfg) : cfg_(cfg) {}

const std::vector<std::string>& DocumentChunker::default_extensions() {
    static const std::vector<std::string> exts = {
        ".md", ".txt", ".rst", ".adoc",   // documentation
        ".cpp", ".c", ".h", ".hpp",       // C/C++ source
        ".py", ".js", ".ts",              // scripting
        ".json", ".yaml", ".yml",         // config / data
        ".ini", ".cfg",                   // configuration
        ".html", ".xml",                  // markup
    };
    return exts;
}

bool DocumentChunker::is_supported(const std::string& path,
                                    const std::vector<std::string>& extensions) {
    auto ext = fs::path(path).extension().string();
    if (extensions.empty()) {
        // Use default list.
        for (const auto& e : default_extensions()) {
            if (ext == e) return true;
        }
        return false;
    }
    for (const auto& e : extensions) {
        if (ext == e) return true;
    }
    return false;
}

std::string DocumentChunker::read_file_content(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return "";
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

// ---------------------------------------------------------------------------
// Core chunking logic: sliding window with paragraph-aware splitting.
// ---------------------------------------------------------------------------

std::vector<TextChunk> DocumentChunker::chunk(
    const std::string& content, int start_line_offset) const {

    if (content.empty()) return {};

    // Count lines up to a given position.
    auto count_lines = [&](size_t pos) -> int {
        int n = 0;
        for (size_t i = 0; i < pos && i < content.size(); ++i) {
            if (content[i] == '\n') n++;
        }
        return start_line_offset + n;
    };

    std::vector<TextChunk> chunks;
    size_t pos = 0;

    while (pos < content.size()) {
        size_t end = pos + cfg_.chunk_size;

        if (cfg_.respect_paragraphs && end < content.size()) {
            // Try to find a paragraph boundary (double newline) within the chunk.
            size_t best_split = std::string::npos;
            for (size_t i = pos + 64; i < end; ++i) { // minimum 64 chars before considering split
                if (content[i] == '\n' && i + 1 < content.size() && content[i + 1] == '\n') {
                    best_split = i + 2; // skip the blank line
                }
            }
            if (best_split != std::string::npos) {
                end = best_split;
            }
        }

        if (end > content.size()) end = content.size();

        TextChunk ck;
        ck.text = content.substr(pos, end - pos);
        ck.start_line = count_lines(pos);
        chunks.push_back(std::move(ck));

        // Advance with overlap.
        size_t next_pos = end - cfg_.overlap;
        if (next_pos >= end) {
            // Overlap larger than chunk — just move forward by half the chunk.
            next_pos = end - cfg_.chunk_size / 2;
        }
        pos = std::max(pos + 1, next_pos); // ensure progress
    }

    return chunks;
}

DocumentChunker::FileChunks DocumentChunker::chunk_file(const std::string& path) const {
    FileChunks result;
    result.source_path = path;

    std::string content = read_file_content(path);
    if (content.empty()) return result;

    result.chunks = chunk(content, 1); // line numbers start at 1.
    return result;
}

std::vector<DocumentChunker::FileChunks> DocumentChunker::chunk_directory(
    const std::string& dir,
    const std::vector<std::string>& extensions) const {

    std::vector<FileChunks> results;

    if (!fs::exists(dir)) return results;

    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;

        if (!is_supported(entry.path().string(), extensions)) continue;

        FileChunks fc = chunk_file(entry.path().string());
        if (!fc.chunks.empty()) {
            results.push_back(std::move(fc));
        }
    }

    return results;
}

} // namespace agent
