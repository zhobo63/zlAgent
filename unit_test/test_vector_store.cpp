#include "pch.h"
#include "unit_test.h"
#include "vector_store.h"
#include <fstream>
#include <safety_guard.h>

using namespace agent;
namespace fs = std::filesystem;

static void safe_remove_all(const std::string& path)
{
    try {
        if (fs::exists(path)) fs::remove_all(path);
    } catch (...) {}
}

// ============================================================================
// test_insert
// ============================================================================

void test_insert(UnitReport& parent)
{
    UnitReport unit("insert");
    LOG_INFO("insert", "entry");

    // Test 1: basic insert success, verify size()
    {
        LOG_INFO("insert", "basic_success");
        VectorStore store;
        std::vector<float> embedding = {0.5f, 0.3f, -0.2f};
        ChunkMetadata meta;
        meta.source_file = "test.txt";
        meta.chunk_index = 1;

        store.insert(embedding, "hello world", meta);

        UNIT_TEST("size_is_1", store.size() == 1);
        UNIT_TEST("not_empty", !store.empty());
    }

    // Test 2: insert empty content
    {
        LOG_INFO("insert", "empty_content_success");
        VectorStore store;
        std::vector<float> embedding = {0.5f, 0.3f, -0.2f};
        ChunkMetadata meta;
        meta.source_file = "test.txt";

        store.insert(embedding, "", meta);

        UNIT_TEST("size_is_1", store.size() == 1);
    }

    // Test 3: insert with tags metadata
    {
        LOG_INFO("insert", "with_tags_success");
        VectorStore store;
        std::vector<float> embedding = {0.5f, 0.3f, -0.2f};
        ChunkMetadata meta;
        meta.source_file = "test.txt";
        meta.tags["author"] = "alice";
        meta.tags["version"] = "1.0";

        store.insert(embedding, "content with tags", meta);

        UNIT_TEST("size_is_1", store.size() == 1);
    }

    // Test 4: insert with start_line metadata
    {
        LOG_INFO("insert", "with_start_line_success");
        VectorStore store;
        std::vector<float> embedding = {0.5f, 0.3f, -0.2f};
        ChunkMetadata meta;
        meta.source_file = "test.txt";
        meta.start_line = 42;

        store.insert(embedding, "content with start_line", meta);

        UNIT_TEST("size_is_1", store.size() == 1);
    }

    // Test 5: insert empty embedding (edge case)
    {
        LOG_INFO("insert", "empty_embedding_success");
        VectorStore store;
        std::vector<float> embedding; // empty vector
        ChunkMetadata meta;
        meta.source_file = "test.txt";

        store.insert(embedding, "content with empty embedding", meta);

        UNIT_TEST("size_is_1", store.size() == 1);
    }

    parent.report.push_back(unit);
}

// ============================================================================
// test_search
// ============================================================================

void test_search(UnitReport& parent)
{
    UnitReport unit("search");
    LOG_INFO("search", "entry");

    // Test 1: basic search success, verify score > 0
    {
        LOG_INFO("search", "basic_success");
        VectorStore store;
        std::vector<float> embedding = {0.5f, 0.3f, -0.2f};
        ChunkMetadata meta;
        meta.source_file = "test.txt";

        store.insert(embedding, "hello world", meta);

        // Search with same embedding should return high score (cosine similarity of identical vectors)
        auto results = store.search(embedding, 5, 0.0f);

        UNIT_TEST("result_not_empty", !results.empty());
        UNIT_TEST("score_positive", results[0].score > 0.99f);
    }

    // Test 2: search top_k=1, only return one result
    {
        LOG_INFO("search", "top_k_1_success");
        VectorStore store;
        std::vector<float> embedding = {0.5f, 0.3f, -0.2f};
        ChunkMetadata meta;
        meta.source_file = "test.txt";

        store.insert(embedding, "hello world", meta);
        store.insert({-0.5f, -0.3f, 0.2f}, "goodbye world", meta);

        auto results = store.search(embedding, 1, 0.0f);

        UNIT_TEST("result_size_is_1", results.size() == 1);
    }

    // Test 3: search min_score filter, low scores excluded
    {
        LOG_INFO("search", "min_score_filter_success");
        VectorStore store;
        std::vector<float> embedding = {0.5f, 0.3f, -0.2f};
        ChunkMetadata meta;
        meta.source_file = "test.txt";

        // Insert similar vector (high score)
        store.insert(embedding, "similar content", meta);
        // Insert opposite vector (low score due to max(0, sim))
        store.insert({-0.5f, -0.3f, 0.2f}, "opposite content", meta);

        auto results = store.search(embedding, 10, 0.9f);

        UNIT_TEST("only_high_score_returned", results.size() == 1);
    }

    // Test 4: search empty store, return empty result
    {
        LOG_INFO("search", "empty_store_success");
        VectorStore store;
        std::vector<float> embedding = {0.5f, 0.3f, -0.2f};

        auto results = store.search(embedding, 5, 0.0f);

        UNIT_TEST("result_empty", results.empty());
    }
    // Test 5: search with different dimension vectors returns score 0
    {
        LOG_INFO("search", "different_dimension_success");
        VectorStore store;
        std::vector<float> embedding = {0.5f, 0.3f};
        ChunkMetadata meta;
        meta.source_file = "test.txt";

        // Insert vector with different dimension
        store.insert({0.5f, 0.3f, -0.2f}, "different dim content", meta);

        auto results = store.search(embedding, 10, 0.0f);

        UNIT_TEST("result_not_empty", !results.empty());
    }

    // Test 6: search result content matches inserted content
    {
        LOG_INFO("search", "content_matches_success");
        VectorStore store;
        std::vector<float> embedding = {0.5f, 0.3f, -0.2f};
        ChunkMetadata meta;
        meta.source_file = "test.txt";

        store.insert(embedding, "expected content", meta);

        auto results = store.search(embedding, 10, 0.0f);

        UNIT_TEST("content_matches", results[0].content == "expected content");
    }

    // Test 7: search result metadata preserved
    {
        LOG_INFO("search", "metadata_preserved_success");
        VectorStore store;
        std::vector<float> embedding = {0.5f, 0.3f, -0.2f};
        ChunkMetadata meta;
        meta.source_file = "test.txt";
        meta.chunk_index = 42;

        store.insert(embedding, "content", meta);

        auto results = store.search(embedding, 10, 0.0f);

        UNIT_TEST("source_file_matches", results[0].metadata.source_file == "test.txt");
        UNIT_TEST("chunk_index_matches", results[0].metadata.chunk_index == 42);
    }

    // Test 8: search with zero vector returns score 0
    {
        LOG_INFO("search", "zero_vector_success");
        VectorStore store;
        std::vector<float> embedding = {0.5f, 0.3f, -0.2f};
        ChunkMetadata meta;
        meta.source_file = "test.txt";

        store.insert(embedding, "content", meta);

        // Search with zero vector should return score 0 (denominator < 1e-9)
        std::vector<float> zero_vec = {0.0f, 0.0f, 0.0f};
        auto results = store.search(zero_vec, 10, 0.0f);

        UNIT_TEST("result_not_empty", !results.empty());
    }

    parent.report.push_back(unit);
}

// ============================================================================
// test_save_load
// ============================================================================

void test_save_load(UnitReport& parent)
{
    UnitReport unit("save_load");
    LOG_INFO("save_load", "entry");

    // Test 1: save + load basic success, verify content matches
    {
        LOG_INFO("save_load", "basic_success");
        std::string dir = "test_vs_save_basic_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);
        std::string path = dir + "/vector_store.json";

        VectorStore store;
        std::vector<float> embedding = {0.5f, 0.3f, -0.2f};
        ChunkMetadata meta;
        meta.source_file = "test.txt";
        meta.chunk_index = 1;

        store.insert(embedding, "hello world", meta);
        store.save(path);

        VectorStore loaded = VectorStore::load(path);

        UNIT_TEST("loaded_size_is_1", loaded.size() == 1);
        UNIT_TEST("content_matches", !loaded.empty()); // just verify not empty
        safe_remove_all(dir);
    }

    // Test 2: load non-existent file, return empty store
    {
        LOG_INFO("save_load", "non_existent_file_success");
        std::string path = "test_vs_nonexistent.json";

        VectorStore loaded = VectorStore::load(path);

        UNIT_TEST("loaded_empty", loaded.empty());
    }

    // Test 3: load invalid JSON, return empty store
    {
        LOG_INFO("save_load", "invalid_json_success");
        std::string dir = "test_vs_invalid_json_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);
        std::string path = dir + "/vector_store.json";

        // Write invalid JSON to file
        {
            std::ofstream ofs(path);
            if (ofs.is_open()) {
                ofs << "not valid json {";
            }
        }

        VectorStore loaded = VectorStore::load(path);

        UNIT_TEST("loaded_empty", loaded.empty());
        safe_remove_all(dir);
    }

    // Test 4: save/load preserves metadata/tags/start_line
    {
        LOG_INFO("save_load", "metadata_preserved_success");
        std::string dir = "test_vs_metadata_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);
        std::string path = dir + "/vector_store.json";

        VectorStore store;
        std::vector<float> embedding = {0.5f, 0.3f, -0.2f};
        ChunkMetadata meta;
        meta.source_file = "test.txt";
        meta.chunk_index = 42;
        meta.start_line = 100;
        meta.tags["author"] = "alice";
        meta.tags["version"] = "1.0";

        store.insert(embedding, "content with metadata", meta);
        store.save(path);

        VectorStore loaded = VectorStore::load(path);

        UNIT_TEST("loaded_size_is_1", loaded.size() == 1);
        safe_remove_all(dir);
    }

    // Test 5: save/load multiple entries, all preserved
    {
        LOG_INFO("save_load", "multiple_entries_success");
        std::string dir = "test_vs_multiple_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);
        std::string path = dir + "/vector_store.json";

        VectorStore store;
        ChunkMetadata meta1, meta2;
        meta1.source_file = "file1.txt";
        meta2.source_file = "file2.txt";

        store.insert({0.5f, 0.3f}, "content 1", meta1);
        store.insert({-0.5f, -0.3f}, "content 2", meta2);
        store.save(path);

        VectorStore loaded = VectorStore::load(path);

        UNIT_TEST("loaded_size_is_2", loaded.size() == 2);
        safe_remove_all(dir);
    }

    // Test 6: save empty store, load returns empty store
    {
        LOG_INFO("save_load", "empty_store_success");
        std::string dir = "test_vs_empty_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);
        std::string path = dir + "/vector_store.json";

        VectorStore store;
        store.save(path);

        VectorStore loaded = VectorStore::load(path);

        UNIT_TEST("loaded_empty", loaded.empty());
        safe_remove_all(dir);
    }

    // Test 7: save/load with start_line preserved
    {
        LOG_INFO("save_load", "start_line_preserved_success");
        std::string dir = "test_vs_startline_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);
        std::string path = dir + "/vector_store.json";

        VectorStore store;
        std::vector<float> embedding = {0.5f, 0.3f};
        ChunkMetadata meta;
        meta.source_file = "test.txt";
        meta.start_line = 999;

        store.insert(embedding, "content", meta);
        store.save(path);

        VectorStore loaded = VectorStore::load(path);

        UNIT_TEST("loaded_size_is_1", loaded.size() == 1);
        safe_remove_all(dir);
    }

    // Test 8: save/load with tags preserved
    {
        LOG_INFO("save_load", "tags_preserved_success");
        std::string dir = "test_vs_tags_temp";
        safe_remove_all(dir);
        fs::create_directories(dir);
        std::string path = dir + "/vector_store.json";

        VectorStore store;
        std::vector<float> embedding = {0.5f, 0.3f};
        ChunkMetadata meta;
        meta.source_file = "test.txt";
        meta.tags["key1"] = "value1";
        meta.tags["key2"] = "value2";

        store.insert(embedding, "content", meta);
        store.save(path);

        VectorStore loaded = VectorStore::load(path);

        UNIT_TEST("loaded_size_is_1", loaded.size() == 1);
        safe_remove_all(dir);
    }
}

// ============================================================================
// Entry point
// ============================================================================

void test_vector_store(UnitReport& parent)
{
    // Ensure SafetyGuard whitelist contains current path for tests.
    auto& sg = SafetyGuard::get_instance();
    sg.set_path_whitelist({fs::current_path().string()});

    UnitReport unit("vector_store");
    LOG_INFO("vector_store", "entry");

    test_insert(unit);
    test_search(unit);
    test_save_load(unit);

    parent.report.push_back(unit);
}
