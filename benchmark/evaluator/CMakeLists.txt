# Google benchmark targets for the query expression evaluator.
#
# Enabled by -DEUGRAPH_ENABLE_BENCHMARK=ON. Results are meant to be compared
# before / after evaluator optimizations (see
# docs/query/engine/evaluator-benchmarks.md).

add_executable(evaluator_bench
    benchmark_main.cpp
    kernel_micro_bench.cpp
)

target_include_directories(evaluator_bench PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${WT_INSTALL_DIR}/include"
)

target_link_libraries(evaluator_bench PRIVATE
    eugraph_query_engine
    Folly::folly
    benchmark::benchmark
)
