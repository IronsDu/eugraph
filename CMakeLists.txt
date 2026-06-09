cmake_minimum_required(VERSION 3.25)

project(eugraph C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo")
endif()

# Options
option(EUGRAPH_BUILD_TESTS "Build unit tests" ON)
option(EUGRAPH_ENABLE_CCACHE "Enable ccache for faster rebuilds" OFF)
option(EUGRAPH_UNITY_BUILD "Enable unity builds to reduce compilation time" OFF)

# ccache support
if(EUGRAPH_ENABLE_CCACHE)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        message(STATUS "ccache found: ${CCACHE_PROGRAM}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    else()
        message(WARNING "ccache requested but not found")
    endif()
endif()

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
    PATCH_COMMAND sed -i
        "/^[^#]/ { /add_subdirectory(bench\\/wtperf)/s/^/# PATCHED: / }"
        <SOURCE_DIR>/CMakeLists.txt &&
        sed -i
        "/^[^#]/ { /add_subdirectory(bench\\/tiered)/s/^/# PATCHED: / }"
        <SOURCE_DIR>/CMakeLists.txt &&
        sed -i
        "/^[^#]/ { /add_subdirectory(bench\\/wt2853_perf)/s/^/# PATCHED: / }"
        <SOURCE_DIR>/CMakeLists.txt &&
        sed -i
        "/^[^#]/ { /add_subdirectory(examples)/s/^/# PATCHED: / }"
        <SOURCE_DIR>/CMakeLists.txt &&
        sed -i
        "/^[^#]/ { /^add_subdirectory(test)/s/^/# PATCHED: / }"
        <SOURCE_DIR>/CMakeLists.txt &&
        sed -i
        "/^[^#]/ { /add_subdirectory(tools\\/checksum_bitflip)/s/^/# PATCHED: / }"
        <SOURCE_DIR>/CMakeLists.txt &&
        sed -i
        "/^[^#]/ { /add_subdirectory(test\\/fail_fs)/s/^/# PATCHED: / }"
        <SOURCE_DIR>/ext/CMakeLists.txt
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        -DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_C_COMPILER_LAUNCHER=${CMAKE_C_COMPILER_LAUNCHER}
        -DCMAKE_CXX_COMPILER_LAUNCHER=${CMAKE_CXX_COMPILER_LAUNCHER}
        -DCMAKE_C_FLAGS=-Wno-error
        -DCMAKE_CXX_FLAGS=-Wno-error
        -DENABLE_STATIC=ON
        -DENABLE_SHARED=OFF
        -DENABLE_STRICT=OFF
    BUILD_BYPRODUCTS
        "${WT_LIB}"
)

# Ensure include dir exists at configure time for CMake validation
file(MAKE_DIRECTORY "${WT_INSTALL_DIR}/include")

# Library target (WiredTiger multi-table backend)
add_library(eugraph_lib STATIC
    src/storage/kv/key_codec.cpp
    src/storage/kv/value_codec.cpp
    src/storage/kv/index_key_codec.cpp
    src/storage/wt_cursor.cpp
    src/storage/wt_session.cpp
    src/storage/wt_connection.cpp
    src/storage/wt_store_base.cpp
    src/storage/data/sync_graph_data_store.cpp
    src/storage/meta/sync_graph_meta_store.cpp
    src/storage/meta/meta_codec.cpp
    src/storage/catalog/catalog_store.cpp
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
set(ANTLR_GENERATED_DIR "${PROJECT_SOURCE_DIR}/src/query/parser/generated")
set(ANTLR_GENERATED_SOURCES
    "${ANTLR_GENERATED_DIR}/CypherLexer.cpp"
    "${ANTLR_GENERATED_DIR}/CypherParser.cpp"
    "${ANTLR_GENERATED_DIR}/CypherParserBaseVisitor.cpp"
    "${ANTLR_GENERATED_DIR}/CypherParserVisitor.cpp"
)

add_library(eugraph_compute STATIC
    ${ANTLR_GENERATED_SOURCES}
    src/query/parser/cypher_parser.cpp
    src/query/parser/index_ddl_parser.cpp
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
    src/storage/meta/async_graph_meta_store.cpp
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
    src/common/types/temporal_value.cpp
    src/query/catalog/catalog.cpp
    src/query/function/batch_ops.cpp
    src/query/function/function_registry.cpp
    src/query/planner/binder/binder.cpp
    src/query/planner/binder/bind_match.cpp
    src/query/planner/binder/bind_return.cpp
    src/query/planner/binder/bind_mutation.cpp
    src/query/planner/binder/bind_merge.cpp
    src/query/planner/binder/bind_expression.cpp
    src/query/planner/binder/bind_pattern.cpp
    src/query/planner/binder/bind_projection.cpp
    src/query/planner/column_resolver.cpp
    src/query/evaluator/vectorized_evaluator.cpp
    src/query/evaluator/eval_binary_op.cpp
    src/query/evaluator/eval_property.cpp
    src/query/evaluator/eval_function_call.cpp
    src/query/evaluator/eval_quantifier.cpp
    src/query/evaluator/eval_case.cpp
    src/query/evaluator/eval_map.cpp
    src/query/evaluator/eval_subscript.cpp
    src/query/evaluator/eval_slice.cpp
    src/query/evaluator/eval_list_comprehension.cpp
    src/query/physical_plan/operator/edge_index_scan_physical_op.cpp
    src/query/physical_plan/operator/edge_property_read_physical_op.cpp
    src/query/physical_plan/operator/aggregate_physical_op.cpp
    src/query/physical_plan/operator/all_node_scan_physical_op.cpp
    src/query/physical_plan/operator/alter_edge_label_physical_op.cpp
    src/query/physical_plan/operator/create_edge_label_physical_op.cpp
    src/query/physical_plan/operator/create_edge_physical_op.cpp
    src/query/physical_plan/operator/create_node_physical_op.cpp
    src/query/physical_plan/operator/correlated_source_physical_op.cpp
    src/query/physical_plan/operator/cross_product_physical_op.cpp
    src/query/physical_plan/operator/distinct_physical_op.cpp
    src/query/physical_plan/operator/expand_physical_op.cpp
    src/query/physical_plan/operator/filter_physical_op.cpp
    src/query/physical_plan/operator/index_scan_physical_op.cpp
    src/query/physical_plan/operator/label_scan_physical_op.cpp
    src/query/physical_plan/operator/limit_physical_op.cpp
    src/query/physical_plan/operator/left_join_physical_op.cpp
    src/query/physical_plan/operator/varlen_expand_physical_op.cpp
    src/query/physical_plan/operator/path_build_physical_op.cpp
    src/query/physical_plan/operator/project_physical_op.cpp
    src/query/physical_plan/operator/delete_physical_op.cpp
    src/query/physical_plan/operator/remove_physical_op.cpp
    src/query/physical_plan/operator/semi_join_physical_op.cpp
    src/query/physical_plan/operator/set_physical_op.cpp
    src/query/physical_plan/operator/singleton_physical_op.cpp
    src/query/physical_plan/operator/skip_physical_op.cpp
    src/query/physical_plan/operator/sort_physical_op.cpp
    src/query/physical_plan/operator/union_physical_op.cpp
    src/query/physical_plan/operator/unwind_physical_op.cpp
    src/query/physical_plan/operator/merge_physical_op.cpp
    src/query/physical_plan/operator/vertex_label_read_physical_op.cpp
    src/query/physical_plan/operator/vertex_property_read_physical_op.cpp
    src/query/physical_plan/physical_operator_base.cpp
    src/query/physical_plan/physical_planner.cpp
    src/query/executor/query_executor.cpp
    src/query/optimizer/memo.cpp
    src/query/optimizer/opt_rule.cpp
    src/query/optimizer/task.cpp
    src/query/optimizer/logical_optimizer.cpp
    src/query/optimizer/rules/filter_pushdown.cpp
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

if(EUGRAPH_UNITY_BUILD)
    set_target_properties(eugraph_query_engine PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_BATCH_SIZE 8
    )
endif()

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

# Disable warnings for thrift-generated code
target_compile_options(eugraph_thrift PRIVATE -w)

target_link_libraries(eugraph_thrift PUBLIC
    FBThrift::thriftcpp2
    FBThrift::async
    Folly::folly
)

# ==================== Thrift formatting (shared by shell & TCK) ====================

add_library(eugraph_thrift_fmt STATIC
    src/thrift_fmt/result_format.cpp
)

target_include_directories(eugraph_thrift_fmt PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(eugraph_thrift_fmt PUBLIC
    eugraph_thrift
)

# ==================== Shell (REPL) ====================

add_library(eugraph_shell_lib STATIC
    src/program/shell/shell_repl.cpp
    src/program/shell/rpc_client.cpp
    src/program/server/eugraph_handler.cpp
    src/storage/graph_manager.cpp
    third_party/linenoise/linenoise.c
)

target_include_directories(eugraph_shell_lib PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/third_party/linenoise"
)

target_link_libraries(eugraph_shell_lib PUBLIC
    eugraph_query_engine
    eugraph_metadata
    eugraph_thrift
    eugraph_thrift_fmt
    Folly::folly
    Folly::folly_deps
    spdlog::spdlog
)

# ==================== Server + Shell executables ====================

add_executable(eugraph-server
    src/program/server/eugraph_server_main.cpp
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
    src/program/shell/shell_main.cpp
)

target_include_directories(eugraph-shell PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(eugraph-shell PRIVATE
    eugraph_shell_lib
    dl
)

# ==================== CSV Loader ====================

add_library(eugraph_loader_lib STATIC
    src/program/loader/csv_loader.cpp
    src/program/shell/rpc_client.cpp
)

target_include_directories(eugraph_loader_lib PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(eugraph_loader_lib PUBLIC
    eugraph_thrift
    Folly::folly
    Folly::folly_deps
    spdlog::spdlog
)

add_executable(eugraph-loader
    src/program/loader/loader_main.cpp
)

target_include_directories(eugraph-loader PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(eugraph-loader PRIVATE
    eugraph_loader_lib
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
        Folly::folly
        Folly::folly_deps
        GTest::gtest
        GTest::gtest_main
        dl
    )

    add_dependencies(query_executor_tests wiredtiger_ext)
    gtest_add_tests(TARGET query_executor_tests)

    # Optimizer tests
    add_executable(optimizer_tests
        tests/test_optimizer.cpp
    )

    target_include_directories(optimizer_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${WT_INSTALL_DIR}/include"
    )

    target_link_libraries(optimizer_tests PRIVATE
        eugraph_query_engine
        eugraph_metadata
        Folly::folly
        Folly::folly_deps
        GTest::gtest
        GTest::gtest_main
        dl
    )

    add_dependencies(optimizer_tests wiredtiger_ext)
    gtest_add_tests(TARGET optimizer_tests)

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
        Folly::folly
        Folly::folly_deps
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
        dl
    )

    add_dependencies(rpc_integration_tests wiredtiger_ext)
    gtest_add_tests(TARGET rpc_integration_tests)

    # Restart persistence tests (data survives store close/reopen)
    add_executable(restart_persistence_tests
        tests/test_restart_persistence.cpp
    )

    target_include_directories(restart_persistence_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${WT_INSTALL_DIR}/include"
    )

    target_link_libraries(restart_persistence_tests PRIVATE
        eugraph_query_engine
        eugraph_metadata
        Folly::folly
        Folly::folly_deps
        GTest::gtest
        dl
    )

    add_dependencies(restart_persistence_tests wiredtiger_ext)
    gtest_add_tests(TARGET restart_persistence_tests)

    # Index key codec + metadata codec tests
    add_executable(index_codec_tests
        tests/test_index_key_codec.cpp
    )

    target_include_directories(index_codec_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${WT_INSTALL_DIR}/include"
    )

    target_link_libraries(index_codec_tests PRIVATE
        eugraph_lib
        GTest::gtest
        GTest::gtest_main
        dl
    )

    add_dependencies(index_codec_tests wiredtiger_ext)
    gtest_add_tests(TARGET index_codec_tests)

    # Index store integration tests
    add_executable(index_store_tests
        tests/test_index_store.cpp
    )

    target_include_directories(index_store_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${WT_INSTALL_DIR}/include"
    )

    target_link_libraries(index_store_tests PRIVATE
        eugraph_lib
        GTest::gtest
        GTest::gtest_main
        dl
    )

    add_dependencies(index_store_tests wiredtiger_ext)
    gtest_add_tests(TARGET index_store_tests)

    # Index E2E tests (meta + data + query engine integration)
    add_executable(index_e2e_tests
        tests/test_index_e2e.cpp
    )

    target_include_directories(index_e2e_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${WT_INSTALL_DIR}/include"
    )

    target_link_libraries(index_e2e_tests PRIVATE
        eugraph_query_engine
        eugraph_metadata
        Folly::folly
        Folly::folly_deps
        GTest::gtest
        GTest::gtest_main
        dl
    )

    add_dependencies(index_e2e_tests wiredtiger_ext)
    gtest_add_tests(TARGET index_e2e_tests)

    # CSV Loader integration tests
    add_executable(loader_integration_tests
        tests/test_loader_integration.cpp
    )

    target_include_directories(loader_integration_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${WT_INSTALL_DIR}/include"
    )

    target_link_libraries(loader_integration_tests PRIVATE
        eugraph_loader_lib
        eugraph_shell_lib
        eugraph_query_engine
        eugraph_metadata
        eugraph_thrift
        Folly::folly
        Folly::folly_deps
        GTest::gtest
        dl
    )

    add_dependencies(loader_integration_tests wiredtiger_ext)
    gtest_add_tests(TARGET loader_integration_tests)

    # Catalog + GraphManager tests
    add_executable(catalog_tests
        tests/test_catalog_and_graph_manager.cpp
    )

    target_include_directories(catalog_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${WT_INSTALL_DIR}/include"
    )

    target_link_libraries(catalog_tests PRIVATE
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

    add_dependencies(catalog_tests wiredtiger_ext)
    gtest_add_tests(TARGET catalog_tests)

    # Vertex serialization tests
    add_executable(vertex_serialization_tests
        tests/test_vertex_serialization.cpp
    )

    target_include_directories(vertex_serialization_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${WT_INSTALL_DIR}/include"
    )

    target_link_libraries(vertex_serialization_tests PRIVATE
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

    add_dependencies(vertex_serialization_tests wiredtiger_ext)
    gtest_add_tests(TARGET vertex_serialization_tests)

    # TCK tests (Cucumber BDD for openCypher TCK) — requires eugraph-server built
    if(NOT DEFINED SKIP_TCK_TESTS)
        add_subdirectory(tests/tck)
    endif()
endif()
