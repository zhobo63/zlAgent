#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

namespace agent {

/**
 * Abstract interface for text embedding providers.
 * Concrete implementations can use LM Studio API or a local TF-IDF model.
 */
class EmbeddingProvider {
public:
    virtual ~EmbeddingProvider() = default;

    // Convert a single text into a normalized vector.
    virtual std::vector<float> embed(const std::string& text) const = 0;

    // Batch embedding (more efficient for multiple texts).
    virtual std::vector<std::vector<float>> embed_batch(
        const std::vector<std::string>& texts) const = 0;

    // Return the dimensionality of produced vectors.
    virtual int dimension() const = 0;

    // Fit the provider on a corpus. No-op by default; overridden by TF-IDF.
    virtual void fit(const std::vector<std::string>& documents) {}
};

using EmbeddingProviderPtr = std::shared_ptr<EmbeddingProvider>;

/**
 * Uses LM Studio / OpenAI-compatible API to produce embeddings via
 * POST /v1/embeddings. Falls back gracefully if the endpoint is unavailable.
 */
class LLMEmbeddingProvider : public EmbeddingProvider {
public:
    explicit LLMEmbeddingProvider(
        const std::string& base_url,
        const std::string& model = "text-embedding-3-small");

    std::vector<float> embed(const std::string& text) const override;
    std::vector<std::vector<float>> embed_batch(
        const std::vector<std::string>& texts) const override;
    int dimension() const override { return 1536; } // default for text-embedding-3-small

private:
    std::string base_url_;
    std::string model_;
};

/**
 * Pure C++ TF-IDF embedding provider. No external dependencies.
 * Must be fit on a corpus before producing meaningful embeddings.
 */
class TfidfEmbeddingProvider : public EmbeddingProvider {
public:
    explicit TfidfEmbeddingProvider(int max_features = 500);

    // Fit the vocabulary and IDF weights on a corpus of texts.
    void fit(const std::vector<std::string>& documents) override;

    std::vector<float> embed(const std::string& text) const override;
    std::vector<std::vector<float>> embed_batch(
        const std::vector<std::string>& texts) const override;
    int dimension() const override { return static_cast<int>(vocabulary_.size()); }

    // Tokenize text into lowercase words (simple whitespace + punctuation split).
    static std::vector<std::string> tokenize(const std::string& text);

    // L2-normalize a vector in-place.
    static void l2_normalize(std::vector<float>& vec);
private:
    int max_features_ = 500;

    // Vocabulary: term -> index (top N by document frequency).
    struct TermInfo {
        int df;       // document frequency
        int tf_sum;   // total term frequency across all docs
    };
    std::vector<std::string> vocabulary_;
    std::map<std::string, int> vocab_index_;
    std::vector<float> idf_weights_;
};

} // namespace agent
