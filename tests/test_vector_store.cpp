#include <catch2/catch_all.hpp>
#include "vector_store.h"
#include <cmath>
#include <filesystem>

using namespace agent;

TEST_CASE("VectorStore: insert and size", "[vstore]") {
    VectorStore store;
    ChunkMetadata meta;
    meta.source_file = "test.txt";
    store.insert({1.0f, 0.0f}, "hello world", meta);
    REQUIRE(store.size() == 1);
}

TEST_CASE("VectorStore: cosine similarity basic", "[vstore]") {
    // Two identical normalized vectors should have similarity ~1.0.
    float sim = VectorStore::cosine_similarity({1.0f, 0.0f}, {1.0f, 0.0f});
    REQUIRE(sim > 0.99f);

    // Orthogonal vectors should have similarity ~0.0.
    sim = VectorStore::cosine_similarity({1.0f, 0.0f}, {0.0f, 1.0f});
    REQUIRE(sim < 0.01f);
}

TEST_CASE("VectorStore: search returns top-K", "[vstore]") {
    VectorStore store;
    ChunkMetadata meta1;
    meta1.source_file = "test.txt";

    // Insert three vectors with different orientations.
    store.insert({1.0f, 0.0f}, "doc A", meta1);
    store.insert({0.7f, 0.7f}, "doc B", meta1);
    store.insert({0.0f, 1.0f}, "doc C", meta1);

    // Query close to doc A.
    auto results = store.search({1.0f, 0.0f}, 2, 0.0f);
    REQUIRE(results.size() == 2);
    REQUIRE(results[0].content == "doc A");
}

TEST_CASE("VectorStore: min_score filters out weak matches", "[vstore]") {
    VectorStore store;
    ChunkMetadata meta2;
    meta2.source_file = "test.txt";

    store.insert({1.0f, 0.0f}, "strong match", meta2);
    store.insert({0.0f, 1.0f}, "weak match", meta2);

    auto results = store.search({1.0f, 0.0f}, 5, 0.8f);
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].content == "strong match");
}

TEST_CASE("VectorStore: empty search returns nothing", "[vstore]") {
    VectorStore store;
    auto results = store.search({1.0f, 0.0f});
    REQUIRE(results.empty());
}

TEST_CASE("VectorStore: JSON save and load roundtrip", "[vstore]") {
    std::string path = "test_vstore_temp.json";

    VectorStore original;
    ChunkMetadata meta3;
    meta3.source_file = "doc1.txt";
    meta3.chunk_index = 0;
    original.insert({0.8f, 0.6f}, "content one", meta3);
    original.save(path);

    auto loaded = VectorStore::load(path);
    REQUIRE(loaded.size() == 1);
    REQUIRE(loaded.search({0.8f, 0.6f}, 1, 0.0f)[0].content == "content one");

    std::filesystem::remove(path);
}

TEST_CASE("VectorStore: load nonexistent file returns empty", "[vstore]") {
    auto loaded = VectorStore::load("nonexistent_vstore.json");
    REQUIRE(loaded.empty());
}
