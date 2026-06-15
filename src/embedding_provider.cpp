#include "pch.h"

#include "embedding_provider.h"
#include "json.hpp"
#include "httplib.h"
#include <cmath>
#include <set>

namespace agent {

// ============================================================================
// LLMEmbeddingProvider - calls LM Studio / OpenAI-compatible API
// ============================================================================

LLMEmbeddingProvider::LLMEmbeddingProvider(
    const std::string& base_url,
    const std::string& model)
    : base_url_(base_url), model_(model) {}

std::vector<float> LLMEmbeddingProvider::embed(const std::string& text) const {
    auto batch = embed_batch({text});
    if (!batch.empty()) return batch[0];
    return {}; // fallback: empty vector on failure
}

std::vector<std::vector<float>> LLMEmbeddingProvider::embed_batch(
    const std::vector<std::string>& texts) const {

    // Build JSON body for /v1/embeddings.
    std::ostringstream json;
    json << "{\"model\":\"" << model_ << "\",\"input\":[";
    for (size_t i = 0; i < texts.size(); ++i) {
        if (i > 0) json << ",";
        // Escape quotes in text.
        std::string escaped;
        for (char c : texts[i]) {
            if (c == '"') escaped += "\\\"";
            else if (c == '\\') escaped += "\\\\";
            else if (c == '\n') escaped += "\\n";
            else if (c == '\r') escaped += "\\r";
            else if (c == '\t') escaped += "\\t";
            else escaped += c;
        }
        json << "\"" << escaped << "\"";
    }
    json << "]}";

    // Use raw HTTP via httplib - same approach as LLMClient.
    std::string url = base_url_ + "/v1/embeddings";

    // Parse URL for host/port/ssl.
    struct UrlParts {
        std::string host;
        int port = 80;
        bool is_ssl = false;
    };
    auto parse_url = [](std::string u) -> UrlParts {
        UrlParts parts;
        size_t scheme_end = u.find("://");
        if (scheme_end != std::string::npos) {
            parts.is_ssl = (u.substr(0, scheme_end) == "https");
            u = u.substr(scheme_end + 3);
        }
        size_t host_end = u.find('/');
        if (host_end != std::string::npos) {
            u = u.substr(0, host_end);
        }
        size_t port_pos = u.rfind(':');
        if (port_pos != std::string::npos && port_pos > 0) {
            parts.host = u.substr(0, port_pos);
            try { parts.port = std::stoi(u.substr(port_pos + 1)); }
            catch (...) { parts.port = parts.is_ssl ? 443 : 80; }
        } else {
            parts.host = u;
            parts.port = parts.is_ssl ? 443 : 80;
        }
        return parts;
    };

    auto parts = parse_url(url);

    // Use plain HTTP - local LM Studio typically runs on HTTP.
    httplib::Client client(parts.host, parts.port);

    auto res = client.Post("/v1/embeddings", json.str(), "application/json");
    if (!res || res->status != 200) {
        return {}; // API call failed
    }

    // Parse response: data is an array of objects with "embedding" field.
    std::vector<std::vector<float>> results;
    try {
        auto j = nlohmann::json::parse(res->body);
        if (j.contains("data") && j["data"].is_array()) {
            for (const auto& item : j["data"]) {
                if (item.contains("embedding") && item["embedding"].is_array()) {
                    std::vector<float> vec;
                    for (const auto& val : item["embedding"]) {
                        vec.push_back(val.get<float>());
                    }
                    results.push_back(vec);
                }
            }
        }
    } catch (...) {
        return {}; // parse error - fallback to empty
    }

    return results;
}

// ============================================================================
// TfidfEmbeddingProvider - pure C++ TF-IDF with L2 normalization
// ============================================================================

TfidfEmbeddingProvider::TfidfEmbeddingProvider(int max_features)
    : max_features_(max_features) {}

std::vector<std::string> TfidfEmbeddingProvider::tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::ostringstream oss;
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            oss << static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            if (oss.str().size() >= 2) { // skip single-character tokens
                tokens.push_back(oss.str());
            }
            oss.str("");
        }
    }
    if (oss.str().size() >= 2) {
        tokens.push_back(oss.str());
    }
    return tokens;
}

void TfidfEmbeddingProvider::l2_normalize(std::vector<float>& vec) {
    float norm = 0.0f;
    for (float v : vec) norm += v * v;
    norm = std::sqrt(norm);
    if (norm > 1e-9f) {
        for (float& v : vec) v /= norm;
    }
}

void TfidfEmbeddingProvider::fit(const std::vector<std::string>& documents) {
    // Count document frequency and total term frequency.
    std::map<std::string, TermInfo> term_stats;
    int n_docs = static_cast<int>(documents.size());

    for (const auto& doc : documents) {
        auto tokens = tokenize(doc);
        std::set<std::string> unique_terms(tokens.begin(), tokens.end());

        for (const auto& term : unique_terms) {
            auto& info = term_stats[term];
            info.df++;
            // Count total occurrences in this document.
            int tf = 0;
            for (const auto& t : tokens) {
                if (t == term) tf++;
            }
            info.tf_sum += tf;
        }
    }

    // Sort by document frequency descending, take top max_features_.
    std::vector<std::pair<std::string, TermInfo>> sorted_terms(
        term_stats.begin(), term_stats.end());
    std::sort(sorted_terms.begin(), sorted_terms.end(),
              [](const auto& a, const auto& b) { return a.second.df > b.second.df; });

    size_t limit = std::min(sorted_terms.size(), static_cast<size_t>(max_features_));
    vocabulary_.resize(limit);
    idf_weights_.resize(limit);

    for (size_t i = 0; i < limit; ++i) {
        vocabulary_[i] = sorted_terms[i].first;
        vocab_index_[sorted_terms[i].first] = static_cast<int>(i);
        // IDF: log(N / df) + 1 (smoothed).
        idf_weights_[i] = std::log(static_cast<float>(n_docs) / sorted_terms[i].second.df) + 1.0f;
    }
}

std::vector<float> TfidfEmbeddingProvider::embed(const std::string& text) const {
    auto batch = embed_batch({text});
    if (!batch.empty()) return batch[0];
    return {};
}

std::vector<std::vector<float>> TfidfEmbeddingProvider::embed_batch(
    const std::vector<std::string>& texts) const {

    std::vector<std::vector<float>> results;
    int dim = static_cast<int>(vocabulary_.size());

    for (const auto& text : texts) {
        std::vector<float> vec(dim, 0.0f);

        // Count term frequencies in this document.
        auto tokens = tokenize(text);
        std::map<std::string, int> tf;
        for (const auto& t : tokens) {
            tf[t]++;
        }

        // Build TF-IDF vector using only vocabulary terms.
        for (const auto& [term, count] : tf) {
            auto it = vocab_index_.find(term);
            if (it != vocab_index_.end()) {
                int idx = it->second;
                vec[idx] = static_cast<float>(count) * idf_weights_[idx];
            }
        }

        // L2-normalize.
        l2_normalize(vec);
        results.push_back(std::move(vec));
    }

    return results;
}

} // namespace agent
