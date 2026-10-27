#pragma once

#include "common/concurrency/graph_usage_gate.hpp"
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
#include "storage/data/async_graph_data_store.hpp"
#include "storage/data/i_async_graph_data_store.hpp"
#include "storage/data/session_pool.hpp"
#include "storage/meta/i_async_graph_meta_store.hpp"

#include <folly/coro/Task.h>
#include <folly/executors/CPUThreadPoolExecutor.h>

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>

namespace eugraph {
namespace compute {

struct StreamContext {
    Schema columns;
    std::string error;
    /// Set by prepareStream; used only by the EUGRAPH_IO_TRACE log.
    std::string query_text;
    /// 图租约：流在 executeCypher 返回后才被消费 ⇒ 必须让图活到流销毁 ✓（声明在 session 之前
    /// ⇒ 比 session 后销毁 ✓）。
    std::optional<GraphUsageGate::Guard> graph_guard;
    /// 本语句独占的会话租约 ✓。**声明在 query_store/phys_op 之前** ⇒ 最后销毁 ✓：
    /// 算子树/游标先释放，随后会话才 close（否则 ~WtCursor 碰已关闭的会话 ✗）。
    std::optional<SessionLease> session;
    /// 本语句会话绑定的异步门面（构造即绑定；算子与 EvalContext 引用它）。
    /// **声明在 phys_op/gen 之前** ⇒ 比算子树后销毁 ✓。
    std::optional<AsyncGraphDataStore> query_store;
    std::unique_ptr<PhysicalOperator> phys_op;
    folly::coro::AsyncGenerator<DataChunk> gen;
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

    StreamContext() = default;

    ~StreamContext() {
        if (!query_store)
            return;
        const char* trace = std::getenv("EUGRAPH_IO_TRACE");
        if (trace == nullptr || *trace == '\0')
            return;
        const uint64_t dispatches = query_store->ioDispatches();
        if (dispatches != 0)
            spdlog::info("[io-trace] io_dispatches={} {} query={}", dispatches, query_store->ioDispatchSummary(),
                         query_text);
    }

    /// 提交本语句的事务（无会话/显式事务 ⇒ 幂等成功 ✓）；必须检查返回值 ✓
    bool commit() {
        if (!session || !should_commit)
            return true;
        const bool ok = (*session)->commit().committed();
        if (!ok)
            spdlog::error("[StreamContext] commit failed: {}", (*session)->error().outcome().message);
        return ok;
    }
    /// 回滚本语句的事务（幂等 ✓）
    void rollback() noexcept {
        if (session)
            (*session)->rollback();
    }
};

/// Top-level query execution engine.
/// Orchestrates: parse → logical plan → physical plan → execute.
/// Depends only on async interfaces — no direct sync store dependency.
class QueryExecutor {
public:
    struct Config {
        size_t compute_threads = 4;
        Config() = default;
    };

    QueryExecutor(AsyncGraphDataStore& async_data, IAsyncGraphMetaStore& async_meta, SessionPool& session_pool,
                  Config config);
    ~QueryExecutor();

    /// Build the streaming execution context for one query. `cancel`, when given, is
    /// armed on the query's store wrapper so that every physical operator observes it
    /// (see IAsyncGraphDataStore::cancelled); pass nullptr for non-cancellable callers.
    /// `existing_session` 非空 ⇒ 语句加入一个**已开启**的显式事务（Bolt BEGIN…COMMIT ✓）：
    /// 不由本语句 begin/commit，会话仍由调用方持有 ✓。
    folly::coro::Task<std::shared_ptr<StreamContext>>
    prepareStream(const std::string& cypher_query, const std::unordered_map<std::string, Value>& params = {},
                  QueryCancel cancel = nullptr, SessionRef existing_session = std::nullopt);

    folly::Executor* computeExecutor() const {
        return compute_pool_.get();
    }

private:
    folly::coro::Task<void> handleIndexDdl(const IndexDdlStatement& stmt, ExecutionResult& result);
    AsyncGraphDataStore& async_data_;
    IAsyncGraphMetaStore& async_meta_;
    Config config_;
    std::shared_ptr<folly::CPUThreadPoolExecutor> compute_pool_;
    SessionPool& session_pool_;
};

} // namespace compute
} // namespace eugraph
