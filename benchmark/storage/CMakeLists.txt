add_executable(storage_op_bench
    benchmark_main.cpp
    storage_op_bench.cpp
)

target_include_directories(storage_op_bench PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${WT_INSTALL_DIR}/include"
)

target_link_libraries(storage_op_bench PRIVATE
    eugraph_query_engine
    Folly::folly
    benchmark::benchmark
)
