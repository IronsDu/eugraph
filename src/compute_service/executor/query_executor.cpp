#include "compute_service/executor/query_executor.hpp"

#include "common/types/constants.hpp"
#include "compute_service/parser/index_ddl_parser.hpp"

#include <folly/coro/BlockingWait.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>

namespace eugraph {
namespace compute {

QueryExecutor::QueryExecutor(IAsyncGraphDataStore& async_data, IAsyncGraphMetaStore& async_meta, Config config)
    : async_data_(async_data), async_meta_(async_meta), config_(config),
      compute_pool_(std::make_shared<folly::CPUThreadPoolExecutor>(config.compute_threads)) {}

QueryExecutor::~QueryExecutor() = default;

ExecutionResult QueryExecutor::executeSync(const std::string& cypher_query) {
    return folly::coro::blockingWait(executeAsync(cypher_query));
}

folly::coro::Task<ExecutionResult> QueryExecutor::executeAsync(const std::string& cypher_query) {
    ExecutionResult result;

    spdlog::info("{} 11111111", __FUNCTION__);
    auto inner = folly::coro::co_invoke([this, &cypher_query, &result]() -> folly::coro::Task<void> {
        spdlog::info("{} 222222222", __FUNCTION__);

        // 0. Try index DDL
        auto ddl_stmt = IndexDdlParser::tryParse(cypher_query);
        if (ddl_stmt.has_value()) {
            co_await handleIndexDdl(*ddl_stmt, result);
            co_return;
        }

        // 0.5. Check for EXPLAIN prefix
        bool is_explain = false;
        std::string query_to_parse;
        {
            std::string trimmed = cypher_query;
            size_t start = trimmed.find_first_not_of(" \t\r\n");
            if (start != std::string::npos) {
                trimmed = trimmed.substr(start);
            }
            std::string prefix = trimmed.substr(0, 7);
            std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::toupper);
            if (prefix == "EXPLAIN" && trimmed.size() > 7 && std::isspace(static_cast<unsigned char>(trimmed[7]))) {
                is_explain = true;
                query_to_parse = trimmed.substr(8);
                size_t qstart = query_to_parse.find_first_not_of(" \t\r\n");
                if (qstart != std::string::npos) {
                    query_to_parse = query_to_parse.substr(qstart);
                }
            } else {
                query_to_parse = cypher_query;
            }
        }

        // 1. Parse
        cypher::CypherQueryParser parser;
        auto parse_result = parser.parse(query_to_parse);
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

        spdlog::info("{} 333333333", __FUNCTION__);
        std::unordered_map<std::string, LabelId> label_name_to_id;
        for (const auto& l : labels)
            label_name_to_id[l.name] = l.id;

        std::unordered_map<std::string, EdgeLabelId> edge_label_name_to_id;
        for (const auto& el : edge_labels)
            edge_label_name_to_id[el.name] = el.id;

        // 4. Begin transaction and set on async data store
        GraphTxnHandle txn = co_await async_data_.beginTran();
        async_data_.setTransaction(txn);

        spdlog::info("{} 4444444444", __FUNCTION__);
        PlanContext ctx;
        ctx.label_name_to_id = std::move(label_name_to_id);
        ctx.edge_label_name_to_id = std::move(edge_label_name_to_id);

        for (const auto& l : labels)
            ctx.label_defs[l.id] = l;
        for (const auto& el : edge_labels)
            ctx.edge_label_defs[el.id] = el;

        ctx.next_vertex_id = co_await async_meta_.nextVertexId();
        ctx.next_edge_id = co_await async_meta_.nextEdgeId();

        // 5. Plan physical operators
        PhysicalPlanner physical_planner;
        auto phys_result = physical_planner.plan(logical_plan, async_data_, ctx);
        if (std::holds_alternative<std::string>(phys_result)) {
            result.error = std::get<std::string>(phys_result);
            co_await async_data_.rollbackTran(txn);
            co_return;
        }
        auto& phys_op = std::get<std::unique_ptr<PhysicalOperator>>(phys_result);

        // 5.5. If EXPLAIN, format plan and return without executing
        if (is_explain) {
            formatExplainPlan(*phys_op, result);
            co_await async_data_.rollbackTran(txn);
            co_return;
        }

        // 6. Execute
        auto gen = phys_op->execute();
        while (auto batch = co_await gen.next()) {
            for (auto& row : batch->rows) {
                result.rows.push_back(std::move(row));
            }
        }

        co_await async_data_.commitTran(txn);
        spdlog::info("{} 55555555555", __FUNCTION__);
    });

    co_await folly::coro::co_withExecutor(compute_pool_.get(), std::move(inner));

    spdlog::info("{} 66666666666666", __FUNCTION__);
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

void QueryExecutor::formatExplainPlan(PhysicalOperator& root, ExecutionResult& result) {
    result.columns.clear();
    result.rows.clear();

    // Collect operators in root-to-leaf order
    std::vector<std::string> op_lines;
    std::function<void(const PhysicalOperator&)> collect;
    collect = [&](const PhysicalOperator& op) {
        op_lines.push_back(op.toString());
        for (const auto* child : op.children()) {
            collect(*child);
        }
    };
    collect(root);

    if (op_lines.empty()) {
        result.columns.push_back("Plan");
        return;
    }

    // Find max width for centering
    size_t max_len = 0;
    for (const auto& line : op_lines) {
        max_len = std::max(max_len, line.size());
    }

    result.columns.push_back("Plan");
    for (size_t i = 0; i < op_lines.size(); i++) {
        // Center the operator line
        size_t left_pad = (max_len - op_lines[i].size()) / 2;
        Row row;
        row.push_back(std::string(left_pad, ' ') + op_lines[i]);
        result.rows.push_back(std::move(row));

        // Insert down arrow between operators
        if (i + 1 < op_lines.size()) {
            size_t arrow_pad = max_len / 2;
            Row arrow_row;
            arrow_row.push_back(std::string(arrow_pad, ' ') + "\xe2\x86\x93");
            result.rows.push_back(std::move(arrow_row));
        }
    }
}

folly::coro::Task<void> QueryExecutor::handleIndexDdl(const IndexDdlStatement& stmt, ExecutionResult& result) {
    if (stmt.type == IndexDdlStatement::CREATE_VERTEX_INDEX) {
        auto label_def = co_await async_meta_.getLabelDef(stmt.label_name);
        if (!label_def.has_value()) {
            result.error = "Label not found: " + stmt.label_name;
            co_return;
        }
        uint16_t prop_id = 0;
        bool found = false;
        for (const auto& p : label_def->properties) {
            if (p.name == stmt.property_name) {
                prop_id = p.id;
                found = true;
                break;
            }
        }
        if (!found) {
            result.error = "Property not found: " + stmt.property_name;
            co_return;
        }

        bool ok =
            co_await async_meta_.createVertexIndex(stmt.index_name, stmt.label_name, stmt.property_name, stmt.unique);
        if (!ok) {
            result.error = "Failed to create index (duplicate name?)";
            co_return;
        }

        auto table = vidxTable(label_def->id, prop_id);
        ok = co_await async_data_.createIndex(table);
        if (!ok) {
            result.error = "Failed to create index storage table";
            co_return;
        }

        // Backfill: scan existing vertices and insert index entries
        {
            GraphTxnHandle txn = co_await async_data_.beginTran();
            async_data_.setTransaction(txn);

            auto gen = async_data_.scanVerticesByLabel(label_def->id);
            while (auto batch = co_await gen.next()) {
                for (auto vid : *batch) {
                    auto props = co_await async_data_.getVertexProperties(vid, label_def->id);
                    if (props.has_value() && prop_id < props->size() && (*props)[prop_id].has_value()) {
                        co_await async_data_.insertIndexEntry(table, (*props)[prop_id].value(), vid);
                    }
                }
            }

            co_await async_data_.commitTran(txn);
        }

        ok = co_await async_meta_.updateIndexState(stmt.index_name, IndexState::PUBLIC);
        if (!ok) {
            result.error = "Failed to set index state to PUBLIC";
            co_return;
        }

        result.columns.push_back("result");
        Row row;
        row.push_back(std::string("Index created: " + stmt.index_name));
        result.rows.push_back(std::move(row));

    } else if (stmt.type == IndexDdlStatement::CREATE_EDGE_INDEX) {
        bool ok =
            co_await async_meta_.createEdgeIndex(stmt.index_name, stmt.label_name, stmt.property_name, stmt.unique);
        if (!ok) {
            result.error = "Failed to create edge index";
            co_return;
        }
        result.columns.push_back("result");
        Row row;
        row.push_back(std::string("Edge index created: " + stmt.index_name));
        result.rows.push_back(std::move(row));

    } else if (stmt.type == IndexDdlStatement::DROP_INDEX) {
        bool ok = co_await async_meta_.dropIndex(stmt.index_name);
        if (!ok) {
            result.error = "Failed to drop index: " + stmt.index_name;
            co_return;
        }
        result.columns.push_back("result");
        Row row;
        row.push_back(std::string("Index dropped: " + stmt.index_name));
        result.rows.push_back(std::move(row));

    } else if (stmt.type == IndexDdlStatement::SHOW_INDEXES || stmt.type == IndexDdlStatement::SHOW_INDEX) {
        auto indexes = co_await async_meta_.listIndexes();
        result.columns = {"name", "label", "property", "unique", "state"};

        for (const auto& idx : indexes) {
            Row row;
            row.push_back(std::string(idx.name));
            row.push_back(std::string(idx.label_name));
            row.push_back(std::string(idx.property_name));
            row.push_back(idx.unique ? std::string("true") : std::string("false"));
            std::string state_str;
            switch (idx.state) {
            case IndexState::WRITE_ONLY:
                state_str = "WRITE_ONLY";
                break;
            case IndexState::PUBLIC:
                state_str = "PUBLIC";
                break;
            case IndexState::DELETE_ONLY:
                state_str = "DELETE_ONLY";
                break;
            default:
                state_str = "ERROR";
                break;
            }
            row.push_back(std::move(state_str));
            result.rows.push_back(std::move(row));
        }
    }
}

} // namespace compute
} // namespace eugraph
