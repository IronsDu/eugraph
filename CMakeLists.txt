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
find_package(folly CONFIG REQUIRED)
find_package(FBThrift CONFIG REQUIRED)

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

# ==================== Metadata Service ====================

add_library(eugraph_metadata STATIC
    src/metadata_service/metadata_codec.cpp
    src/metadata_service/metadata_service.cpp
)

target_include_directories(eugraph_metadata PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(eugraph_metadata PUBLIC
    eugraph_lib
    Folly::folly
    spdlog::spdlog
)

# ==================== Query Engine (Logical Plan + Physical Plan + Executor) ====================

add_library(eugraph_query_engine STATIC
    src/compute_service/logical_plan/logical_plan_builder.cpp
    src/compute_service/executor/async_graph_store.cpp
    src/compute_service/executor/expression_evaluator.cpp
    src/compute_service/physical_plan/physical_operator.cpp
    src/compute_service/physical_plan/physical_planner.cpp
    src/compute_service/executor/query_executor.cpp
)

target_include_directories(eugraph_query_engine PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
    "${ANTLR_GENERATED_DIR}"
)

target_link_libraries(eugraph_query_engine PUBLIC
    eugraph_compute
    eugraph_lib
    Folly::folly
    Folly::folly_deps
    spdlog::spdlog
)

# ==================== Thrift Generated Code ====================

set(GEN_CPP2_DIR "${PROJECT_SOURCE_DIR}/src/gen-cpp2")

add_library(eugraph_thrift STATIC
    ${GEN_CPP2_DIR}/eugraph_types.cpp
    ${GEN_CPP2_DIR}/eugraph_data.cpp
    ${GEN_CPP2_DIR}/eugraph_constants.cpp
    ${GEN_CPP2_DIR}/eugraph_metadata.cpp
    ${GEN_CPP2_DIR}/eugraph_types_binary.cpp
    ${GEN_CPP2_DIR}/eugraph_types_compact.cpp
    ${GEN_CPP2_DIR}/EuGraphService.cpp
    ${GEN_CPP2_DIR}/EuGraphServiceAsyncClient.cpp
    ${GEN_CPP2_DIR}/EuGraphService_processmap_binary.cpp
    ${GEN_CPP2_DIR}/EuGraphService_processmap_compact.cpp
)

target_include_directories(eugraph_thrift PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(eugraph_thrift PUBLIC
    FBThrift::thriftcpp2
    FBThrift::async
    Folly::folly
)

# ==================== Shell (REPL) ====================

add_library(eugraph_shell_lib STATIC
    src/shell/shell_repl.cpp
    src/shell/rpc_client.cpp
    src/server/eugraph_handler.cpp
)

target_include_directories(eugraph_shell_lib PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(eugraph_shell_lib PUBLIC
    eugraph_query_engine
    eugraph_metadata
    eugraph_thrift
    Folly::folly
    Folly::folly_deps
    spdlog::spdlog
)

# ==================== Server + Shell executables ====================

add_executable(eugraph-server
    src/server/eugraph_server_main.cpp
)

target_include_directories(eugraph-server PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${WT_INSTALL_DIR}/include"
)

target_link_libraries(eugraph-server PRIVATE
    eugraph_shell_lib
    eugraph_query_engine
    eugraph_metadata
    eugraph_thrift
    Folly::folly
    Folly::folly_deps
    spdlog::spdlog
    dl
)

add_dependencies(eugraph-server wiredtiger_ext)

add_executable(eugraph-shell
    src/shell/shell_main.cpp
)

target_include_directories(eugraph-shell PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(eugraph-shell PRIVATE
    eugraph_shell_lib
    dl
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

    # Logical plan tests
    add_executable(logical_plan_tests
        tests/test_logical_plan.cpp
    )

    target_include_directories(logical_plan_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${ANTLR_GENERATED_DIR}"
    )

    target_link_libraries(logical_plan_tests PRIVATE
        eugraph_query_engine
        GTest::gtest
        GTest::gtest_main
    )

    gtest_add_tests(TARGET logical_plan_tests)

    # Query executor end-to-end tests
    add_executable(query_executor_tests
        tests/test_query_executor.cpp
    )

    target_include_directories(query_executor_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${WT_INSTALL_DIR}/include"
    )

    target_link_libraries(query_executor_tests PRIVATE
        eugraph_query_engine
        eugraph_metadata
        GTest::gtest
        GTest::gtest_main
        dl
    )

    add_dependencies(query_executor_tests wiredtiger_ext)
    gtest_add_tests(TARGET query_executor_tests)

    # Metadata service tests
    add_executable(metadata_tests
        tests/test_metadata_service.cpp
    )

    target_include_directories(metadata_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${WT_INSTALL_DIR}/include"
    )

    target_link_libraries(metadata_tests PRIVATE
        eugraph_metadata
        GTest::gtest
        GTest::gtest_main
        dl
    )

    add_dependencies(metadata_tests wiredtiger_ext)
    gtest_add_tests(TARGET metadata_tests)

    # RPC integration tests (handler + thrift types)
    add_executable(rpc_integration_tests
        tests/test_rpc_integration.cpp
    )

    target_include_directories(rpc_integration_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${WT_INSTALL_DIR}/include"
    )

    target_link_libraries(rpc_integration_tests PRIVATE
        eugraph_shell_lib
        eugraph_query_engine
        eugraph_metadata
        eugraph_thrift
        Folly::folly
        Folly::folly_deps
        GTest::gtest
        GTest::gtest_main
        dl
    )

    add_dependencies(rpc_integration_tests wiredtiger_ext)
    gtest_add_tests(TARGET rpc_integration_tests)
endif()
