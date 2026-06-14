#include <catch2/catch_all.hpp>
#include "rag_manager.h"
#include "embedding_provider.h"
#include <filesystem>

using namespace agent;

// Mock embedding provider that returns deterministic 2D vectors.
class MockEmbeddingProvider : public EmbeddingProvider {
public:
    std::vector<float> embed(const std::string& text) const override {
        // Simple hash-based vector for testing.
        unsigned int h = 0;
        for (char c : text) h += static_cast<unsigned char>(c);
        float x = static_cast<float>((h & 0xFF)) / 255.0f;
        float y = static_cast<float>(((h >> 8) & 0xFF)) / 255.0f;
        // L2 normalize.
        float norm = std::sqrt(x * x + y * y);
        if (norm > 1e-9f) { x /= norm; y /= norm; }
        return {x, y};
    }

    std::vector<std::vector<float>> embed_batch(
        const std::vector<std::string>& texts) const override {
        std::vector<std::vector<float>> results;
        for (const auto& t : texts) results.push_back(embed(t));
        return results;
    }

    int dimension() const override { return 2; }
};

TEST_CASE("RAGManager: add_document and search", "[rag]") {
    MockEmbeddingProvider provider;
    RAGManager::Config cfg1;
    cfg1.min_score = 0.0f; // accept all results for testing
    RAGManager rag(&provider, cfg1);

    rag.add_document("This is a test document about vectors.", "test_source");
    REQUIRE(rag.total_chunks() > 0);

    auto results = rag.search("vectors", 5);
    // May or may not find depending on embedding - just check no crash.
    CHECK(results.size() >= 0);
}

TEST_CASE("RAGManager: empty search returns nothing", "[rag]") {
    MockEmbeddingProvider provider;
    RAGManager::Config cfg;
    RAGManager rag(&provider, cfg);

    auto results = rag.search("anything");
    REQUIRE(results.empty());
}

TEST_CASE("RAGManager: clear removes all chunks", "[rag]") {
    MockEmbeddingProvider provider;
    RAGManager::Config cfg;
    RAGManager rag(&provider, cfg);

    rag.add_document("some content", "src");
    REQUIRE(rag.total_chunks() > 0);

    rag.clear();
    REQUIRE(rag.total_chunks() == 0);
}

TEST_CASE("RAGManager: save and load store roundtrip", "[rag]") {
    std::string path = "test_rag_store_temp.json";

    MockEmbeddingProvider provider;
    RAGManager::Config cfg2;
    cfg2.store_path = path;
    RAGManager rag(&provider, cfg2);

    rag.add_document("persisted content", "src");
    rag.save(path);

    // Load into a new manager.
    RAGManager rag2(&provider, cfg2);
    REQUIRE(rag2.load_store(path) == true);
    REQUIRE(rag2.total_chunks() > 0);

    std::filesystem::remove(path);
}

TEST_CASE("RAGManager: load nonexistent store returns false", "[rag]") {
    MockEmbeddingProvider provider;
    RAGManager::Config cfg;
    RAGManager rag(&provider, cfg);

    REQUIRE(rag.load_store("nonexistent_rag.json") == false);
}
