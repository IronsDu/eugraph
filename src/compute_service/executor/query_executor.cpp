#include "compute_service/executor/query_executor.hpp"

#include <folly/coro/BlockingWait.h>

#include <spdlog/spdlog.h>

namespace eugraph {
namespace compute {

QueryExecutor::QueryExecutor(ISyncGraphDataStore& sync_data, IAsyncGraphDataStore& async_data,
                             IAsyncGraphMetaStore& async_meta, Config config)
    : sync_data_(sync_data), async_data_(async_data), async_meta_(async_meta), config_(config),
      compute_pool_(std::make_shared<folly::CPUThreadPoolExecutor>(config.compute_threads)),
      io_scheduler_(std::make_shared<IoScheduler>(config.io_threads)) {}

QueryExecutor::~QueryExecutor() = default;

ExecutionResult QueryExecutor::executeSync(const std::string& cypher_query) {
    return folly::coro::blockingWait(executeAsync(cypher_query));
}

folly::coro::Task<ExecutionResult> QueryExecutor::executeAsync(const std::string& cypher_query) {
    ExecutionResult result;

    auto inner = folly::coro::co_invoke([this, &cypher_query, &result]() -> folly::coro::Task<void> {
        // 1. Parse
        cypher::CypherQueryParser parser;
        auto parse_result = parser.parse(cypher_query);
        if (std::holds_alternative<cypher::ParseError>(parse_result)) {
            result.error = std::get<cypher::ParseError>(parse_result).message;
            co_return;
        }
        auto stmt = std::move(std::get<cypher::Statement>(parse_result));

        // 2. Build logical plan
        LogicalPlanBuilder plan_builder;
        auto logical_result = plan_builder.build(std::move(stmt));
        if (std::holds_alternative<std::string>(logical_result)) {
            result.error = std::get<std::string>(logical_result);
            co_return;
        }
        auto& logical_plan = std::get<LogicalPlan>(logical_result);

        extractColumnsFromLogicalPlan(logical_plan.root, result.columns);

        // 3. Load label/edge_label mappings from metadata service
        auto labels = co_await async_meta_.listLabels();
        auto edge_labels = co_await async_meta_.listEdgeLabels();

        std::unordered_map<std::string, LabelId> label_name_to_id;
        for (const auto& l : labels)
            label_name_to_id[l.name] = l.id;

        std::unordered_map<std::string, EdgeLabelId> edge_label_name_to_id;
        for (const auto& el : edge_labels)
            edge_label_name_to_id[el.name] = el.id;

        // 4. Plan physical operators
        GraphTxnHandle txn = sync_data_.beginTransaction();
        AsyncGraphDataStore async_store(sync_data_, *io_scheduler_, txn);

        PlanContext ctx;
        ctx.label_name_to_id = std::move(label_name_to_id);
        ctx.edge_label_name_to_id = std::move(edge_label_name_to_id);

        for (const auto& l : labels)
            ctx.label_defs[l.id] = l;
        for (const auto& el : edge_labels)
            ctx.edge_label_defs[el.id] = el;

        ctx.next_vertex_id = co_await async_meta_.nextVertexId();
        ctx.next_edge_id = co_await async_meta_.nextEdgeId();
        PhysicalPlanner physical_planner;
        auto phys_result = physical_planner.plan(logical_plan, async_store, ctx);
        if (std::holds_alternative<std::string>(phys_result)) {
            result.error = std::get<std::string>(phys_result);
            sync_data_.rollbackTransaction(txn);
            co_return;
        }
        auto& phys_op = std::get<std::unique_ptr<PhysicalOperator>>(phys_result);

        // 5. Execute
        auto gen = phys_op->execute();
        while (auto batch = co_await gen.next()) {
            for (auto& row : batch->rows) {
                result.rows.push_back(std::move(row));
            }
        }

        sync_data_.commitTransaction(txn);
    });

    co_await folly::coro::co_withExecutor(compute_pool_.get(), std::move(inner));

    co_return result;
}

void QueryExecutor::extractColumnsFromLogicalPlan(const LogicalOperator& op, Schema& columns) {
    std::visit(
        [&columns, this](const auto& ptr) {
            using T = std::decay_t<decltype(ptr)>;
            using OpType = typename T::element_type;

            if constexpr (std::is_same_v<OpType, ProjectOp>) {
                for (const auto& item : ptr->items) {
                    if (item.alias.has_value()) {
                        columns.push_back(*item.alias);
                    } else {
                        columns.push_back(cypher::expressionToString(item.expr));
                    }
                }
            } else if constexpr (std::is_same_v<OpType, LimitOp> || std::is_same_v<OpType, FilterOp>) {
                if (!ptr->children.empty()) {
                    extractColumnsFromLogicalPlan(ptr->children[0], columns);
                }
            }
        },
        op);
}

} // namespace compute
} // namespace eugraph
