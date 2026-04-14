#include "compute_service/executor/query_executor.hpp"

#include <folly/coro/BlockingWait.h>

namespace eugraph {
namespace compute {

QueryExecutor::QueryExecutor(IGraphStore& store, IMetadataService& meta, Config config)
    : store_(store), meta_(meta), config_(config),
      compute_pool_(std::make_shared<folly::CPUThreadPoolExecutor>(config.compute_threads)),
      io_scheduler_(std::make_shared<IoScheduler>(config.io_threads)) {}

QueryExecutor::~QueryExecutor() = default;

folly::coro::AsyncGenerator<RowBatch> QueryExecutor::execute(const std::string& cypher_query) {
    // 1. Parse
    cypher::CypherQueryParser parser;
    auto parse_result = parser.parse(cypher_query);
    if (std::holds_alternative<cypher::ParseError>(parse_result)) {
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

    // 3. Load label/edge_label mappings from metadata service
    auto labels = co_await meta_.listLabels();
    auto edge_labels = co_await meta_.listEdgeLabels();

    std::unordered_map<std::string, LabelId> label_name_to_id;
    for (const auto& l : labels)
        label_name_to_id[l.name] = l.id;

    std::unordered_map<std::string, EdgeLabelId> edge_label_name_to_id;
    for (const auto& el : edge_labels)
        edge_label_name_to_id[el.name] = el.id;

    // 4. Plan physical operators
    GraphTxnHandle txn = store_.beginTransaction();
    AsyncGraphStore async_store(store_, *io_scheduler_, txn);

    PlanContext ctx;
    ctx.label_name_to_id = std::move(label_name_to_id);
    ctx.edge_label_name_to_id = std::move(edge_label_name_to_id);

    // Pre-allocate IDs for CREATE operations
    // Allocate a batch of IDs upfront; individual create ops consume them
    ctx.next_vertex_id = co_await meta_.nextVertexId();
    ctx.next_edge_id = co_await meta_.nextEdgeId();

    PhysicalPlanner physical_planner;
    auto phys_result = physical_planner.plan(logical_plan, async_store, ctx);
    if (std::holds_alternative<std::string>(phys_result)) {
        store_.rollbackTransaction(txn);
        co_return;
    }
    auto& phys_op = std::get<std::unique_ptr<PhysicalOperator>>(phys_result);

    // 5. Execute
    auto gen = phys_op->execute();
    while (auto batch = co_await gen.next()) {
        co_yield std::move(*batch);
    }

    // Commit transaction
    store_.commitTransaction(txn);
}

std::vector<Row> QueryExecutor::executeSync(const std::string& cypher_query) {
    std::vector<Row> all_rows;

    auto task = folly::coro::co_invoke([this, &cypher_query, &all_rows]() -> folly::coro::Task<void> {
        auto gen = execute(cypher_query);
        while (auto batch = co_await gen.next()) {
            for (auto& row : batch->rows) {
                all_rows.push_back(std::move(row));
            }
        }
    });

    folly::coro::blockingWait(folly::coro::co_withExecutor(compute_pool_.get(), std::move(task)));

    return all_rows;
}

} // namespace compute
} // namespace eugraph
