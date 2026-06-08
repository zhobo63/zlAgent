#pragma once

#include <string>
#include <vector>

namespace agent {

/**
 * A chunk of text extracted from a document during splitting.
 */
struct TextChunk {
    std::string text;
    int start_line = -1;   // line number in the original file (if available)
};

/**
 * Splits documents into overlapping chunks suitable for embedding and retrieval.
 * Respects paragraph boundaries when possible to avoid cutting sentences mid-way.
 */
class DocumentChunker {
public:
    struct Config {
        int chunk_size = 1024;           // max characters per chunk
        int overlap = 200;               // overlapping characters between adjacent chunks
        bool respect_paragraphs = true;  // try to split at paragraph boundaries (double newline)
    };

    explicit DocumentChunker(const Config& cfg = {});

    // Split raw text content into chunks.
    std::vector<TextChunk> chunk(const std::string& content, int start_line_offset = 0) const;

    // Read a file and split it into chunks.
    struct FileChunks {
        std::string source_path;
        std::vector<TextChunk> chunks;
    };
    FileChunks chunk_file(const std::string& path) const;

    // Scan a directory for supported files and chunk them all.
    std::vector<FileChunks> chunk_directory(
        const std::string& dir,
        const std::vector<std::string>& extensions = {}) const;

private:
    Config cfg_;

    // Default supported file extensions.
    static const std::vector<std::string>& default_extensions();

    // Check if a file extension is supported.
    static bool is_supported(const std::string& path,
                             const std::vector<std::string>& extensions);

    // Read file content as string; returns empty on failure.
    static std::string read_file_content(const std::string& path);
};

} // namespace agent
