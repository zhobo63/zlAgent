cmake_minimum_required(VERSION 3.16)
project(tool_clang_format LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(clang_format_plugin SHARED tool_clang_format.cpp)

target_compile_definitions(clang_format_plugin PRIVATE TOOL_SDK_EXPORTS)
target_include_directories(clang_format_plugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)

if(WIN32)
    set_target_properties(clang_format_plugin PROPERTIES
        PREFIX "tool_"
        OUTPUT_NAME "clang_format"
    )
endif()

# Copy the DLL to plugins/ directory after build
add_custom_command(TARGET clang_format_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_SOURCE_DIR}/../../plugins
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:clang_format_plugin>
        ${PROJECT_SOURCE_DIR}/../../plugins/
    COMMENT "Copying tool_clang_format.dll to plugins/"
)
