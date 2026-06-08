#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

namespace agent {

/**
 * Metadata attached to each chunk in the vector store.
 */
struct ChunkMetadata {
    std::string source_file;      // origin file path or "inline"
    int chunk_index = 0;          // position within that document
    int start_line = -1;          // line number (if available)
    std::map<std::string, std::string> tags; // custom key-value labels
};

/**
 * A single indexed entry: embedding vector + content + metadata.
 */
struct VectorEntry {
    std::vector<float> embedding;
    std::string content;
    ChunkMetadata metadata;
};

/**
 * In-memory vector store with cosine similarity search and JSON persistence.
 */
class VectorStore {
public:
    // Insert a new entry (embedding must be L2-normalized for correct cosine).
    void insert(const std::vector<float>& embedding,
                const std::string& content,
                const ChunkMetadata& meta);

    // Search results ranked by cosine similarity.
    struct SearchResult {
        float score;              // 0..1 (higher = more similar)
        std::string content;
        ChunkMetadata metadata;
    };

    // Cosine similarity top-K search. min_score filters out weak matches.
    std::vector<SearchResult> search(
        const std::vector<float>& query_embedding,
        int top_k = 5,
        float min_score = 0.3f) const;

    // Persistence: save/load from a JSON file.
    void save(const std::string& path) const;
    static VectorStore load(const std::string& path);

    size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }

private:
    // Cosine similarity between two vectors (assumes same dimension).
    static float cosine_similarity(const std::vector<float>& a,
                                   const std::vector<float>& b);

    std::vector<VectorEntry> entries_;
};

} // namespace agent
