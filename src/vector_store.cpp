#include "pch.h"

#include "vector_store.h"
#include <cmath>
#include "json.hpp"

namespace agent {
using json = nlohmann::json;

// Unified helper: dump JSON to a file, throw on failure.
static void write_json_file(const std::string& path, const json& j) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        std::cerr << "Failed to open file for writing: " + path;
        return;
    }
    ofs << j.dump(2);
}

// ============================================================================
// VectorStore
// ============================================================================

void VectorStore::insert(const std::vector<float>& embedding,
                         const std::string& content,
                         const ChunkMetadata& meta) {
    entries_.push_back({embedding, content, meta});
}

std::vector<VectorStore::SearchResult> VectorStore::search(
    const std::vector<float>& query_embedding,
    int top_k,
    float min_score) const {

    struct ScoredEntry {
        float score;
        size_t index;
    };
    std::vector<ScoredEntry> scored;
    scored.reserve(entries_.size());

    for (size_t i = 0; i < entries_.size(); ++i) {
        float s = cosine_similarity(query_embedding, entries_[i].embedding);
        if (s >= min_score) {
            scored.push_back({s, i});
        }
    }

    // Partial sort to get top-K.
    int k = std::min(top_k, static_cast<int>(scored.size()));
    std::partial_sort(scored.begin(), scored.begin() + k, scored.end(),
                     [](const ScoredEntry& a, const ScoredEntry& b) {
                         return a.score > b.score;
                     });

    std::vector<SearchResult> results;
    results.reserve(k);
    for (int i = 0; i < k; ++i) {
        size_t idx = scored[i].index;
        results.push_back({scored[i].score, entries_[idx].content, entries_[idx].metadata});
    }

    return results;
}

void VectorStore::save(const std::string& path) const {
    json root;
    for (const auto& entry : entries_) {
        json e;
        // Serialize embedding as array of floats.
        e["embedding"] = entry.embedding;
        e["content"] = entry.content;
        e["source_file"] = entry.metadata.source_file;
        e["chunk_index"] = entry.metadata.chunk_index;
        if (entry.metadata.start_line >= 0) {
            e["start_line"] = entry.metadata.start_line;
        }
        if (!entry.metadata.tags.empty()) {
            e["tags"] = json(entry.metadata.tags);
        }
        root["entries"].push_back(e);
    }

    write_json_file(path, root);
}

VectorStore VectorStore::load(const std::string& path) {
    VectorStore store;

    std::ifstream in(path);
    if (!in.is_open()) {
        std::cerr << "[VectorStore] Failed to load from: " << path << std::endl;
        return store; // empty store on failure
    }

    try {
        json root = json::parse(in);
        if (root.contains("entries") && root["entries"].is_array()) {
            for (const auto& e : root["entries"]) {
                VectorEntry entry;

                if (e.contains("embedding") && e["embedding"].is_array()) {
                    for (const auto& val : e["embedding"]) {
                        entry.embedding.push_back(val.get<float>());
                    }
                }

                entry.content = e.value("content", "");

                entry.metadata.source_file = e.value("source_file", "unknown");
                entry.metadata.chunk_index = e.value("chunk_index", 0);
                if (e.contains("start_line")) {
                    entry.metadata.start_line = e["start_line"].get<int>();
                }
                if (e.contains("tags") && e["tags"].is_object()) {
                    for (auto it = e["tags"].begin(); it != e["tags"].end(); ++it) {
                        entry.metadata.tags[it.key()] = it.value().get<std::string>();
                    }
                }

                store.insert(entry.embedding, entry.content, entry.metadata);
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "[VectorStore] Parse error loading " << path
                  << ": " << ex.what() << std::endl;
    }

    return store;
}

} // namespace agent
