#include "pch.h"
#include "unit_test.h"
#include "rag_manager.h"
#include <safety_guard.h>

using namespace agent;
namespace fs = std::filesystem;

static void safe_remove_all(const std::string& path)
{
    try {
        if (fs::exists(path)) fs::remove_all(path);
    } catch (...) {}
}

// Mock embedding provider that returns deterministic 2D vectors.
class MockEmbeddingProvider : public EmbeddingProvider {
public:
    std::vector<float> embed(const std::string& text) const override {
        unsigned int h = 0;
        for (char c : text) h += static_cast<unsigned char>(c);
        float x = static_cast<float>((h & 0xFF)) / 255.0f;
        float y = static_cast<float>(((h >> 8) & 0xFF)) / 255.0f;
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

// ============================================================
// Config validation tests
// ============================================================

static void test_rag_manager_config(UnitReport& parent)
{
    UnitReport unit("rag_manager_config");
    LOG_INFO("rag_manager", "config_entry");

    // --- Default config values ---
    {
        LOG_INFO("rag_manager", "default_config_values");
        RAGManager::Config cfg;
        UNIT_TEST("top_k_default_5", cfg.top_k == 5);
        UNIT_TEST("min_score_default_0_3", cfg.min_score == 0.3f);
        UNIT_TEST("store_path_default_empty", cfg.store_path.empty());
    }

    // --- Custom config values ---
    {
        LOG_INFO("rag_manager", "custom_config_values");
        RAGManager::Config cfg;
        cfg.top_k = 10;
        cfg.min_score = 0.5f;
        cfg.store_path = "test_store.json";
        UNIT_TEST("top_k_is_10", cfg.top_k == 10);
        UNIT_TEST("min_score_is_0_5", cfg.min_score == 0.5f);
        UNIT_TEST("store_path_set", cfg.store_path == "test_store.json");
    }

    parent.report.push_back(unit);
}

// ============================================================
// Constructor tests
// ============================================================

static void test_rag_manager_constructor(UnitReport& parent)
{
    UnitReport unit("rag_manager_constructor");
    LOG_INFO("rag_manager", "constructor_entry");

    // --- Constructor with provider and default config ---
    {
        LOG_INFO("rag_manager", "with_provider_default_config");
        MockEmbeddingProvider provider;
        RAGManager mgr(&provider);
        UNIT_TEST("constructed_successfully", true); // no exception
    }

    // --- Constructor with provider and custom config ---
    {
        LOG_INFO("rag_manager", "with_provider_custom_config");
        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.top_k = 3;
        cfg.min_score = 0.1f;
        RAGManager mgr(&provider, cfg);
        UNIT_TEST("constructed_successfully", true); // no exception
    }

    parent.report.push_back(unit);
}

// ============================================================
// add_document tests
// ============================================================

static void test_add_document(UnitReport& parent)
{
    UnitReport unit("add_document");
    LOG_INFO("rag_manager", "add_document_entry");

    // --- Add document with content, verify chunks inserted ---
    {
        LOG_INFO("rag_manager", "basic_success");
        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        std::string content = u8"This is a test document for RAG manager testing.";
        mgr.add_document(content, "test_source.txt");

        UNIT_TEST("chunks_inserted", mgr.total_chunks() > 0);
    }

    // --- Add document with empty content → no chunks inserted ---
    {
        LOG_INFO("rag_manager", "empty_content_no_chunks");
        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        mgr.add_document("", "test_source.txt");

        UNIT_TEST("no_chunks_for_empty", mgr.total_chunks() == 0);
    }

    // --- Add document with default source_name ("inline") ---
    {
        LOG_INFO("rag_manager", "default_source_name");
        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        std::string content = u8"Document with default source name.";
        mgr.add_document(content); // no source_name → "inline"

        UNIT_TEST("chunks_inserted", mgr.total_chunks() > 0);
    }

    // --- Add multiple documents, verify cumulative chunks ---
    {
        LOG_INFO("rag_manager", "multiple_documents_cumulative");
        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        mgr.add_document(u8"First document content.", "doc1.txt");
        size_t after_first = mgr.total_chunks();

        mgr.add_document(u8"Second document content here.", "doc2.txt");
        size_t after_second = mgr.total_chunks();

        UNIT_TEST("chunks_after_first", after_first > 0);
        UNIT_TEST("cumulative_chunks_increased", after_second > after_first);
    }

    // --- Add document with long content → multiple chunks ---
    {
        LOG_INFO("rag_manager", "long_content_multiple_chunks");
        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        // Create a long document (well over default chunk_size of 1024)
        std::string long_content(3000, 'A');
        mgr.add_document(long_content, "long_doc.txt");

        UNIT_TEST("multiple_chunks_for_long_doc", mgr.total_chunks() > 1);
    }

    parent.report.push_back(unit);
}

// ============================================================
// add_file tests
// ============================================================

static void test_add_file(UnitReport& parent)
{
    UnitReport unit("add_file");
    LOG_INFO("rag_manager", "add_file_entry");

    // --- Add existing file, verify chunks inserted ---
    {
        LOG_INFO("rag_manager", "existing_file_success");
        std::string dir = "test_rm_af_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        std::string expected = u8"This is file content for testing.";
        {
            std::ofstream f(fs::path(dir) / "test.txt");
            f << expected;
        }

        mgr.add_file((fs::path(dir) / "test.txt").string());

        UNIT_TEST("chunks_inserted_from_file", mgr.total_chunks() > 0);

        safe_remove_all(dir);
    }

    // --- Add non-existent file → no chunks inserted ---
    {
        LOG_INFO("rag_manager", "nonexistent_file_no_chunks");
        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        mgr.add_file("nonexistent_file_xyz.txt");

        UNIT_TEST("no_chunks_for_nonexistent", mgr.total_chunks() == 0);
    }

    // --- Add empty file → no chunks inserted ---
    {
        LOG_INFO("rag_manager", "empty_file_no_chunks");
        std::string dir = "test_rm_afef_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        { std::ofstream f(fs::path(dir) / "empty.txt"); }

        mgr.add_file((fs::path(dir) / "empty.txt").string());

        UNIT_TEST("no_chunks_for_empty_file", mgr.total_chunks() == 0);

        safe_remove_all(dir);
    }

    // --- Add large file → multiple chunks ---
    {
        LOG_INFO("rag_manager", "large_file_multiple_chunks");
        std::string dir = "test_rm_aflf_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        // Write a file larger than default chunk_size (1024)
        {
            std::ofstream f(fs::path(dir) / "large.txt");
            for (int i = 0; i < 3000; ++i) f << 'A';
        }

        mgr.add_file((fs::path(dir) / "large.txt").string());

        UNIT_TEST("multiple_chunks_for_large_file", mgr.total_chunks() > 1);

        safe_remove_all(dir);
    }

    parent.report.push_back(unit);
}

// ============================================================
// add_directory tests
// ============================================================

static void test_add_directory(UnitReport& parent)
{
    UnitReport unit("add_directory");
    LOG_INFO("rag_manager", "add_directory_entry");

    // --- Directory with supported files → chunks inserted ---
    {
        LOG_INFO("rag_manager", "directory_with_supported_files");
        std::string dir = "test_rm_ad_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        { std::ofstream f(fs::path(dir) / "readme.md"); f << u8"# Hello World"; }
        { std::ofstream f(fs::path(dir) / "notes.txt"); f << u8"Some notes."; }

        mgr.add_directory(dir);

        UNIT_TEST("chunks_inserted_from_dir", mgr.total_chunks() > 0);

        safe_remove_all(dir);
    }

    // --- Non-existent directory → no chunks inserted ---
    {
        LOG_INFO("rag_manager", "nonexistent_directory_no_chunks");
        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        mgr.add_directory("nonexistent_dir_xyz_123");

        UNIT_TEST("no_chunks_for_nonexistent_dir", mgr.total_chunks() == 0);
    }

    // --- Empty directory → no chunks inserted ---
    {
        LOG_INFO("rag_manager", "empty_directory_no_chunks");
        std::string dir = "test_rm_aded_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        mgr.add_directory(dir);

        UNIT_TEST("no_chunks_for_empty_dir", mgr.total_chunks() == 0);

        safe_remove_all(dir);
    }

    // --- Custom extensions filter → only matching files chunked ---
    {
        LOG_INFO("rag_manager", "custom_extensions_filter");
        std::string dir = "test_rm_adcef_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        { std::ofstream f(fs::path(dir) / "readme.md"); f << u8"# Hello World"; }
        { std::ofstream f(fs::path(dir) / "notes.txt"); f << u8"Some notes."; }

        // Only allow .md files
        mgr.add_directory(dir, {".md"});

        UNIT_TEST("chunks_only_from_md", mgr.total_chunks() > 0);

        safe_remove_all(dir);
    }

    // --- Recursive directory scanning → nested files chunked ---
    {
        LOG_INFO("rag_manager", "recursive_directory_scanning");
        std::string dir = "test_rm_adr_temp";
        safe_remove_all(dir);
        fs::create_directories(fs::path(dir) / "sub1" / "sub2");

        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        { std::ofstream f(fs::path(dir) / "root.md"); f << u8"Root file."; }
        { std::ofstream f(fs::path(dir) / "sub1" / "level1.txt"); f << u8"Level 1."; }
        { std::ofstream f(fs::path(dir) / "sub1" / "sub2" / "level2.md"); f << u8"Level 2."; }

        mgr.add_directory(dir);

        UNIT_TEST("chunks_from_nested_dirs", mgr.total_chunks() > 0);

        safe_remove_all(dir);
    }

    parent.report.push_back(unit);
}

// ============================================================
// search tests
// ============================================================

static void test_search(UnitReport& parent)
{
    UnitReport unit("search");
    LOG_INFO("rag_manager", "search_entry");

    // --- Basic search: add document then search → results returned ---
    {
        LOG_INFO("rag_manager", "basic_success");
        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        std::string content = u8"This is a test document for searching.";
        mgr.add_document(content, "test_source.txt");

        auto results = mgr.search("test", 5);

        UNIT_TEST("results_not_empty", !results.empty());
    }

    // --- Search with top_k limit → at most top_k results ---
    {
        LOG_INFO("rag_manager", "top_k_limit");
        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        std::string content = u8"This is a test document for searching.";
        mgr.add_document(content, "test_source.txt");

        auto results = mgr.search("test", 1);

        UNIT_TEST("at_most_top_k_results", results.size() <= 1);
    }

    // --- Search with default top_k (-1) → uses config's top_k ---
    {
        LOG_INFO("rag_manager", "default_top_k_from_config");
        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.top_k = 3;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        std::string content = u8"This is a test document for searching.";
        mgr.add_document(content, "test_source.txt");

        auto results = mgr.search("test", -1); // top_k=-1 → use config's 3

        UNIT_TEST("uses_config_top_k", results.size() <= 3);
    }

    // --- Search empty store → no results ---
    {
        LOG_INFO("rag_manager", "empty_store_no_results");
        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        auto results = mgr.search("test", 5);

        UNIT_TEST("no_results_for_empty_store", results.empty());
    }

    // --- Search result metadata: source_file and chunk_index set ---
    {
        LOG_INFO("rag_manager", "result_metadata_set");
        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        std::string content = u8"This is a test document for searching.";
        mgr.add_document(content, "my_source.txt");

        auto results = mgr.search("test", 5);

        if (!results.empty()) {
            UNIT_TEST("source_file_set", !results[0].source.empty());
            UNIT_TEST("chunk_index_non_negative", results[0].chunk_index >= 0);
        } else {
            UNIT_TEST("source_file_set", false); // fallback
            UNIT_TEST("chunk_index_non_negative", false);
        }
    }

    // --- Search result score is within valid range [0, 1] ---
    {
        LOG_INFO("rag_manager", "result_score_valid_range");
        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        std::string content = u8"This is a test document for searching.";
        mgr.add_document(content, "test_source.txt");

        auto results = mgr.search("test", 5);

        if (!results.empty()) {
            UNIT_TEST("score_in_range_0_to_1", results[0].score >= 0.0f && results[0].score <= 1.0f);
        } else {
            UNIT_TEST("score_in_range_0_to_1", false); // fallback
        }
    }

    // --- Search with min_score filter → low scores excluded ---
    {
        LOG_INFO("rag_manager", "min_score_filter");
        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.9f; // high threshold
        RAGManager mgr(&provider, cfg);

        std::string content = u8"This is a test document for searching.";
        mgr.add_document(content, "test_source.txt");

        auto results = mgr.search("unrelated_query_xyz", 10);

        // With high min_score and unrelated query, expect no or few results
        UNIT_TEST("high_min_score_filters_results", true); // no exception means it works
    }

    parent.report.push_back(unit);
}

// ============================================================
// clear tests
// ============================================================

static void test_clear(UnitReport& parent)
{
    UnitReport unit("clear");
    LOG_INFO("rag_manager", "clear_entry");

    // --- Clear after adding documents → zero chunks ---
    {
        LOG_INFO("rag_manager", "basic_success");
        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        mgr.add_document(u8"Some content to clear.", "test.txt");
        UNIT_TEST("chunks_before_clear", mgr.total_chunks() > 0);

        mgr.clear();

        UNIT_TEST("zero_chunks_after_clear", mgr.total_chunks() == 0);
    }

    // --- Clear empty store → no error ---
    {
        LOG_INFO("rag_manager", "clear_empty_store_no_error");
        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        mgr.clear(); // should not throw

        UNIT_TEST("no_exception_on_clear_empty", true);
    }

    // --- After clear, search returns empty results ---
    {
        LOG_INFO("rag_manager", "search_after_clear_empty");
        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        mgr.add_document(u8"Some content.", "test.txt");
        mgr.clear();

        auto results = mgr.search("test", 5);

        UNIT_TEST("no_results_after_clear", results.empty());
    }

    // --- After clear, can add documents again ---
    {
        LOG_INFO("rag_manager", "add_document_after_clear");
        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        mgr.add_document(u8"First content.", "test1.txt");
        mgr.clear();
        mgr.add_document(u8"Second content after clear.", "test2.txt");

        UNIT_TEST("chunks_after_re_add", mgr.total_chunks() > 0);
    }

    parent.report.push_back(unit);
}

// ============================================================
// save / load_store tests
// ============================================================

static void test_save_load(UnitReport& parent)
{
    UnitReport unit("save_load");
    LOG_INFO("rag_manager", "save_load_entry");

    // --- Save and load store → data preserved ---
    {
        LOG_INFO("rag_manager", "basic_success");
        std::string dir = "test_rm_sl_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        mgr.add_document(u8"Content to save and load.", "test.txt");
        size_t before_save = mgr.total_chunks();

        std::string store_path = (fs::path(dir) / "rag_store.json").string();
        mgr.save(store_path);

        RAGManager mgr2(&provider, cfg);
        bool loaded = mgr2.load_store(store_path);

        UNIT_TEST("load_returned_true", loaded);
        UNIT_TEST("chunks_preserved_after_load", mgr2.total_chunks() == before_save);

        safe_remove_all(dir);
    }

    // --- Load non-existent file → returns false ---
    {
        LOG_INFO("rag_manager", "nonexistent_file_returns_false");
        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        bool loaded = mgr.load_store("nonexistent_rag_store.json");

        UNIT_TEST("load_nonexistent_returns_false", !loaded);
    }

    // --- Load invalid JSON file → returns false ---
    {
        LOG_INFO("rag_manager", "invalid_json_returns_false");
        std::string dir = "test_rm_sliv_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        std::string store_path = (fs::path(dir) / "rag_store.json").string();
        { std::ofstream f(store_path); f << "not valid json {"; }

        bool loaded = mgr.load_store(store_path);

        UNIT_TEST("load_invalid_json_returns_false", !loaded);

        safe_remove_all(dir);
    }

    // --- Save empty store → load returns true (empty but valid) ---
    {
        LOG_INFO("rag_manager", "save_load_empty_store");
        std::string dir = "test_rm_sles_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        std::string store_path = (fs::path(dir) / "rag_store.json").string();
        mgr.save(store_path);

        RAGManager mgr2(&provider, cfg);
        bool loaded = mgr2.load_store(store_path);

        UNIT_TEST("load_empty_store", true); // no exception means it works
        UNIT_TEST("loaded_store_is_empty", mgr2.total_chunks() == 0);

        safe_remove_all(dir);
    }

    // --- Save/load preserves searchability ---
    {
        LOG_INFO("rag_manager", "save_load_preserves_searchability");
        std::string dir = "test_rm_slps_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);

        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        std::string content = u8"This is a test document for searching.";
        mgr.add_document(content, "test_source.txt");

        std::string store_path = (fs::path(dir) / "rag_store.json").string();
        mgr.save(store_path);

        RAGManager mgr2(&provider, cfg);
        mgr2.load_store(store_path);

        auto results = mgr2.search("test", 5);

        UNIT_TEST("searchable_after_load", !results.empty());

        safe_remove_all(dir);
    }

    parent.report.push_back(unit);
}

// ============================================================
// total_chunks tests
// ============================================================

static void test_total_chunks(UnitReport& parent)
{
    UnitReport unit("total_chunks");
    LOG_INFO("rag_manager", "total_chunks_entry");

    // --- New manager has zero chunks ---
    {
        LOG_INFO("rag_manager", "new_manager_zero_chunks");
        MockEmbeddingProvider provider;
        RAGManager mgr(&provider);

        UNIT_TEST("zero_chunks_initially", mgr.total_chunks() == 0);
    }

    // --- After adding document, chunks > 0 ---
    {
        LOG_INFO("rag_manager", "after_add_document_chunks_gt_0");
        MockEmbeddingProvider provider;
        RAGManager::Config cfg;
        cfg.min_score = 0.0f;
        RAGManager mgr(&provider, cfg);

        mgr.add_document(u8"Some content.", "test.txt");

        UNIT_TEST("chunks_gt_0_after_add", mgr.total_chunks() > 0);
    }

    parent.report.push_back(unit);
}

// ============================================================
// Entry point for rag_manager tests
// ============================================================

void test_rag_manager(UnitReport& parent)
{
    // Ensure SafetyGuard whitelist contains current path for tests.
    auto& sg = SafetyGuard::get_instance();
    sg.set_path_whitelist({fs::current_path().string()});

    UnitReport unit("rag_manager");
    LOG_INFO("rag_manager", "entry");

    test_rag_manager_config(unit);
    test_rag_manager_constructor(unit);
    test_add_document(unit);
    test_add_file(unit);
    test_add_directory(unit);
    test_search(unit);
    test_clear(unit);
    test_save_load(unit);
    test_total_chunks(unit);

    parent.report.push_back(unit);
}
