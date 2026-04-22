#pragma once

#include "compute_service/executor/row.hpp"
#include "compute_service/logical_plan/logical_plan_builder.hpp"
#include "compute_service/parser/ast.hpp"
#include "compute_service/parser/cypher_parser.hpp"
#include "compute_service/physical_plan/physical_planner.hpp"
#include "storage/data/i_async_graph_data_store.hpp"
#include "storage/meta/i_async_graph_meta_store.hpp"

#include <folly/coro/Task.h>
#include <folly/executors/CPUThreadPoolExecutor.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <variant>

namespace eugraph {
namespace compute {

/// Top-level query execution engine.
/// Orchestrates: parse → logical plan → physical plan → execute.
/// Depends only on async interfaces — no direct sync store dependency.
class QueryExecutor {
public:
    struct Config {
        size_t compute_threads = 4;
        Config() = default;
    };

    QueryExecutor(IAsyncGraphDataStore& async_data, IAsyncGraphMetaStore& async_meta, Config config);
    ~QueryExecutor();

    ExecutionResult executeSync(const std::string& cypher_query);
    folly::coro::Task<ExecutionResult> executeAsync(const std::string& cypher_query);

private:
    IAsyncGraphDataStore& async_data_;
    IAsyncGraphMetaStore& async_meta_;
    Config config_;
    std::shared_ptr<folly::CPUThreadPoolExecutor> compute_pool_;

    void extractColumnsFromLogicalPlan(const LogicalOperator& op, Schema& columns);
};

} // namespace compute
} // namespace eugraph
