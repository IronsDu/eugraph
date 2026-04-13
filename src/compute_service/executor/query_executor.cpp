#include "compute_service/executor/query_executor.hpp"

#include <folly/coro/BlockingWait.h>

namespace eugraph {
namespace compute {

QueryExecutor::QueryExecutor(IGraphStore& store, Config config)
    : store_(store), config_(config),
      compute_pool_(std::make_shared<folly::CPUThreadPoolExecutor>(config.compute_threads)),
      io_scheduler_(std::make_shared<IoScheduler>(config.io_threads)) {}

QueryExecutor::~QueryExecutor() = default;

void QueryExecutor::setLabelMappings(std::unordered_map<std::string, LabelId> label_name_to_id,
                                      std::unordered_map<std::string, EdgeLabelId> edge_label_name_to_id) {
    label_name_to_id_ = std::move(label_name_to_id);
    edge_label_name_to_id_ = std::move(edge_label_name_to_id);
}

folly::coro::AsyncGenerator<RowBatch> QueryExecutor::execute(const std::string& cypher_query) {
    // 1. Parse
    cypher::CypherQueryParser parser;
    auto parse_result = parser.parse(cypher_query);
    if (std::holds_alternative<cypher::ParseError>(parse_result)) {
        const auto& err = std::get<cypher::ParseError>(parse_result);
        // Yield an empty result on parse error — in production, propagate error
        co_return;
    }
    auto stmt = std::move(std::get<cypher::Statement>(parse_result));

    // 2. Build logical plan
    LogicalPlanBuilder plan_builder;
    auto logical_result = plan_builder.build(std::move(stmt));
    if (std::holds_alternative<std::string>(logical_result)) {
        co_return;
    }
    auto& logical_plan = std::get<LogicalPlan>(logical_result);

    // 3. Plan physical operators
    GraphTxnHandle txn = store_.beginTransaction();
    AsyncGraphStore async_store(store_, *io_scheduler_, txn);

    PlanContext ctx;
    ctx.label_name_to_id = label_name_to_id_;
    ctx.edge_label_name_to_id = edge_label_name_to_id_;

    PhysicalPlanner physical_planner;
    auto phys_result = physical_planner.plan(logical_plan, async_store, ctx);
    if (std::holds_alternative<std::string>(phys_result)) {
        store_.rollbackTransaction(txn);
        co_return;
    }
    auto& phys_op = std::get<std::unique_ptr<PhysicalOperator>>(phys_result);

    // 4. Execute: run the physical operator tree via compute executor
    auto gen = phys_op->execute();
    while (auto batch = co_await gen.next()) {
        co_yield std::move(*batch);
    }

    // Commit transaction
    store_.commitTransaction(txn);
}

std::vector<Row> QueryExecutor::executeSync(const std::string& cypher_query) {
    std::vector<Row> all_rows;

    folly::coro::blockingWait(folly::coro::co_invoke([this, &cypher_query, &all_rows]() -> folly::coro::Task<void> {
        auto gen = execute(cypher_query);
        while (auto batch = co_await gen.next()) {
            for (auto& row : batch->rows) {
                all_rows.push_back(std::move(row));
            }
        }
    }));

    return all_rows;
}

} // namespace compute
} // namespace eugraph
