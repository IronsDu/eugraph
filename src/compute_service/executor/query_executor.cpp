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

folly::coro::Task<std::shared_ptr<StreamContext>> QueryExecutor::prepareStream(const std::string& cypher_query) {
    auto ctx = std::make_shared<StreamContext>(async_data_);

    // 0. Quick guard: skip DDL if query starts with EXPLAIN (so DDL isn't executed for EXPLAIN queries)
    // The actual EXPLAIN handling is in the grammar/AST below.
    bool skip_ddl = false;
    {
        std::string trimmed = cypher_query;
        size_t s = trimmed.find_first_not_of(" \t\r\n");
        if (s != std::string::npos) {
            trimmed = trimmed.substr(s);
        }
        if (trimmed.size() >= 7) {
            std::string prefix = trimmed.substr(0, 7);
            std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::toupper);
            if (prefix == "EXPLAIN" && (trimmed.size() == 7 || std::isspace(static_cast<unsigned char>(trimmed[7])))) {
                skip_ddl = true;
            }
        }
    }

    // 0.5. Try index DDL
    if (!skip_ddl) {
        auto ddl_stmt = IndexDdlParser::tryParse(cypher_query);
        if (ddl_stmt.has_value()) {
            ExecutionResult ddl_result;
            co_await handleIndexDdl(*ddl_stmt, ddl_result);
            if (!ddl_result.error.empty()) {
                ctx->error = std::move(ddl_result.error);
                co_return ctx;
            }
            ctx->columns = std::move(ddl_result.columns);
            ctx->gen = folly::coro::co_invoke(
                [rows = std::move(ddl_result.rows)]() mutable -> folly::coro::AsyncGenerator<RowBatch> {
                    if (!rows.empty()) {
                        RowBatch batch;
                        batch.rows = std::move(rows);
                        co_yield std::move(batch);
                    }
                });
            co_return ctx;
        }
    }

    // 1. Parse (EXPLAIN handled via grammar/AST, not string manipulation)
    cypher::CypherQueryParser parser;
    auto parse_result = parser.parse(cypher_query);
    if (std::holds_alternative<cypher::ParseError>(parse_result)) {
        ctx->error = std::get<cypher::ParseError>(parse_result).message;
        co_return ctx;
    }
    auto stmt_var = std::move(std::get<cypher::Statement>(parse_result));

    // 1.5. Extract EXPLAIN flag from AST
    bool is_explain = false;
    cypher::Statement stmt;
    if (auto* es = std::get_if<std::unique_ptr<cypher::ExplainStatement>>(&stmt_var)) {
        is_explain = true;
        stmt = cypher::Statement(std::move((*es)->query));
    } else {
        stmt = std::move(stmt_var);
    }

    LogicalPlanBuilder plan_builder;
    auto logical_result = plan_builder.build(std::move(stmt));
    if (std::holds_alternative<std::string>(logical_result)) {
        ctx->error = std::get<std::string>(logical_result);
        co_return ctx;
    }
    auto& logical_plan = std::get<LogicalPlan>(logical_result);

    extractColumnsFromLogicalPlan(logical_plan.root, ctx->columns);

    // Load label/edge_label mappings from metadata service
    auto labels = co_await async_meta_.listLabels();
    auto edge_labels = co_await async_meta_.listEdgeLabels();

    std::unordered_map<std::string, LabelId> label_name_to_id;
    for (const auto& l : labels)
        label_name_to_id[l.name] = l.id;

    std::unordered_map<std::string, EdgeLabelId> edge_label_name_to_id;
    for (const auto& el : edge_labels)
        edge_label_name_to_id[el.name] = el.id;

    // Begin transaction
    GraphTxnHandle txn = co_await async_data_.beginTran();
    async_data_.setTransaction(txn);

    // Store label/edge_label defs + name→id maps in StreamContext so physical operator
    // raw pointers remain valid throughout streaming consumption
    for (const auto& l : labels)
        ctx->label_defs[l.id] = l;
    for (const auto& el : edge_labels)
        ctx->edge_label_defs[el.id] = el;
    ctx->label_name_to_id = std::move(label_name_to_id);
    ctx->edge_label_name_to_id = std::move(edge_label_name_to_id);

    // PlanContext references StreamContext's maps — physical operators get references to those maps
    PlanContext plan_ctx{
        .label_name_to_id = ctx->label_name_to_id,
        .edge_label_name_to_id = ctx->edge_label_name_to_id,
        .label_defs = ctx->label_defs,
        .edge_label_defs = ctx->edge_label_defs,
        .variable_vertex_ids = {},
        .variable_edge_ids = {},
    };

    plan_ctx.next_vertex_id = co_await async_meta_.nextVertexId();
    plan_ctx.next_edge_id = co_await async_meta_.nextEdgeId();

    PhysicalPlanner physical_planner;
    auto phys_result = physical_planner.plan(logical_plan, async_data_, plan_ctx);
    if (std::holds_alternative<std::string>(phys_result)) {
        ctx->error = std::get<std::string>(phys_result);
        co_await async_data_.rollbackTran(txn);
        co_return ctx;
    }
    auto& phys_op = std::get<std::unique_ptr<PhysicalOperator>>(phys_result);

    // 5.5. If EXPLAIN, format plan into generator without executing
    if (is_explain) {
        std::vector<std::string> op_lines;
        std::function<void(const PhysicalOperator&)> collect;
        collect = [&](const PhysicalOperator& op) {
            op_lines.push_back(op.toString());
            for (const auto* child : op.children()) {
                collect(*child);
            }
        };
        collect(*phys_op);

        ctx->columns.clear();
        if (op_lines.empty()) {
            ctx->columns.push_back("Plan");
        } else {
            size_t max_len = 0;
            for (const auto& line : op_lines) {
                max_len = std::max(max_len, line.size());
            }
            ctx->columns.push_back("Plan");
            std::vector<Row> plan_rows;
            for (size_t i = 0; i < op_lines.size(); i++) {
                size_t left_pad = (max_len - op_lines[i].size()) / 2;
                Row row;
                row.push_back(std::string(left_pad, ' ') + op_lines[i]);
                plan_rows.push_back(std::move(row));
                if (i + 1 < op_lines.size()) {
                    size_t arrow_pad = max_len / 2;
                    Row arrow_row;
                    arrow_row.push_back(std::string(arrow_pad, ' ') + "\xe2\x86\x93");
                    plan_rows.push_back(std::move(arrow_row));
                }
            }
            ctx->gen = folly::coro::co_invoke(
                [rows = std::move(plan_rows)]() mutable -> folly::coro::AsyncGenerator<RowBatch> {
                    if (!rows.empty()) {
                        RowBatch batch;
                        batch.rows = std::move(rows);
                        co_yield std::move(batch);
                    }
                });
        }

        co_await async_data_.rollbackTran(txn);
        ctx->should_commit = false;
        co_return ctx;
    }

    ctx->phys_op = std::move(phys_op);
    ctx->gen = ctx->phys_op->execute();
    ctx->txn = txn;

    co_return ctx;
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
            } else if constexpr (std::is_same_v<OpType, AggregateOp>) {
                for (const auto& name : ptr->output_names) {
                    columns.push_back(name);
                }
            } else if constexpr (std::is_same_v<OpType, LimitOp> || std::is_same_v<OpType, FilterOp> ||
                                 std::is_same_v<OpType, SortOp> || std::is_same_v<OpType, SkipOp> ||
                                 std::is_same_v<OpType, DistinctOp>) {
                if (!ptr->children.empty()) {
                    extractColumnsFromLogicalPlan(ptr->children[0], columns);
                }
            }
        },
        op);
}

folly::coro::Task<void> QueryExecutor::handleIndexDdl(const IndexDdlStatement& stmt, ExecutionResult& result) {
    if (stmt.type == IndexDdlStatement::CREATE_VERTEX_INDEX) {
        auto label_def = co_await async_meta_.getLabelDef(stmt.label_name);
        if (!label_def.has_value()) {
            result.error = "Label not found: " + stmt.label_name;
            co_return;
        }
        // Resolve all property names to property IDs
        std::vector<uint16_t> prop_ids;
        for (const auto& pn : stmt.property_names) {
            bool found = false;
            for (const auto& p : label_def->properties) {
                if (p.name == pn) {
                    prop_ids.push_back(p.id);
                    found = true;
                    break;
                }
            }
            if (!found) {
                result.error = "Property not found: " + pn;
                co_return;
            }
        }

        bool ok =
            co_await async_meta_.createVertexIndex(stmt.index_name, stmt.label_name, stmt.property_names, stmt.unique);
        if (!ok) {
            result.error = "Failed to create index (duplicate name?)";
            co_return;
        }

        auto table = vidxCompositeTable(label_def->id, prop_ids);
        ok = co_await async_data_.createIndex(table);
        if (!ok) {
            result.error = "Failed to create index storage table";
            co_return;
        }

        // Backfill: scan existing vertices and insert composite index entries
        bool hasConflict = false;
        {
            GraphTxnHandle txn = co_await async_data_.beginTran();
            async_data_.setTransaction(txn);

            {
                auto gen = async_data_.scanVerticesByLabel(label_def->id);
                while (auto batch = co_await gen.next()) {
                    for (auto vid : *batch) {
                        auto props_opt = co_await async_data_.getVertexProperties(vid, label_def->id);
                        if (!props_opt.has_value())
                            continue;
                        auto& props = *props_opt;
                        // Collect all indexed property values; skip if any is missing
                        std::vector<PropertyValue> values;
                        bool allPresent = true;
                        for (auto pid : prop_ids) {
                            if (pid < props.size() && props[pid].has_value()) {
                                values.push_back(props[pid].value());
                            } else {
                                allPresent = false;
                                break;
                            }
                        }
                        if (!allPresent)
                            continue;

                        if (stmt.unique) {
                            bool constraint_ok = co_await async_data_.checkUniqueConstraint(table, values);
                            if (!constraint_ok) {
                                spdlog::warn("Unique index '{}' backfill found duplicate value on vertex {}",
                                             stmt.index_name, vid);
                                hasConflict = true;
                                break;
                            }
                        }
                        co_await async_data_.insertIndexEntry(table, values, vid);
                    }
                    if (hasConflict)
                        break;
                }
            } // gen destroyed before commit

            co_await async_data_.commitTran(txn);
        }

        if (hasConflict) {
            ok = co_await async_meta_.updateIndexState(stmt.index_name, IndexState::ERROR);
            result.error = "Unique index creation failed: duplicate values found during backfill";
            co_return;
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
            co_await async_meta_.createEdgeIndex(stmt.index_name, stmt.label_name, stmt.property_names, stmt.unique);
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
            // Join property names with commas
            std::string prop_names_joined;
            for (size_t i = 0; i < idx.property_names.size(); ++i) {
                if (i > 0)
                    prop_names_joined += ", ";
                prop_names_joined += idx.property_names[i];
            }
            Row row;
            row.push_back(std::string(idx.name));
            row.push_back(std::string(idx.label_name));
            row.push_back(std::move(prop_names_joined));
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
