#pragma once

#include "compute_service/binder/plan_binder.hpp"
#include "compute_service/catalog/catalog.hpp"
#include "compute_service/executor/data_chunk.hpp"
#include "compute_service/executor/row.hpp"
#include "compute_service/function/function_registry.hpp"
#include "compute_service/logical_plan/logical_plan_builder.hpp"
#include "compute_service/parser/ast.hpp"
#include "compute_service/parser/cypher_parser.hpp"
#include "compute_service/parser/index_ddl_parser.hpp"
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

struct StreamContext {
    Schema columns;
    std::string error;
    std::unique_ptr<PhysicalOperator> phys_op;
    folly::coro::AsyncGenerator<DataChunk> gen;
    GraphTxnHandle txn = INVALID_GRAPH_TXN;
    IAsyncGraphDataStore& store;
    bool should_commit = true;
    // Owned by StreamContext so references in physical operators remain valid
    std::unordered_map<LabelId, LabelDef> label_defs;
    std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_defs;
    std::unordered_map<std::string, LabelId> label_name_to_id;
    std::unordered_map<std::string, EdgeLabelId> edge_label_name_to_id;
    // Binder results: catalog, function registry, bound expressions
    std::unique_ptr<catalog::Catalog> catalog;
    std::unique_ptr<function::FunctionRegistry> func_registry;
    std::unique_ptr<binder::BoundPlanResult> bound_plan;

    explicit StreamContext(IAsyncGraphDataStore& s) : store(s) {}
};

/// Top-level query execution engine.
/// Orchestrates: parse → logical plan → physical plan → execute.
/// Depends only on async interfaces — no direct sync store dependency.
class QueryExecutor {
public:
    struct Config {
        size_t compute_threads = 4;
        bool enable_binder = false; // set to true to enable PlanBinder validation pass
        Config() = default;
    };

    QueryExecutor(IAsyncGraphDataStore& async_data, IAsyncGraphMetaStore& async_meta, Config config);
    ~QueryExecutor();

    folly::coro::Task<std::shared_ptr<StreamContext>> prepareStream(const std::string& cypher_query);

private:
    folly::coro::Task<void> handleIndexDdl(const IndexDdlStatement& stmt, ExecutionResult& result);
    IAsyncGraphDataStore& async_data_;
    IAsyncGraphMetaStore& async_meta_;
    Config config_;
    std::shared_ptr<folly::CPUThreadPoolExecutor> compute_pool_;

    void extractColumnsFromLogicalPlan(const LogicalOperator& op, Schema& columns);
};

} // namespace compute
} // namespace eugraph
