cmake_minimum_required(VERSION 3.15)
project(unit_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Shared core library (same sources as main project)
add_library(zlagent_core STATIC
    ../tools/file_tool.cpp
    ../tools/terminal_tool.cpp
    ../tools/code_search_tool.cpp
    ../tools/fs_tool.cpp
    ../tools/skill_tool.cpp
    ../tools/rag_tool.cpp
    ../tools/memory_tool.cpp
    ../tools/overview_tool.cpp

    ../src/config.cpp
    ../src/safety_guard.cpp
    ../src/system_prompt.cpp
    ../src/language_detector.cpp
    ../src/key_watcher.cpp
    ../src/skill_system.cpp
    ../src/llm_client.cpp
    ../src/tool.cpp
    ../src/memory.cpp
    ../src/token_counter.cpp
    ../src/agent.cpp
    ../src/plugin_loader.cpp
    ../src/local_tools.cpp
    ../src/task_planner.cpp
    ../src/self_reflector.cpp
    ../src/multi_agent.cpp
    ../src/embedding_provider.cpp
    ../src/vector_store.cpp
    ../src/document_chunker.cpp
    ../src/rag_manager.cpp
    ../src/long_term_memory.cpp
    ../src/tui.cpp
    ../src/command_dispatcher.cpp
    ../src/command_handlers.cpp
    ../src/terminal_command_detector.cpp
    ../src/file_utils.cpp
    ../src/remote_log.cpp
    ../src/user_reply.cpp
    ../src/reply_mode_command.cpp
    ../src/event.cpp
    ../src/telegram_client.cpp
    ../src/project_summary/summary_tool.cpp
)

target_include_directories(zlagent_core PRIVATE ../include)
target_precompile_headers(zlagent_core PRIVATE ../include/pch.h)

if(MSVC)
    target_compile_options(zlagent_core PRIVATE /utf-8 /wd4566)
endif()

# Unit test executable
add_executable(unit_test
    main.cpp
    test_file_tools.cpp
)

target_include_directories(unit_test PRIVATE ../include)
target_precompile_headers(unit_test PRIVATE ../include/pch.h)

target_link_libraries(unit_test PRIVATE zlagent_core)

# Link required libraries for iostream / string / vector on Windows
if(WIN32)
    target_link_libraries(zlagent_core PRIVATE ${CMAKE_DL_LIBS} ws2_32)
endif()

# Enable OpenSSL support for HTTPS (same as main target)
# Note: disabled for unit_test - tests don't need HTTPS
