# Unit test executable
add_executable(unit_test
    main.cpp
    test_code_search_tool.cpp
    test_file_tools.cpp
)

target_include_directories(unit_test PRIVATE ../include)
target_precompile_headers(unit_test PRIVATE ../include/pch.h)

# Link against the shared core library from the parent project
target_link_libraries(unit_test PRIVATE zlagent_core)

if(MSVC)
    target_compile_options(unit_test PRIVATE /utf-8 /wd4566)
endif()
