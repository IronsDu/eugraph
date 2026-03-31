cmake_minimum_required(VERSION 3.25)

project(eugraph CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo")
endif()

# Options
option(EUGRAPH_BUILD_TESTS "Build unit tests" ON)

# Dependencies
find_package(spdlog CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(lz4 CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(zstd CONFIG REQUIRED)

# WiredTiger: use static library to avoid DT_INIT bug with GCC 15
find_path(WIREDTIGER_INCLUDE_DIR NAMES wiredtiger.h)
find_library(WIREDTIGER_LIBRARY NAMES wiredtiger PATH_SUFFIXES lib)
if(NOT WIREDTIGER_INCLUDE_DIR OR NOT WIREDTIGER_LIBRARY)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WIREDTIGER REQUIRED wiredtiger)
    set(WIREDTIGER_INCLUDE_DIR ${WIREDTIGER_INCLUDE_DIRS})
    set(WIREDTIGER_LIBRARY ${WIREDTIGER_LIBRARIES})
endif()

# Library target
add_library(eugraph_lib STATIC
    src/storage/kv/wiredtiger_store.cpp
    src/storage/kv/key_codec.cpp
    src/storage/kv/value_codec.cpp
)

target_include_directories(eugraph_lib PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
    ${WIREDTIGER_INCLUDE_DIR}
)

target_link_libraries(eugraph_lib PUBLIC
    ${WIREDTIGER_LIBRARY}
    lz4::lz4
    ZLIB::ZLIB
    zstd::libzstd
    spdlog::spdlog
    Threads::Threads
    dl
)

# Test target
if(EUGRAPH_BUILD_TESTS)
    enable_testing()

    add_executable(eugraph_tests
        tests/test_key_codec.cpp
        tests/test_value_codec.cpp
        tests/test_wiredtiger_store.cpp
    )

    target_include_directories(eugraph_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        ${WIREDTIGER_INCLUDE_DIR}
    )

    target_link_libraries(eugraph_tests PRIVATE
        eugraph_lib
        GTest::gtest
        GTest::gtest_main
    )

    include(GoogleTest)
    gtest_add_tests(TARGET eugraph_tests)
endif()
