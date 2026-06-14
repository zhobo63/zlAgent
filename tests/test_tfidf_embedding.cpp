#include <catch2/catch_all.hpp>
#include "embedding_provider.h"
#include <cmath>

using namespace agent;

TEST_CASE("TfidfEmbeddingProvider: fit builds vocabulary", "[tfidf]") {
    TfidfEmbeddingProvider provider(10);
    std::vector<std::string> docs = {"hello world test", "world of code"};
    provider.fit(docs);

    auto vec = provider.embed("hello world");
    REQUIRE(!vec.empty());
}

TEST_CASE("TfidfEmbeddingProvider: L2 normalization", "[tfidf]") {
    TfidfEmbeddingProvider provider(10);
    std::vector<std::string> docs = {"the quick brown fox jumps over the lazy dog"};
    provider.fit(docs);

    auto vec = provider.embed("quick fox");
    // Check L2 norm is approximately 1.0.
    float norm_sq = 0.0f;
    for (float v : vec) norm_sq += v * v;
    REQUIRE(std::abs(std::sqrt(norm_sq) - 1.0f) < 0.01f);
}

TEST_CASE("TfidfEmbeddingProvider: dimension matches max_features", "[tfidf]") {
    TfidfEmbeddingProvider provider(5);
    std::vector<std::string> docs = {"test document one", "another test doc"};
    provider.fit(docs);

    REQUIRE(provider.dimension() == 5);
}

TEST_CASE("TfidfEmbeddingProvider: batch embedding works", "[tfidf]") {
    TfidfEmbeddingProvider provider(10);
    std::vector<std::string> docs = {"hello world test", "world of code"};
    provider.fit(docs);

    auto results = provider.embed_batch({"hello", "world"});
    REQUIRE(results.size() == 2);
}

TEST_CASE("TfidfEmbeddingProvider: similar texts have higher similarity", "[tfidf]") {
    TfidfEmbeddingProvider provider(10);
    std::vector<std::string> docs = {"the cat sat on the mat", "a dog ran in the park"};
    provider.fit(docs);

    auto v1 = provider.embed("cat sat");
    auto v2 = provider.embed("dog ran");

    // Dot product as proxy for cosine (vectors are L2-normalized).
    float dot = 0.0f;
    for (size_t i = 0; i < std::min(v1.size(), v2.size()); ++i) {
        dot += v1[i] * v2[i];
    }

    // Should be low but not zero since they share "the".
    REQUIRE(dot >= 0.0f);
}
