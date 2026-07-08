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
    src/key_watcher.cpp
    src/skill_system.cpp
    src/llm_client.cpp
    src/tool.cpp
    src/memory.cpp
    src/token_counter.cpp
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
    # Long-term memory
    src/long_term_memory.cpp
    # TUI (terminal UI)
    src/tui.cpp
    # CLI command system
    src/command_dispatcher.cpp
    src/command_handlers.cpp
    # Terminal command direct execution
    src/terminal_command_detector.cpp
    # File utilities
    src/file_utils.cpp
    # Remote logging
    src/remote_log.cpp
    # User reply (intervention during reasoning loop)
    src/user_reply.cpp
    src/reply_mode_command.cpp

    # Event broker (message broadcast)
    src/event.cpp
    # Telegram Bot client
    src/telegram_client.cpp
    # Project summary tool
    src/project_summary/summary_tool.cpp
)

# Tool source files
set(TOOL_SOURCES
    tools/file_tool.cpp
    tools/terminal_tool.cpp
    tools/code_search_tool.cpp
    tools/fs_tool.cpp
    tools/skill_tool.cpp
    tools/rag_tool.cpp
    tools/memory_tool.cpp
    tools/overview_tool.cpp
)

# Include directories
include_directories(include)

# OpenSSL configuration for HTTPS support
if(MSVC AND EXISTS "${CMAKE_BINARY_DIR}/packages/openssl-native.3.5.5/include")
    set(OPENSSL_INCLUDE_DIR "${CMAKE_BINARY_DIR}/packages/openssl-native.3.5.5/include" CACHE PATH "OpenSSL include dir")
    set(OPENSSL_CRYPTO_LIBRARY "${CMAKE_BINARY_DIR}/packages/openssl-native.3.5.5/lib/win-x64/native/libcrypto.lib" CACHE FILEPATH "OpenSSL crypto lib")
    set(OPENSSL_SSL_LIBRARY "${CMAKE_BINARY_DIR}/packages/openssl-native.3.5.5/lib/win-x64/native/libssl.lib" CACHE FILEPATH "OpenSSL ssl lib")
endif()

# Shared core static library
add_library(zlagent_core STATIC
    ${CORE_SOURCES}
    ${TOOL_SOURCES}
)
target_include_directories(zlagent_core PRIVATE include)
target_precompile_headers(zlagent_core PRIVATE include/pch.h)
if(MSVC)
    target_compile_options(zlagent_core PRIVATE /utf-8 /wd4566)
endif()

# Main executable (links against shared core library)
add_executable(zlagent
    src/main.cpp
)
target_link_libraries(zlagent PRIVATE zlagent_core)

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
    add_compile_definitions(CPPHTTLIB_OPENSSL_SUPPORT)

    # Link OpenSSL libraries (static on MSVC, dynamic elsewhere)
    if(TARGET OpenSSL::SSL AND TARGET OpenSSL::Crypto)
        target_link_libraries(zlagent_core PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    elseif(MSVC AND EXISTS "${CMAKE_BINARY_DIR}/packages/openssl-native.3.5.5/lib/win-x64/native")
        # Static link OpenSSL from NuGet package
        set(OPENSSL_STATIC_DIR "${CMAKE_BINARY_DIR}/packages/openssl-native.3.5.5/lib/win-x64/native")
        target_include_directories(zlagent_core PRIVATE "${CMAKE_BINARY_DIR}/packages/openssl-native.3.5.5/include")
        target_link_libraries(zlagent_core PRIVATE
            "${OPENSSL_STATIC_DIR}/libssl_static_v143.lib"
            "${OPENSSL_STATIC_DIR}/libcrypto_static_v143.lib"
            # Windows system libraries required by static OpenSSL
            ws2_32 advapi32 crypt32 user32 gdi32
        )
    endif()
endif()

# Platform-specific settings
if(MSVC)
    add_compile_options(/utf-8 /wd4566)
    target_link_libraries(zlagent_core PRIVATE ws2_32)
elseif(UNIX AND NOT APPLE)
    # Linux: link dl for dlopen/dlsym, pthread for threading
    target_link_libraries(zlagent_core PRIVATE dl pthread)
elseif(APPLE)
    # macOS: dlopen is in libSystem, no extra linking needed
endif()





# Unit tests (enable with -DBUILD_TESTING=ON)
option(BUILD_TESTING "Build unit tests" OFF)
if(BUILD_TESTING)
    add_subdirectory(unit_test)
endif()

# Install target
install(TARGETS zlagent DESTINATION bin)
