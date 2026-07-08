# Fetch Catch2 via CMake's built-in downloader
include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.6.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

# Test source files
set(TEST_SOURCES
    test_config.cpp
    test_memory.cpp
    test_tool_registry.cpp
    test_document_chunker.cpp
    test_vector_store.cpp
    test_tfidf_embedding.cpp
    test_command_dispatcher.cpp
    test_safety_guard.cpp
    test_skill_loader.cpp
    test_rag_manager.cpp
    test_long_term_memory.cpp
    # Tool tests
    test_code_search_tool.cpp
    test_file_tools.cpp
    test_fs_tools.cpp
    test_memory_tools.cpp
    test_skill_tools.cpp
)

# Link the actual implementation source files so tests can call real code
set(TEST_IMPL_SOURCES
    ../src/config.cpp
    ../src/memory.cpp
    ../src/tool.cpp
    ../src/document_chunker.cpp
    ../src/vector_store.cpp
    ../src/embedding_provider.cpp
    ../src/command_dispatcher.cpp
    ../src/safety_guard.cpp
    ../src/skill_system.cpp
    ../src/rag_manager.cpp
    ../src/long_term_memory.cpp
    ../src/llm_client.cpp
    ../src/token_counter.cpp
    ../src/remote_log.cpp
    # File utilities (used by file tools)
    ../src/file_utils.cpp
    # Tool implementations
    ../tools/code_search_tool.cpp
    ../tools/file_tool.cpp
    ../tools/fs_tool.cpp
    ../tools/memory_tool.cpp
    ../tools/skill_tool.cpp
)

add_executable(tests ${TEST_SOURCES} ${TEST_IMPL_SOURCES})

target_include_directories(tests PRIVATE ../include)

# Link Catch2
target_link_libraries(tests PRIVATE Catch2::Catch2WithMain)

# Enable OpenSSL support for HTTPS (same as main target)
if(OPENSSL_FOUND)
    add_compile_definitions(CPPHTTLIB_OPENSSL_SUPPORT)

    if(TARGET OpenSSL::SSL AND TARGET OpenSSL::Crypto)
        target_link_libraries(tests PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    elseif(MSVC AND EXISTS "${CMAKE_BINARY_DIR}/packages/openssl-native.3.5.5/lib/win-x64/native")
        set(OPENSSL_STATIC_DIR "${CMAKE_BINARY_DIR}/packages/openssl-native.3.5.5/lib/win-x64/native")
        target_include_directories(tests PRIVATE "${CMAKE_BINARY_DIR}/packages/openssl-native.3.5.5/include")
        target_link_libraries(tests PRIVATE
            "${OPENSSL_STATIC_DIR}/libssl_static_v143.lib"
            "${OPENSSL_STATIC_DIR}/libcrypto_static_v143.lib"
            ws2_32 advapi32 crypt32 user32 gdi32
        )
    endif()
endif()

# Enable CTest integration
enable_testing()
