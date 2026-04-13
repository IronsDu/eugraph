#pragma once

#include "compute_service/executor/async_graph_store.hpp"
#include "compute_service/executor/io_scheduler.hpp"
#include "compute_service/executor/row.hpp"
#include "compute_service/logical_plan/logical_plan_builder.hpp"
#include "compute_service/parser/cypher_parser.hpp"
#include "compute_service/physical_plan/physical_operator.hpp"
#include "compute_service/physical_plan/physical_planner.hpp"
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

    QueryExecutor(IGraphStore& store) : QueryExecutor(store, Config{}) {}
    QueryExecutor(IGraphStore& store, Config config);
    ~QueryExecutor();

    /// Execute a Cypher query string. Returns batches of result rows.
    folly::coro::AsyncGenerator<RowBatch> execute(const std::string& cypher_query);

    /// Execute a Cypher query synchronously (blocking). For testing convenience.
    std::vector<Row> executeSync(const std::string& cypher_query);

    /// Set label name → ID mappings. Must be called before queries.
    void setLabelMappings(std::unordered_map<std::string, LabelId> label_name_to_id,
                          std::unordered_map<std::string, EdgeLabelId> edge_label_name_to_id);

private:
    IGraphStore& store_;
    Config config_;
    std::shared_ptr<folly::CPUThreadPoolExecutor> compute_pool_;
    std::shared_ptr<IoScheduler> io_scheduler_;
    std::unordered_map<std::string, LabelId> label_name_to_id_;
    std::unordered_map<std::string, EdgeLabelId> edge_label_name_to_id_;
};

} // namespace compute
} // namespace eugraph
