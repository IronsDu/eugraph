#pragma once

#include "common/types/index_state.hpp"
#include "query/catalog/catalog.hpp"
#include "query/dataset/data_chunk.hpp"
#include "query/dataset/row.hpp"
#include "query/function/function_registry.hpp"
#include "query/parser/ast.hpp"
#include "query/parser/cypher_parser.hpp"
#include "query/parser/index_ddl_parser.hpp"
#include "query/physical_plan/physical_planner.hpp"
#include "query/physical_plan/query_context.hpp"
#include "query/planner/binder.hpp"
#include "storage/data/i_async_graph_data_store.hpp"
#include "storage/meta/i_async_graph_meta_store.hpp"

#include <folly/coro/Task.h>
#include <folly/executors/CPUThreadPoolExecutor.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <variant>

namespace eugraph {

class IndexBuildService; // 前向声明（实现细节见 .cpp，保持编译防火墙）
namespace compute {

struct StreamContext {
    Schema columns;
    std::string error;
    std::unique_ptr<PhysicalOperator> phys_op;
    folly::coro::AsyncGenerator<DataChunk> gen;
    GraphTxnHandle txn = INVALID_GRAPH_TXN;
    IAsyncGraphDataStore& store;
    // Owns the transaction-bound store used by the physical plan.
    std::unique_ptr<IAsyncGraphDataStore> query_store;
    /// Per-statement execution state, shared with the operator tree (see
    /// PhysicalOperator::setQueryContext). Declared after phys_op/query_store on
    /// purpose: it must not be the thing that keeps them alive during teardown.
    std::shared_ptr<QueryContext> query_context;
    bool should_commit = true;
    // Owned by StreamContext so references in physical operators remain valid
    std::unordered_map<LabelId, LabelDef> label_defs;
    std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_defs;
    std::unordered_map<std::string, LabelId> label_name_to_id;
    std::unordered_map<std::string, EdgeLabelId> edge_label_name_to_id;
    // Binder results: catalog, function registry, bound expressions
    std::unique_ptr<catalog::Catalog> catalog;
    std::unique_ptr<function::FunctionRegistry> func_registry;
    std::unique_ptr<binder::BoundStatement> bound_plan;
    // Presentation metadata: per-column label order for vertex formatting.
    std::unordered_map<std::string, std::vector<LabelId>> label_order;

    explicit StreamContext(IAsyncGraphDataStore& s) : store(s) {}
};

/// Top-level query execution engine.
/// Orchestrates: parse → logical plan → physical plan → execute.
/// Depends only on async interfaces — no direct sync store dependency.
class QueryExecutor {
public:
    /// 注入"每图索引构建服务"：注入后 CREATE INDEX 走**后台异步**构建；未注入则走同步路径（既有行为）。
    void setIndexBuildService(std::shared_ptr<IndexBuildService> service);

    struct Config {
        size_t compute_threads = 4;
        Config() = default;
    };

    QueryExecutor(IAsyncGraphDataStore& async_data, IAsyncGraphMetaStore& async_meta, Config config);
    ~QueryExecutor();

    /// Build the streaming execution context for one query. `cancel`, when given, is
    /// armed on the query's store wrapper so that every physical operator observes it
    /// (see IAsyncGraphDataStore::cancelled); pass nullptr for non-cancellable callers.
    folly::coro::Task<std::shared_ptr<StreamContext>>
    prepareStream(const std::string& cypher_query, const std::unordered_map<std::string, Value>& params = {},
                  QueryCancel cancel = nullptr);

    folly::Executor* computeExecutor() const {
        return compute_pool_.get();
    }

private:
    std::shared_ptr<IndexBuildService> index_builds_;
    folly::coro::Task<void> handleIndexDdl(const IndexDdlStatement& stmt, ExecutionResult& result);

    /// 顶点索引的 accessor 解析结果（原为 handleIndexDdl 内的局部结构体，抽出以便协程参数化）
    struct ResolvedIndexAccessor {
        bool is_strong = false;
        LabelId source_label_id = 0;
        uint16_t source_prop_id = UINT16_MAX;
        std::string property_name;
    };

    /// 顶点索引回填 + 提交（**不落状态**：由调用方或发布回调落，保持两库提交顺序）。
    /// 抽成协程的原因：同步路径与后台构建任务需要**同一份实现**（P1-④）。
    /// `cancelled` 为可选取消令牌：**逐批检查**（构建中 DROP 时据此尽快退出，而不是把整个回填跑完）。
    folly::coro::Task<IndexBuildResult> backfillVertexIndex(IndexDdlStatement stmt, std::string table, LabelId label_id,
                                                            std::vector<ResolvedIndexAccessor> resolved,
                                                            std::function<bool()> cancelled = {});

    /// 边索引回填 + 提交（同样**不落状态**）
    folly::coro::Task<IndexBuildResult> backfillEdgeIndex(IndexDdlStatement stmt, std::string table,
                                                          EdgeLabelId edge_label_id, std::vector<uint16_t> prop_ids,
                                                          std::function<bool()> cancelled = {});
    IAsyncGraphDataStore& async_data_;
    IAsyncGraphMetaStore& async_meta_;
    Config config_;
    std::shared_ptr<folly::CPUThreadPoolExecutor> compute_pool_;
};

} // namespace compute
} // namespace eugraph
