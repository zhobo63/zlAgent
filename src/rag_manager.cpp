#include "pch.h"

#include "rag_manager.h"

namespace agent {

RAGManager::RAGManager(EmbeddingProvider* provider, const Config& cfg)
    : provider_(provider), cfg_(cfg) {}

void RAGManager::add_document(const std::string& content,
                               const std::string& source_name) {
    if (!content.empty() && provider_) {
        auto chunks = chunker_.chunk(content);

        // Check needs_fit_ under lock, then do fit outside.
        bool should_fit;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            should_fit = needs_fit_;
        }
        if (should_fit) {
            std::vector<std::string> corpus;
            for (const auto& ck : chunks) corpus.push_back(ck.text);
            provider_->fit(corpus);
        }

        // Embed all chunks in batch — outside lock.
        std::vector<std::string> texts;
        for (const auto& ck : chunks) texts.push_back(ck.text);
        auto embeddings = provider_->embed_batch(texts);

        // Insert into store under lock.
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (should_fit) needs_fit_ = false;
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
}

void RAGManager::add_file(const std::string& path) {
    if (!provider_) return;

    auto fc = chunker_.chunk_file(path);
    if (fc.chunks.empty()) return;

    // Check needs_fit_ under lock, then do fit outside.
    bool should_fit;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        should_fit = needs_fit_;
    }
    if (should_fit) {
        std::vector<std::string> corpus;
        for (const auto& ck : fc.chunks) corpus.push_back(ck.text);
        provider_->fit(corpus);
    }

    // Embed all chunks in batch — outside lock.
    std::vector<std::string> texts;
    for (const auto& ck : fc.chunks) texts.push_back(ck.text);
    auto embeddings = provider_->embed_batch(texts);

    // Insert into store under lock.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (should_fit) needs_fit_ = false;
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
}

void RAGManager::add_directory(const std::string& dir,
                                const std::vector<std::string>& extensions) {
    if (!provider_) return;

    auto files = chunker_.chunk_directory(dir, extensions);
    if (files.empty()) return;

    // Check needs_fit_ under lock, then do fit outside.
    bool should_fit;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        should_fit = needs_fit_;
    }
    if (should_fit) {
        std::vector<std::string> corpus;
        for (const auto& fc : files) {
            for (const auto& ck : fc.chunks) corpus.push_back(ck.text);
        }
        provider_->fit(corpus);
    }

    // Embed file by file — outside lock.
    struct BatchResult {
        std::vector<std::string> texts;
        std::vector<std::vector<float>> embeddings;
        const DocumentChunker::FileChunks* fc = nullptr;
    };
    std::vector<BatchResult> batches;
    for (const auto& fc : files) {
        BatchResult br;
        br.fc = &fc;
        for (const auto& ck : fc.chunks) br.texts.push_back(ck.text);
        br.embeddings = provider_->embed_batch(br.texts);
        batches.push_back(std::move(br));
    }

    // Insert into store under lock.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (should_fit) needs_fit_ = false;
        for (auto& br : batches) {
            const auto& fc = *br.fc;
            for (size_t i = 0; i < fc.chunks.size() && i < br.embeddings.size(); ++i) {
                ChunkMetadata meta;
                meta.source_file = fc.source_path;
                meta.chunk_index = static_cast<int>(i);
                if (fc.chunks[i].start_line >= 0) {
                    meta.start_line = fc.chunks[i].start_line;
                }
                store_.insert(br.embeddings[i], fc.chunks[i].text, meta);
            }
        }
    }
}

std::vector<RAGManager::RagResult> RAGManager::search(
    const std::string& query, int top_k) const {
    bool store_empty;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!provider_ || store_.empty()) return {};
        store_empty = false;  // just for clarity
    }

    auto k = (top_k > 0) ? top_k : cfg_.top_k;

    // Embed the query — outside lock.
    auto q_emb = provider_->embed(query);
    if (q_emb.empty()) return {};

    // Search vector store under lock.
    std::vector<RagResult> rag_results;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto results = store_.search(q_emb, k, cfg_.min_score);
        for (const auto& r : results) {
            RagResult rr;
            rr.score = r.score;
            rr.content = r.content;
            rr.source = r.metadata.source_file;
            rr.chunk_index = r.metadata.chunk_index;
            rag_results.push_back(rr);
        }
    }

    return rag_results;
}

void RAGManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    store_ = VectorStore();
    needs_fit_ = true; // TF-IDF vocabulary is lost.
}

size_t RAGManager::total_chunks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return store_.size();
}

void RAGManager::save(const std::string& path) const {
    // Snapshot under lock, then write outside.
    auto snapshot = VectorStore();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot = store_;
    }
    snapshot.save(path);
}

bool RAGManager::load_store(const std::string& store_path) {
    // Load outside lock.
    auto loaded = VectorStore::load(store_path);
    if (loaded.empty()) return false;
    // Swap into shared state under lock.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        store_ = std::move(loaded);
    }
    return true;
}

} // namespace agent
