add_executable(executor_hop_bench
    executor_hop_bench.cpp
)

target_include_directories(executor_hop_bench PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(executor_hop_bench PRIVATE
    eugraph_lib
    Folly::folly
)
