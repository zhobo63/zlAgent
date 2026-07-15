#pragma once

#include <mutex>
#include <string>
#include <vector>
#include <memory>
#include "embedding_provider.h"
#include "vector_store.h"
#include "document_chunker.h"

namespace agent {

/**
 * High-level RAG manager: ingests documents into a vector store and provides
 * semantic search over the knowledge base.
 */
class RAGManager {
public:
    struct Config {
        int top_k = 5;                  // default number of results to return
        float min_score = 0.3f;         // minimum cosine similarity threshold
        std::string store_path = "";    // persistence path (empty = in-memory only)
    };

    RAGManager(EmbeddingProvider* provider, const Config& cfg = {});

    // ── Knowledge Base Ingestion ────────────────────────

    // Add a single text document with an explicit source name.
    void add_document(const std::string& content,
                      const std::string& source_name = "inline");

    // Add a file from disk (auto-detects supported types).
    void add_file(const std::string& path);

    // Recursively scan a directory and ingest all supported files.
    void add_directory(const std::string& dir,
                       const std::vector<std::string>& extensions = {});

    // ── Retrieval ───────────────────────────────────────

    struct RagResult {
        float score;
        std::string content;
        std::string source;
        int chunk_index;
    };

    // Search the knowledge base with a natural language query.
    std::vector<RagResult> search(const std::string& query, int top_k = -1) const;

    // ── State ───────────────────────────────────────────

    size_t total_chunks() const;
    void clear();

    // Persist the vector store to disk.
    void save(const std::string& path) const;

    // Load a persisted vector store into this manager (replaces current store).
    bool load_store(const std::string& store_path);

private:
    mutable std::mutex mutex_;               // protects store_ and needs_fit_
    EmbeddingProvider* provider_;
    Config cfg_;
    VectorStore store_;
    DocumentChunker chunker_;

    // If using TF-IDF, we need to fit on the corpus before embedding.
    bool needs_fit_ = false;
};

} // namespace agent
