cmake_minimum_required(VERSION 3.16)
project(zlagent VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OpenSSL for HTTPS support (from NuGet package in build/packages/)
if(MSVC)
    set(OPENSSL_ROOT_DIR "${CMAKE_BINARY_DIR}/packages/openssl-native.3.5.5" CACHE PATH "OpenSSL root")
endif()

# Core source files
set(CORE_SOURCES
    src/config.cpp
    src/safety_guard.cpp
    src/system_prompt.cpp
    src/language_detector.cpp
    src/skill_system.cpp
    src/llm_client.cpp
    src/tool.cpp
    src/memory.cpp
    src/agent.cpp
    src/plugin_loader.cpp
    src/local_tools.cpp
    src/task_planner.cpp
    src/self_reflector.cpp
    src/multi_agent.cpp
    # RAG system
    src/embedding_provider.cpp
    src/vector_store.cpp
    src/document_chunker.cpp
    src/rag_manager.cpp
)

# Tool source files
set(TOOL_SOURCES
    tools/file_tool.cpp
    tools/terminal_tool.cpp
    tools/code_search_tool.cpp
    tools/fs_tool.cpp
    tools/skill_tool.cpp
    tools/rag_tool.cpp
)

# Include directories
include_directories(include)

# OpenSSL configuration for HTTPS support
if(MSVC AND EXISTS "${CMAKE_BINARY_DIR}/packages/openssl-native.3.5.5/include")
    set(OPENSSL_INCLUDE_DIR "${CMAKE_BINARY_DIR}/packages/openssl-native.3.5.5/include" CACHE PATH "OpenSSL include dir")
    set(OPENSSL_CRYPTO_LIBRARY "${CMAKE_BINARY_DIR}/packages/openssl-native.3.5.5/lib/win-x64/native/libcrypto.lib" CACHE FILEPATH "OpenSSL crypto lib")
    set(OPENSSL_SSL_LIBRARY "${CMAKE_BINARY_DIR}/packages/openssl-native.3.5.5/lib/win-x64/native/libssl.lib" CACHE FILEPATH "OpenSSL ssl lib")
endif()

# Main executable
add_executable(zlagent
    ${CORE_SOURCES}
    ${TOOL_SOURCES}
    src/main.cpp
)

# Enable OpenSSL support for HTTPS
find_package(OpenSSL QUIET)
if(OPENSSL_FOUND)
    message(STATUS "OpenSSL found: ${OPENSSL_INCLUDE_DIR}")
else()
    # Fallback: use NuGet package paths directly
    if(MSVC AND EXISTS "${CMAKE_BINARY_DIR}/packages/openssl-native.3.5.5/include")
        set(OPENSSL_FOUND TRUE)
        message(STATUS "Using OpenSSL from NuGet package (build/packages/openssl-native.3.5.5)")
    endif()
endif()

if(OPENSSL_FOUND)
    add_compile_definitions(CPPHTTPLIB_OPENSSL_SUPPORT)

    # Link OpenSSL libraries
    if(TARGET OpenSSL::SSL AND TARGET OpenSSL::Crypto)
        target_link_libraries(zlagent PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    elseif(MSVC AND EXISTS "${CMAKE_BINARY_DIR}/packages/openssl-native.3.5.5/lib/win-x64/native")
        # Fallback: link directly from NuGet package
        target_include_directories(zlagent PRIVATE "${CMAKE_BINARY_DIR}/packages/openssl-native.3.5.5/include")
        target_link_libraries(zlagent PRIVATE
            "${CMAKE_BINARY_DIR}/packages/openssl-native.3.5.5/lib/win-x64/native/libssl.lib"
            "${CMAKE_BINARY_DIR}/packages/openssl-native.3.5.5/lib/win-x64/native/libcrypto.lib"
        )
    endif()
endif()

# Platform-specific settings
if(MSVC)
    add_compile_options(/utf-8 /wd4566)
elseif(UNIX AND NOT APPLE)
    # Linux: link dl for dlopen/dlsym
    target_link_libraries(zlagent PRIVATE dl)
elseif(APPLE)
    # macOS: dlopen is in libSystem, no extra linking needed
endif()

# Install target
install(TARGETS zlagent DESTINATION bin)
