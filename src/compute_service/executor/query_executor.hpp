#pragma once

#include "compute_service/executor/async_graph_store.hpp"
#include "compute_service/executor/io_scheduler.hpp"
#include "compute_service/executor/row.hpp"
#include "compute_service/logical_plan/logical_plan_builder.hpp"
#include "compute_service/parser/ast.hpp"
#include "compute_service/parser/cypher_parser.hpp"
#include "compute_service/physical_plan/physical_operator.hpp"
#include "compute_service/physical_plan/physical_planner.hpp"
#include "metadata_service/metadata_service.hpp"
#include "storage/graph_store.hpp"

#include <folly/coro/AsyncGenerator.h>
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
class QueryExecutor {
public:
    struct Config {
        size_t compute_threads = 4;
        size_t io_threads = 2;
        Config() = default;
    };

    QueryExecutor(IGraphStore& store, IMetadataService& meta, Config config);
    ~QueryExecutor();

    /// Execute a Cypher query string. Returns batches of result rows.
    folly::coro::AsyncGenerator<RowBatch> execute(const std::string& cypher_query);

    /// Execute a Cypher query synchronously (blocking). Returns result with columns, rows, and error.
    ExecutionResult executeSync(const std::string& cypher_query);

    /// Execute a Cypher query as a coroutine (non-blocking). Schedules work on
    /// the compute pool and co_awaits completion. Safe to call from IO threads.
    folly::coro::Task<ExecutionResult> executeAsync(const std::string& cypher_query);

private:
    IGraphStore& store_;
    IMetadataService& meta_;
    Config config_;
    std::shared_ptr<folly::CPUThreadPoolExecutor> compute_pool_;
    std::shared_ptr<IoScheduler> io_scheduler_;

    void extractColumnsFromLogicalPlan(const LogicalOperator& op, Schema& columns);
};

} // namespace compute
} // namespace eugraph
