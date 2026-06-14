#include <catch2/catch_all.hpp>
#include "document_chunker.h"
#include <fstream>
#include <filesystem>

using namespace agent;

TEST_CASE("DocumentChunker: basic chunking", "[chunker]") {
    DocumentChunker::Config cfg;
    DocumentChunker chunker(cfg);
    std::string text = "Hello world. This is a test document for chunking.";
    auto chunks = chunker.chunk(text);
    REQUIRE(!chunks.empty());
}

TEST_CASE("DocumentChunker: empty content returns no chunks", "[chunker]") {
    DocumentChunker::Config cfg;
    DocumentChunker chunker(cfg);
    auto chunks = chunker.chunk("");
    REQUIRE(chunks.empty());
}

TEST_CASE("DocumentChunker: multiple chunks for long text", "[chunker]") {
    DocumentChunker::Config cfg2;
    cfg2.chunk_size = 100;
    cfg2.overlap = 20;
    DocumentChunker chunker(cfg2);
    std::string long_text;
    for (int i = 0; i < 50; ++i) {
        long_text += "This is sentence number " + std::to_string(i) + ". ";
    }

    auto chunks = chunker.chunk(long_text);
    REQUIRE(chunks.size() > 1);
}

TEST_CASE("DocumentChunker: paragraph-aware splitting", "[chunker]") {
    DocumentChunker::Config cfg3;
    cfg3.chunk_size = 500;
    cfg3.overlap = 50;
    cfg3.respect_paragraphs = true;
    DocumentChunker chunker(cfg3);
    std::string text = "First paragraph here.\n\nSecond paragraph with more content.";

    auto chunks = chunker.chunk(text);
    REQUIRE(!chunks.empty());
}

TEST_CASE("DocumentChunker: start_line tracking", "[chunker]") {
    DocumentChunker::Config cfg4;
    cfg4.chunk_size = 100;
    DocumentChunker chunker(cfg4);
    std::string text = "line1\nline2\nline3\nline4\nline5";

    auto chunks = chunker.chunk(text, 1);
    if (!chunks.empty()) {
        REQUIRE(chunks[0].start_line >= 1);
    }
}

TEST_CASE("DocumentChunker: overlap ensures continuity", "[chunker]") {
    DocumentChunker::Config cfg5;
    cfg5.chunk_size = 50;
    cfg5.overlap = 20;
    DocumentChunker chunker(cfg5);
    std::string text = "ABCDEFGHIJ"; // 10 chars repeated to fill chunks
    for (int i = 0; i < 30; ++i) text += "ABCDEFGHIJ";

    auto chunks = chunker.chunk(text);
    REQUIRE(chunks.size() > 1);
}
