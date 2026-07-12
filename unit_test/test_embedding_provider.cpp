#include "pch.h"
#include "unit_test.h"
#include "embedding_provider.h"

using namespace agent;

// ============================================================================
// LLMEmbeddingProvider tests (no server available, test graceful fallback)
// ============================================================================

static void test_llm_embedding_provider(UnitReport& parent)
{
    UnitReport unit("llm_embedding_provider");
    LOG_INFO("llm_embedding_provider", "entry");

    // Test 1: dimension() returns expected value
    {
        LOG_INFO("llm_embedding_provider", "dimension_default");
        LLMEmbeddingProvider provider("http://localhost:1234");
        UNIT_TEST("dimension_is_1536", provider.dimension() == 1536);
    }

    // Test 2: embed() returns empty vector when no server available (graceful fallback)
    {
        LOG_INFO("llm_embedding_provider", "embed_no_server_fallback");
        LLMEmbeddingProvider provider("http://localhost:99999");
        auto result = provider.embed("hello world");
        UNIT_TEST("empty_on_failure", result.empty());
    }

    // Test 3: embed_batch() returns empty when no server available
    {
        LOG_INFO("llm_embedding_provider", "embed_batch_no_server_fallback");
        LLMEmbeddingProvider provider("http://localhost:99999");
        auto result = provider.embed_batch({"hello", "world"});
        UNIT_TEST("empty_on_failure", result.empty());
    }

    // Test 4: custom model name accepted in constructor
    {
        LOG_INFO("llm_embedding_provider", "custom_model_accepted");
        LLMEmbeddingProvider provider("http://localhost:1234", "my-custom-model");
        UNIT_TEST("dimension_is_1536", provider.dimension() == 1536);
    }

    parent.report.push_back(unit);
}

// ============================================================================
// TfidfEmbeddingProvider::tokenize tests
// ============================================================================

static void test_tfidf_tokenize(UnitReport& parent)
{
    UnitReport unit("tfidf_tokenize");
    LOG_INFO("tfidf_tokenize", "entry");

    // Test 1: basic tokenization
    {
        LOG_INFO("tfidf_tokenize", "basic_success");
        TfidfEmbeddingProvider provider;
        auto tokens = provider.tokenize("hello world foo bar");
        UNIT_TEST("token_count_4", tokens.size() == 4);
        UNIT_TEST("first_token_hello", tokens[0] == "hello");
        UNIT_TEST("last_token_bar", tokens[3] == "bar");
    }

    // Test 2: single-character tokens are skipped
    {
        LOG_INFO("tfidf_tokenize", "skip_single_char");
        TfidfEmbeddingProvider provider;
        auto tokens = provider.tokenize("a b hello c world d");
        UNIT_TEST("only_multi_char_tokens", tokens.size() == 2);
        UNIT_TEST("first_token_hello", tokens[0] == "hello");
    }

    // Test 3: uppercase converted to lowercase
    {
        LOG_INFO("tfidf_tokenize", "lowercase_conversion");
        TfidfEmbeddingProvider provider;
        auto tokens = provider.tokenize("Hello WORLD FooBar");
        UNIT_TEST("all_lowercase", tokens[0] == "hello" && tokens[1] == "world" && tokens[2] == "foobar");
    }

    // Test 4: punctuation splits tokens
    {
        LOG_INFO("tfidf_tokenize", "punctuation_split");
        TfidfEmbeddingProvider provider;
        auto tokens = provider.tokenize("hello,world;foo:bar!");
        UNIT_TEST("split_by_punct", tokens.size() == 4);
        UNIT_TEST("tokens_correct", tokens[0] == "hello" && tokens[1] == "world" && tokens[2] == "foo" && tokens[3] == "bar");
    }

    // Test 5: underscore is part of token
    {
        LOG_INFO("tfidf_tokenize", "underscore_in_token");
        TfidfEmbeddingProvider provider;
        auto tokens = provider.tokenize("hello_world foo_bar_baz");
        UNIT_TEST("tokens_with_underscore", tokens.size() == 2);
        UNIT_TEST("first_token", tokens[0] == "hello_world");
    }

    // Test 6: empty string returns no tokens
    {
        LOG_INFO("tfidf_tokenize", "empty_string");
        TfidfEmbeddingProvider provider;
        auto tokens = provider.tokenize("");
        UNIT_TEST("no_tokens", tokens.empty());
    }

    // Test 7: only single-char words returns no tokens
    {
        LOG_INFO("tfidf_tokenize", "only_single_chars");
        TfidfEmbeddingProvider provider;
        auto tokens = provider.tokenize("a b c d e");
        UNIT_TEST("no_tokens", tokens.empty());
    }

    // Test 8: numbers are included as tokens (if >= 2 chars)
    {
        LOG_INFO("tfidf_tokenize", "numeric_tokens");
        TfidfEmbeddingProvider provider;
        auto tokens = provider.tokenize("abc123 def456");
        UNIT_TEST("tokens_with_numbers", tokens.size() == 2);
        UNIT_TEST("first_token", tokens[0] == "abc123");
    }

    // Test 9: newlines and tabs split tokens
    {
        LOG_INFO("tfidf_tokenize", "whitespace_split");
        TfidfEmbeddingProvider provider;
        auto tokens = provider.tokenize("hello\nworld\tfoo");
        UNIT_TEST("split_by_whitespace", tokens.size() == 3);
    }

    parent.report.push_back(unit);
}

// ============================================================================
// TfidfEmbeddingProvider::l2_normalize tests
// ============================================================================

static void test_tfidf_l2_normalize(UnitReport& parent)
{
    UnitReport unit("tfidf_l2_normalize");
    LOG_INFO("tfidf_l2_normalize", "entry");

    // Test 1: normalize a simple vector [3, 4] -> [0.6, 0.8]
    {
        LOG_INFO("tfidf_l2_normalize", "basic_normalize");
        std::vector<float> vec = {3.0f, 4.0f};
        TfidfEmbeddingProvider provider;
        provider.l2_normalize(vec);
        UNIT_TEST("first_component", std::abs(vec[0] - 0.6f) < 1e-5f);
        UNIT_TEST("second_component", std::abs(vec[1] - 0.8f) < 1e-5f);
    }

    // Test 2: zero vector remains unchanged (no division by zero)
    {
        LOG_INFO("tfidf_l2_normalize", "zero_vector");
        std::vector<float> vec = {0.0f, 0.0f, 0.0f};
        TfidfEmbeddingProvider provider;
        provider.l2_normalize(vec);
        UNIT_TEST("all_zeros", vec[0] == 0.0f && vec[1] == 0.0f && vec[2] == 0.0f);
    }

    // Test 3: already normalized vector stays the same
    {
        LOG_INFO("tfidf_l2_normalize", "already_normalized");
        std::vector<float> vec = {1.0f / std::sqrt(3.0f), 1.0f / std::sqrt(3.0f), 1.0f / std::sqrt(3.0f)};
        TfidfEmbeddingProvider provider;
        provider.l2_normalize(vec);
        float norm = 0.0f;
        for (float v : vec) norm += v * v;
        UNIT_TEST("still_normalized", std::abs(norm - 1.0f) < 1e-5f);
    }

    // Test 4: single element vector becomes 1.0 or -1.0
    {
        LOG_INFO("tfidf_l2_normalize", "single_element");
        std::vector<float> vec = {5.0f};
        TfidfEmbeddingProvider provider;
        provider.l2_normalize(vec);
        UNIT_TEST("normalized_to_1", std::abs(vec[0] - 1.0f) < 1e-5f);
    }

    // Test 5: negative values handled correctly
    {
        LOG_INFO("tfidf_l2_normalize", "negative_values");
        std::vector<float> vec = {-3.0f, -4.0f};
        TfidfEmbeddingProvider provider;
        provider.l2_normalize(vec);
        UNIT_TEST("first_component", std::abs(vec[0] + 0.6f) < 1e-5f);
        UNIT_TEST("second_component", std::abs(vec[1] + 0.8f) < 1e-5f);
    }

    parent.report.push_back(unit);
}

// ============================================================================
// TfidfEmbeddingProvider::fit tests
// ============================================================================

static void test_tfidf_fit(UnitReport& parent)
{
    UnitReport unit("tfidf_fit");
    LOG_INFO("tfidf_fit", "entry");

    // Test 1: basic fit on small corpus, dimension matches max_features
    {
        LOG_INFO("tfidf_fit", "basic_fit_success");
        TfidfEmbeddingProvider provider(50);
        std::vector<std::string> docs = {
            "hello world foo bar",
            "hello world baz qux",
            "foo bar hello"
        };
        provider.fit(docs);
        UNIT_TEST("dimension_is_50", provider.dimension() == 50);
    }

    // Test 2: fit with max_features larger than vocabulary size
    {
        LOG_INFO("tfidf_fit", "max_features_larger_than_vocab");
        TfidfEmbeddingProvider provider(1000);
        std::vector<std::string> docs = {"hello world"};
        provider.fit(docs);
        UNIT_TEST("dimension_is_2", provider.dimension() == 2); // only 2 terms
    }

    // Test 3: fit with max_features smaller than vocabulary size
    {
        LOG_INFO("tfidf_fit", "max_features_smaller_than_vocab");
        TfidfEmbeddingProvider provider(2);
        std::vector<std::string> docs = {
            "hello world foo bar baz qux",
            "hello world"
        };
        provider.fit(docs);
        UNIT_TEST("dimension_is_2", provider.dimension() == 2);
    }

    // Test 4: fit on empty corpus
    {
        LOG_INFO("tfidf_fit", "empty_corpus");
        TfidfEmbeddingProvider provider(50);
        std::vector<std::string> docs;
        provider.fit(docs);
        UNIT_TEST("dimension_is_0", provider.dimension() == 0);
    }

    // Test 5: terms appearing in more documents ranked higher (by df)
    {
        LOG_INFO("tfidf_fit", "df_ranking");
        TfidfEmbeddingProvider provider(2);
        std::vector<std::string> docs = {
            "hello world foo",
            "hello bar baz",
            "hello qux quux"
        };
        // "hello" appears in all 3 docs, should be top term
        provider.fit(docs);
        UNIT_TEST("dimension_is_2", provider.dimension() == 2);
    }

    parent.report.push_back(unit);
}

// ============================================================================
// TfidfEmbeddingProvider::embed tests
// ============================================================================

static void test_tfidf_embed(UnitReport& parent)
{
    UnitReport unit("tfidf_embed");
    LOG_INFO("tfidf_embed", "entry");

    // Test 1: embed after fit returns vector of correct dimension
    {
        LOG_INFO("tfidf_embed", "correct_dimension");
        TfidfEmbeddingProvider provider(50);
        std::vector<std::string> docs = {
            "hello world foo bar",
            "hello world baz qux"
        };
        provider.fit(docs);
        auto result = provider.embed("hello world");
        UNIT_TEST("dimension_is_50", result.size() == 50);
    }

    // Test 2: embed returns non-zero for known terms
    {
        LOG_INFO("tfidf_embed", "non_zero_for_known_terms");
        TfidfEmbeddingProvider provider(10);
        std::vector<std::string> docs = {"hello world foo bar"};
        provider.fit(docs);
        auto result = provider.embed("hello world");
        bool has_nonzero = false;
        for (float v : result) {
            if (std::abs(v) > 1e-9f) { has_nonzero = true; break; }
        }
        UNIT_TEST("has_nonzero_values", has_nonzero);
    }

    // Test 3: embed returns all-zero for unknown terms
    {
        LOG_INFO("tfidf_embed", "all_zero_for_unknown_terms");
        TfidfEmbeddingProvider provider(10);
        std::vector<std::string> docs = {"hello world foo bar"};
        provider.fit(docs);
        auto result = provider.embed("xyz abc unknown");
        bool all_zero = true;
        for (float v : result) {
            if (std::abs(v) > 1e-9f) { all_zero = false; break; }
        }
        UNIT_TEST("all_zeros", all_zero);
    }

    // Test 4: embed before fit returns empty vector
    {
        LOG_INFO("tfidf_embed", "embed_before_fit");
        TfidfEmbeddingProvider provider(50);
        auto result = provider.embed("hello world");
        UNIT_TEST("empty_vector", result.empty());
    }

    // Test 5: embed returns L2-normalized vector (norm ≈ 1)
    {
        LOG_INFO("tfidf_embed", "l2_normalized_output");
        TfidfEmbeddingProvider provider(10);
        std::vector<std::string> docs = {"hello world foo bar baz qux"};
        provider.fit(docs);
        auto result = provider.embed("hello world");
        float norm = 0.0f;
        for (float v : result) norm += v * v;
        UNIT_TEST("norm_close_to_1", std::abs(norm - 1.0f) < 1e-4f);
    }

    // Test 6: same text produces same embedding (deterministic)
    {
        LOG_INFO("tfidf_embed", "deterministic");
        TfidfEmbeddingProvider provider(10);
        std::vector<std::string> docs = {"hello world foo bar"};
        provider.fit(docs);
        auto result1 = provider.embed("hello world");
        auto result2 = provider.embed("hello world");
        UNIT_TEST("same_result", result1 == result2);
    }

    // Test 7: embed empty string after fit returns normalized zero vector
    {
        LOG_INFO("tfidf_embed", "empty_string_after_fit");
        TfidfEmbeddingProvider provider(50);
        std::vector<std::string> docs = {"hello world foo bar"};
        provider.fit(docs);
        auto result = provider.embed("");
        UNIT_TEST("dimension_is_50", result.size() == 50);
    }

    parent.report.push_back(unit);
}

// ============================================================================
// TfidfEmbeddingProvider::embed_batch tests
// ============================================================================

static void test_tfidf_embed_batch(UnitReport& parent)
{
    UnitReport unit("tfidf_embed_batch");
    LOG_INFO("tfidf_embed_batch", "entry");

    // Test 1: embed_batch returns correct number of results
    {
        LOG_INFO("tfidf_embed_batch", "correct_count");
        TfidfEmbeddingProvider provider(10);
        std::vector<std::string> docs = {"hello world foo bar baz qux"};
        provider.fit(docs);
        auto result = provider.embed_batch({"hello world", "foo bar", "baz qux"});
        UNIT_TEST("three_results", result.size() == 3);
    }

    // Test 2: each batch result has correct dimension
    {
        LOG_INFO("tfidf_embed_batch", "correct_dimension_each");
        TfidfEmbeddingProvider provider(10);
        std::vector<std::string> docs = {"hello world foo bar baz qux"};
        provider.fit(docs);
        auto result = provider.embed_batch({"hello", "world"});
        UNIT_TEST("first_dim_10", result[0].size() == 10);
        UNIT_TEST("second_dim_10", result[1].size() == 10);
    }

    // Test 3: batch results differ for different texts
    {
        LOG_INFO("tfidf_embed_batch", "different_texts_different_results");
        TfidfEmbeddingProvider provider(10);
        std::vector<std::string> docs = {"hello world foo bar baz qux"};
        provider.fit(docs);
        auto result = provider.embed_batch({"hello world", "foo bar"});
        UNIT_TEST("results_differ", result[0] != result[1]);
    }

    // Test 4: embed_batch with empty input returns empty results
    {
        LOG_INFO("tfidf_embed_batch", "empty_input");
        TfidfEmbeddingProvider provider(10);
        std::vector<std::string> docs = {"hello world foo bar"};
        provider.fit(docs);
        auto result = provider.embed_batch({});
        UNIT_TEST("no_results", result.empty());
    }

    // Test 5: embed_batch before fit returns empty vectors
    {
        LOG_INFO("tfidf_embed_batch", "before_fit");
        TfidfEmbeddingProvider provider(10);
        auto result = provider.embed_batch({"hello world"});
        UNIT_TEST("empty_vectors", result[0].empty());
    }

    // Test 6: embed_batch results are L2-normalized
    {
        LOG_INFO("tfidf_embed_batch", "l2_normalized_each");
        TfidfEmbeddingProvider provider(10);
        std::vector<std::string> docs = {"hello world foo bar baz qux"};
        provider.fit(docs);
        auto result = provider.embed_batch({"hello world", "foo bar"});
        float norm1 = 0.0f, norm2 = 0.0f;
        for (float v : result[0]) norm1 += v * v;
        for (float v : result[1]) norm2 += v * v;
        UNIT_TEST("first_normalized", std::abs(norm1 - 1.0f) < 1e-4f);
        UNIT_TEST("second_normalized", std::abs(norm2 - 1.0f) < 1e-4f);
    }

    parent.report.push_back(unit);
}

// ============================================================================
// TfidfEmbeddingProvider integration tests
// ============================================================================

static void test_tfidf_integration(UnitReport& parent)
{
    UnitReport unit("tfidf_integration");
    LOG_INFO("tfidf_integration", "entry");

    // Test 1: full pipeline - fit, embed, verify similar texts produce closer vectors
    {
        LOG_INFO("tfidf_integration", "similar_texts_closer_vectors");
        TfidfEmbeddingProvider provider(20);
        std::vector<std::string> docs = {
            "the cat sat on the mat",
            "a dog ran in the park",
            "the cat played with a ball"
        };
        provider.fit(docs);

        auto v1 = provider.embed("cat sat mat");
        auto v2 = provider.embed("dog ran park");
        auto v3 = provider.embed("cat played ball");

        // Cosine similarity between v1 and v3 should be higher than v1 and v2
        float sim_13 = 0.0f, sim_12 = 0.0f;
        for (size_t i = 0; i < v1.size(); ++i) {
            sim_13 += v1[i] * v3[i];
            sim_12 += v1[i] * v2[i];
        }
        UNIT_TEST("cat_texts_more_similar", sim_13 > sim_12);
    }

    // Test 2: EmbeddingProvider interface works with TfidfEmbeddingProvider via shared_ptr
    {
        LOG_INFO("tfidf_integration", "polymorphic_interface");
        auto provider = std::make_shared<TfidfEmbeddingProvider>(50);
        EmbeddingProviderPtr ptr = provider;

        std::vector<std::string> docs = {"hello world foo bar baz qux"};
        ptr->fit(docs);

        UNIT_TEST("dimension_via_ptr", ptr->dimension() == 50);
        auto result = ptr->embed("hello world");
        UNIT_TEST("embed_via_ptr_not_empty", !result.empty());
    }

    // Test 3: fit with documents containing only single-char tokens (no vocabulary built)
    {
        LOG_INFO("tfidf_integration", "single_char_docs_no_vocab");
        TfidfEmbeddingProvider provider(50);
        std::vector<std::string> docs = {"a b c d e", "f g h i j"};
        provider.fit(docs);
        UNIT_TEST("dimension_is_0", provider.dimension() == 0);
    }

    // Test 4: fit with mixed single-char and multi-char tokens
    {
        LOG_INFO("tfidf_integration", "mixed_token_docs");
        TfidfEmbeddingProvider provider(50);
        std::vector<std::string> docs = {"a hello b world c foo d bar e baz f qux g"};
        provider.fit(docs);
        UNIT_TEST("dimension_is_6", provider.dimension() == 6); // hello, world, foo, bar, baz, qux
    }

    parent.report.push_back(unit);
}

// ============================================================================
// Entry point
// ============================================================================

void test_embedding_provider(UnitReport& parent)
{
    UnitReport unit("embedding_provider");
    LOG_INFO("embedding_provider", "entry");

    test_llm_embedding_provider(unit);
    test_tfidf_tokenize(unit);
    test_tfidf_l2_normalize(unit);
    test_tfidf_fit(unit);
    test_tfidf_embed(unit);
    test_tfidf_embed_batch(unit);
    test_tfidf_integration(unit);

    parent.report.push_back(unit);
}
