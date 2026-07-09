# Unit test executable
add_executable(unit_test
    main.cpp
    test_code_search_tool.cpp
    test_file_tools.cpp
    test_overview_tool.cpp
)

target_include_directories(unit_test PRIVATE ../include)
target_precompile_headers(unit_test PRIVATE ../include/pch.h)

# Link against the shared core library from the parent project
target_link_libraries(unit_test PRIVATE zlagent_core)

if(MSVC)
    target_compile_options(unit_test PRIVATE /utf-8 /wd4566)
endif()

# OpenSSL support (same as main target)
if(OPENSSL_FOUND)
    add_compile_definitions(CPPHTTLIB_OPENSSL_SUPPORT)

    if(TARGET OpenSSL::SSL AND TARGET OpenSSL::Crypto)
        target_link_libraries(unit_test PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    elseif(MSVC AND EXISTS "${CMAKE_BINARY_DIR}/packages/openssl-native.3.5.5/lib/win-x64/native")
        set(OPENSSL_STATIC_DIR "${CMAKE_BINARY_DIR}/packages/openssl-native.3.5.5/lib/win-x64/native")
        target_include_directories(unit_test PRIVATE "${CMAKE_BINARY_DIR}/packages/openssl-native.3.5.5/include")
        target_link_libraries(unit_test PRIVATE
            "${OPENSSL_STATIC_DIR}/libssl_static_v143.lib"
            "${OPENSSL_STATIC_DIR}/libcrypto_static_v143.lib"
            ws2_32 advapi32 crypt32 user32 gdi32
        )
    endif()
endif()
