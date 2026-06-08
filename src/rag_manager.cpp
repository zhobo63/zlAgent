#include "rag_manager.h"
#include <iostream>
#include <algorithm>

namespace agent {

RAGManager::RAGManager(EmbeddingProvider* provider, const Config& cfg)
    : provider_(provider), cfg_(cfg) {}

void RAGManager::add_document(const std::string& content,
                               const std::string& source_name) {
    if (!content.empty() && provider_) {
        auto chunks = chunker_.chunk(content);

        // For TF-IDF: collect all text first to fit the vocabulary.
        if (needs_fit_) {
            std::vector<std::string> corpus;
            for (const auto& ck : chunks) corpus.push_back(ck.text);
            static_cast<TfidfEmbeddingProvider*>(provider_)->fit(corpus);
            needs_fit_ = false;
        }

        // Embed all chunks in batch.
        std::vector<std::string> texts;
        for (const auto& ck : chunks) texts.push_back(ck.text);
        auto embeddings = provider_->embed_batch(texts);

        for (size_t i = 0; i < chunks.size() && i < embeddings.size(); ++i) {
            ChunkMetadata meta;
            meta.source_file = source_name;
            meta.chunk_index = static_cast<int>(i);
            if (chunks[i].start_line >= 0) {
                meta.start_line = chunks[i].start_line;
            }

            store_.insert(embeddings[i], chunks[i].text, meta);
        }
    }
}

void RAGManager::add_file(const std::string& path) {
    if (!provider_) return;

    auto fc = chunker_.chunk_file(path);
    if (fc.chunks.empty()) return;

    // For TF-IDF: fit on this file's chunks.
    if (needs_fit_) {
        std::vector<std::string> corpus;
        for (const auto& ck : fc.chunks) corpus.push_back(ck.text);
        static_cast<TfidfEmbeddingProvider*>(provider_)->fit(corpus);
        needs_fit_ = false;
    }

    // Embed all chunks in batch.
    std::vector<std::string> texts;
    for (const auto& ck : fc.chunks) texts.push_back(ck.text);
    auto embeddings = provider_->embed_batch(texts);

    for (size_t i = 0; i < fc.chunks.size() && i < embeddings.size(); ++i) {
        ChunkMetadata meta;
        meta.source_file = path;
        meta.chunk_index = static_cast<int>(i);
        if (fc.chunks[i].start_line >= 0) {
            meta.start_line = fc.chunks[i].start_line;
        }

        store_.insert(embeddings[i], fc.chunks[i].text, meta);
    }
}

void RAGManager::add_directory(const std::string& dir,
                                const std::vector<std::string>& extensions) {
    if (!provider_) return;

    auto files = chunker_.chunk_directory(dir, extensions);
    if (files.empty()) return;

    // For TF-IDF: collect all chunks across all files to fit the vocabulary.
    if (needs_fit_) {
        std::vector<std::string> corpus;
        for (const auto& fc : files) {
            for (const auto& ck : fc.chunks) corpus.push_back(ck.text);
        }
        static_cast<TfidfEmbeddingProvider*>(provider_)->fit(corpus);
        needs_fit_ = false;
    }

    // Embed file by file.
    for (const auto& fc : files) {
        std::vector<std::string> texts;
        for (const auto& ck : fc.chunks) texts.push_back(ck.text);
        auto embeddings = provider_->embed_batch(texts);

        for (size_t i = 0; i < fc.chunks.size() && i < embeddings.size(); ++i) {
            ChunkMetadata meta;
            meta.source_file = fc.source_path;
            meta.chunk_index = static_cast<int>(i);
            if (fc.chunks[i].start_line >= 0) {
                meta.start_line = fc.chunks[i].start_line;
            }

            store_.insert(embeddings[i], fc.chunks[i].text, meta);
        }
    }
}

std::vector<RAGManager::RagResult> RAGManager::search(
    const std::string& query, int top_k) const {
    if (!provider_ || store_.empty()) return {};

    auto k = (top_k > 0) ? top_k : cfg_.top_k;

    // Embed the query.
    auto q_emb = provider_->embed(query);
    if (q_emb.empty()) return {};

    // Search vector store.
    auto results = store_.search(q_emb, k, cfg_.min_score);

    std::vector<RagResult> rag_results;
    for (const auto& r : results) {
        RagResult rr;
        rr.score = r.score;
        rr.content = r.content;
        rr.source = r.metadata.source_file;
        rr.chunk_index = r.metadata.chunk_index;
        rag_results.push_back(rr);
    }

    return rag_results;
}

void RAGManager::clear() {
    store_ = VectorStore();
    needs_fit_ = true; // TF-IDF vocabulary is lost.
}

void RAGManager::save(const std::string& path) const {
    store_.save(path);
}

bool RAGManager::load_store(const std::string& store_path) {
    auto loaded = VectorStore::load(store_path);
    if (loaded.empty()) return false;
    store_ = std::move(loaded);
    return true;
}

} // namespace agent
