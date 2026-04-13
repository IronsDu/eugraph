cmake_minimum_required(VERSION 3.25)

project(eugraph C CXX)
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
find_package(antlr4-runtime CONFIG REQUIRED)

# WiredTiger: build as independent CMake project via ExternalProject.
# This avoids CMAKE_SOURCE_DIR conflicts from WiredTiger's internal scripts.
include(ExternalProject)
include(GNUInstallDirs)
set(WT_INSTALL_DIR "${CMAKE_BINARY_DIR}/wiredtiger-install")
set(WT_LIB "${WT_INSTALL_DIR}/${CMAKE_INSTALL_LIBDIR}/libwiredtiger.a")

ExternalProject_Add(wiredtiger_ext
    SOURCE_DIR "${PROJECT_SOURCE_DIR}/third_party/wiredtiger"
    INSTALL_DIR "${WT_INSTALL_DIR}"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        -DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_C_FLAGS=-Wno-error=array-bounds\ -Wno-array-bounds
        -DCMAKE_CXX_FLAGS=-Wno-error=array-bounds\ -Wno-array-bounds
        -DENABLE_STATIC=ON
        -DENABLE_SHARED=OFF
    BUILD_BYPRODUCTS
        "${WT_LIB}"
)

# Ensure include dir exists at configure time for CMake validation
file(MAKE_DIRECTORY "${WT_INSTALL_DIR}/include")

# Library target (WiredTiger multi-table backend)
add_library(eugraph_lib STATIC
    src/storage/kv/key_codec.cpp
    src/storage/kv/value_codec.cpp
    src/storage/graph_store_impl.cpp
)

target_include_directories(eugraph_lib PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
    "${WT_INSTALL_DIR}/include"
)

target_link_libraries(eugraph_lib PUBLIC
    "${WT_LIB}"
    lz4::lz4
    ZLIB::ZLIB
    zstd::libzstd
    spdlog::spdlog
    Threads::Threads
)

add_dependencies(eugraph_lib wiredtiger_ext)

# ==================== Compute Service (Cypher Parser + Query Engine) ====================

# Pre-generated ANTLR4 C++ sources (committed to repo, no Java build dependency)
set(ANTLR_GENERATED_DIR "${PROJECT_SOURCE_DIR}/src/compute_service/parser/generated")
set(ANTLR_GENERATED_SOURCES
    "${ANTLR_GENERATED_DIR}/CypherLexer.cpp"
    "${ANTLR_GENERATED_DIR}/CypherParser.cpp"
    "${ANTLR_GENERATED_DIR}/CypherParserBaseVisitor.cpp"
    "${ANTLR_GENERATED_DIR}/CypherParserVisitor.cpp"
)

add_library(eugraph_compute STATIC
    ${ANTLR_GENERATED_SOURCES}
    src/compute_service/parser/cypher_parser.cpp
)

target_include_directories(eugraph_compute PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
    "${ANTLR_GENERATED_DIR}"
)

target_link_libraries(eugraph_compute PUBLIC
    antlr4_static
    spdlog::spdlog
)

# Test targets
if(EUGRAPH_BUILD_TESTS)
    enable_testing()

    # Main test suite (KeyCodec, ValueCodec, GraphStore with WT backend)
    add_executable(eugraph_tests
        tests/test_key_codec.cpp
        tests/test_value_codec.cpp
        tests/test_graph_store_wt.cpp
    )

    target_include_directories(eugraph_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${WT_INSTALL_DIR}/include"
    )

    target_link_libraries(eugraph_tests PRIVATE
        eugraph_lib
        GTest::gtest
        GTest::gtest_main
        dl
    )

    add_dependencies(eugraph_tests wiredtiger_ext)

    include(GoogleTest)
    gtest_add_tests(TARGET eugraph_tests)

    # WiredTiger raw API tests
    add_executable(wt_tests
        tests/test_wiredtiger.cpp
    )

    target_include_directories(wt_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${WT_INSTALL_DIR}/include"
    )

    target_link_libraries(wt_tests PRIVATE
        "${WT_LIB}"
        GTest::gtest
        GTest::gtest_main
        Threads::Threads
        dl
    )

    add_dependencies(wt_tests wiredtiger_ext)
    gtest_add_tests(TARGET wt_tests)

    # Cypher parser tests
    add_executable(cypher_parser_tests
        tests/test_cypher_parser.cpp
    )

    target_include_directories(cypher_parser_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${ANTLR_GENERATED_DIR}"
    )

    target_link_libraries(cypher_parser_tests PRIVATE
        eugraph_compute
        GTest::gtest
        GTest::gtest_main
    )

    gtest_add_tests(TARGET cypher_parser_tests)
endif()
