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
find_package(RocksDB CONFIG REQUIRED)

# Library target
add_library(eugraph_lib STATIC
    src/storage/kv/rocksdb_store.cpp
    src/storage/kv/key_codec.cpp
    src/storage/kv/value_codec.cpp
    src/storage/graph_store_impl.cpp
)

target_include_directories(eugraph_lib PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(eugraph_lib PUBLIC
    RocksDB::rocksdb
    lz4::lz4
    ZLIB::ZLIB
    zstd::libzstd
    spdlog::spdlog
    Threads::Threads
)

# Test target
if(EUGRAPH_BUILD_TESTS)
    enable_testing()

    add_executable(eugraph_tests
        tests/test_key_codec.cpp
        tests/test_value_codec.cpp
        tests/test_graph_store.cpp
    )

    target_include_directories(eugraph_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
    )

    target_link_libraries(eugraph_tests PRIVATE
        eugraph_lib
        GTest::gtest
        GTest::gtest_main
    )

    include(GoogleTest)
    gtest_add_tests(TARGET eugraph_tests)
endif()
