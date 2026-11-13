#include "query/executor/query_executor.hpp"
#include "common/config/env_vars.hpp"

#include "common/types/constants.hpp"
#include "query/catalog/catalog.hpp"
#include "query/function/function_registry.hpp"
#include "query/optimizer/logical_optimizer.hpp"
#include "query/parser/index_ddl_parser.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "storage/kv/value_codec.hpp"

#include <folly/coro/BlockingWait.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace eugraph {
namespace compute {

namespace {

/// Bound the WiredTiger transaction used by index backfill. A single
/// transaction over an sf1-sized label (millions of vertices) holds enough
/// dirty pages that eviction can force WT_ROLLBACK; the backfill is therefore
/// split into independent commit batches.
size_t indexBackfillBatchRows() {
    constexpr size_t kDefault = 20000;
    const char* env = std::getenv(config::kEnvIndexBackfillBatch);
    if (env == nullptr || *env == '\0')
        return kDefault;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(env, &end, 10);
    if (end == env || end == nullptr || *end != '\0' || parsed == 0)
        return kDefault;
    return static_cast<size_t>(std::min<unsigned long>(parsed, 1000000));
}

} // namespace

QueryExecutor::QueryExecutor(AsyncGraphDataStore& async_data, IAsyncGraphMetaStore& async_meta,
                             SessionPool& session_pool, Config config)
    : async_data_(async_data), async_meta_(async_meta), config_(config),
      compute_pool_(config.compute_pool ? std::move(config.compute_pool)
                                        : std::make_shared<folly::CPUThreadPoolExecutor>(config.compute_threads)),
      session_pool_(session_pool) {}

QueryExecutor::~QueryExecutor() = default;

folly::coro::Task<std::shared_ptr<StreamContext>>
QueryExecutor::prepareStream(const std::string& cypher_query, const std::unordered_map<std::string, Value>& params,
                             QueryCancel cancel, SessionRef existing_session) {
    auto ctx = std::make_shared<StreamContext>();
    ctx->query_text = cypher_query;
    const auto planning_start = std::chrono::steady_clock::now();

    // 0. Quick guard: skip DDL if query starts with EXPLAIN/PROFILE (so DDL isn't
    // executed for a plan/profile request). The actual prefix handling is in the
    // grammar/AST below.
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
            if ((prefix == "EXPLAIN" || prefix == "PROFILE") &&
                (trimmed.size() == 7 || std::isspace(static_cast<unsigned char>(trimmed[7])))) {
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
            ctx->gen = wrapRowsToChunkGenerator(std::move(ddl_result.rows));
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

    // 1.5. Extract EXPLAIN / PROFILE mode from AST
    bool is_explain = false;
    bool is_profile = false;
    cypher::Statement stmt;
    if (auto* es = std::get_if<std::unique_ptr<cypher::ExplainStatement>>(&stmt_var)) {
        is_explain = true;
        stmt = cypher::Statement(std::move((*es)->query));
    } else if (auto* ps = std::get_if<std::unique_ptr<cypher::ProfileStatement>>(&stmt_var)) {
        is_profile = true;
        stmt = cypher::Statement(std::move((*ps)->query));
    } else {
        stmt = std::move(stmt_var);
    }
    ctx->profile_requested = is_profile;

    // Load label/edge_label mappings from metadata service
    auto labels = co_await async_meta_.listLabels();
    auto edge_labels = co_await async_meta_.listEdgeLabels();

    std::unordered_map<std::string, LabelId> label_name_to_id;
    for (const auto& l : labels)
        label_name_to_id[l.name] = l.id;

    std::unordered_map<std::string, EdgeLabelId> edge_label_name_to_id;
    for (const auto& el : edge_labels)
        edge_label_name_to_id[el.name] = el.id;

    // Build catalog and function registry
    std::unordered_map<LabelId, LabelDef> label_defs_map;
    for (const auto& l : labels)
        label_defs_map[l.id] = l;
    std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_defs_map;
    for (const auto& el : edge_labels)
        edge_label_defs_map[el.id] = el;

    auto catalog = std::make_unique<catalog::Catalog>();
    catalog->load(std::move(label_defs_map), std::move(edge_label_defs_map));

    auto func_registry = std::make_unique<function::FunctionRegistry>();

    // 2. Bind: AST → BoundLogicalPlan
    binder::Binder binder(*catalog, *func_registry, params);
    auto bound_stmt = binder.bind(stmt);
    if (!bound_stmt) {
        std::string err = "Binding failed";
        for (const auto& e : binder.errors())
            err += "; " + e;
        ctx->error = std::move(err);
        co_return ctx;
    }

    // Extract output column names from the bound plan
    for (const auto& ci : bound_stmt->plan.output_schema) {
        ctx->columns.push_back(ci.name);
    }

    ctx->catalog = std::move(catalog);
    ctx->func_registry = std::move(func_registry);

    // 会话驱动（§2/§4）：取租约 → begin → 执行 → 由语句边界 commit/rollback ✓。
    // 显式事务（Bolt BEGIN…COMMIT）由调用方传入已开启的会话 ⇒ 本语句不 begin/commit ✓。
    SessionRef active_session = existing_session;
    const bool write = binder.ctx().has_mutation;
    if (!active_session) {
        auto lease = session_pool_.acquire();
        if (!lease) {
            ctx->error = "Failed to acquire a session (pool exhausted)";
            spdlog::error("[QueryExecutor] session pool exhausted; refusing to run '{}'", cypher_query);
            co_return ctx;
        }
        GraphSession& session = **lease;
        active_session = std::ref(session);
        ctx->session = std::move(*lease); // 租约随 ctx 存活；析构 ⇒ 池 put（未提交自动回滚 ✓）
        // Construct the facade before begin so transaction control can be
        // dispatched to Storage IO too (begin is a WT call on the session).
        ctx->query_store.emplace(async_data_.for_session(session));
        const bool begun = co_await ctx->query_store->beginSession(write);
        if (!begun) {
            ctx->error = "Failed to begin transaction";
            spdlog::error("[QueryExecutor] begin_transaction failed; refusing to run '{}'", cypher_query);
            co_return ctx;
        }
    } else if (!active_session->get().active()) {
        ctx->error = "Explicit transaction session is not active";
        co_return ctx;
    } else {
        ctx->should_commit = false; // 显式事务：语句不提交，由 BEGIN/COMMIT 边界负责 ✓
    }

    // Statement execution context: the one place statement-scoped state (today the
    // cancellation flag, later deadlines/budgets) belongs. Operators hold it through
    // a shared_ptr, and the store stays unaware of it -- cancellation is observed on
    // the operator side, where upstream chunks are consumed.
    auto query_ctx = std::make_shared<QueryContext>(std::move(cancel), is_profile);
    ctx->query_context = query_ctx;
    // 显式事务分支：会话已由 BEGIN 开启，这里只构造绑定门面；自动提交分支已在上面构造。
    if (!ctx->query_store)
        ctx->query_store.emplace(async_data_.for_session(active_session->get()));
    // Give the store the statement's "current operator counters" slot: traced
    // operators install their counters there while they run (#310).
    ctx->query_store->setProfileState(query_ctx->profileState());
    if (is_profile)
        ctx->query_store->setProfileCounters(query_ctx->statementCounters());
    IAsyncGraphDataStore& query_data = *ctx->query_store;

    // Store label/edge_label defs + name→id maps in StreamContext so physical operator
    // raw pointers remain valid throughout streaming consumption
    for (const auto& l : labels)
        ctx->label_defs[l.id] = l;
    for (const auto& el : edge_labels)
        ctx->edge_label_defs[el.id] = el;
    ctx->label_name_to_id = std::move(label_name_to_id);
    ctx->edge_label_name_to_id = std::move(edge_label_name_to_id);
    ctx->label_vertex_counts = async_meta_.schema().label_vertex_counts;

    PlanContext plan_ctx{
        .label_name_to_id = ctx->label_name_to_id,
        .edge_label_name_to_id = ctx->edge_label_name_to_id,
        .label_defs = ctx->label_defs,
        .edge_label_defs = ctx->edge_label_defs,
        .label_vertex_counts = ctx->label_vertex_counts,
        .eval_ctx = {},
        .requirements = {},
        .extraction_info = {},
        .var_slots = {},
        .scoped_var_slots = {},
        .label_order_by_name = {},
        .alias_map = {},
        .slot_allocator = {},
        .fresh_expands = {},
        .static_prune_hints = {},
        .func_registry = ctx->func_registry.get(),
        .expand_allowed_filter = {},
        .filtered_expand = nullptr,
        .deferred_slots = std::nullopt,
        .anchor_edge_var = {},
    };

    plan_ctx.eval_ctx.catalog = ctx->catalog.get();
    plan_ctx.eval_ctx.label_defs = &ctx->label_defs;
    // Static property pruning is only valid when the statement itself cannot
    // create labels/properties between bind time and execution time.
    plan_ctx.eval_ctx.allow_static_schema_pruning = !binder.ctx().has_mutation;

    // Populate variable → SlotId mapping from the Binder's symbol table.
    // This covers ALL variables (including intermediate anon edges/nodes),
    // not just the RETURN-level output schema.
    for (const auto& [name, info] : binder.ctx().symbols) {
        if (info.slot_id != binder::INVALID_SLOT_ID)
            plan_ctx.var_slots[name] = info.slot_id;
    }
    // Also seed from the binder's ordered binding log, but only for names
    // absent from the final symbol table. WITH clauses narrow ctx().symbols
    // to their outputs, and inner scopes may bind the same name to a fresh
    // slot; neither must overwrite the visible outer binding.
    for (const auto& binding : binder.ctx().binding_order) {
        if (binding.slot != binder::INVALID_SLOT_ID && plan_ctx.var_slots.count(binding.name) == 0)
            plan_ctx.var_slots[binding.name] = binding.slot;
    }
    // Scope-aware records for DPL: (scope, name) → slot.
    plan_ctx.scoped_var_slots = binder.ctx().scoped_bindings;
    // Query-time label presentation order (source_labels are already in
    // pattern order for bound node variables).
    for (const auto& [name, info] : binder.ctx().symbols) {
        if (!info.source_labels.empty())
            plan_ctx.label_order_by_name[name] = info.source_labels;
    }
    // Seed the planner's slot allocator to continue after the binder's slots.
    // Start from the next slot after the binder's allocation.
    ctx->label_order = plan_ctx.label_order_by_name;
    plan_ctx.slot_allocator.seed(binder.ctx().slot_allocator.current());

    // 2.5. Logical optimization. Projection pushdown is a pre-Cascades
    // optimizer pass: the binder collected semantic property references, the
    // optimizer turns them into scan/expand property demand.
    optimizer::LogicalOptimizer logical_optimizer;
    logical_optimizer.optimize(bound_stmt->plan, ctx->catalog.get(), &ctx->label_vertex_counts);

    // 3. Physical planning. Try CBO-chosen plan first (Phase 4); fall back
    // to planBound (RBO over the optimized logical tree) when no winner.
    PhysicalPlanner physical_planner;
    std::variant<std::unique_ptr<PhysicalOperator>, std::string> phys_result = std::string("");
    if (bound_stmt->plan.chosen) {
        phys_result = physical_planner.planChosen(*bound_stmt->plan.chosen, query_data, async_meta_, plan_ctx);
        if (std::holds_alternative<std::string>(phys_result)) {
            // planChosen failed — log and fall through to planBound rather than
            // aborting the whole query. The RBO path produces a known-good plan.
            spdlog::warn("[executor] planChosen failed ({}); falling back to planBound",
                         std::get<std::string>(phys_result));
            phys_result = std::string("");
        }
    }
    if (!bound_stmt->plan.chosen || std::holds_alternative<std::string>(phys_result)) {
        phys_result = physical_planner.planBound(bound_stmt->plan, query_data, async_meta_, plan_ctx);
    }
    if (std::holds_alternative<std::string>(phys_result)) {
        ctx->error = std::get<std::string>(phys_result);
        ctx->rollback();
        co_return ctx;
    }
    auto& phys_op = std::get<std::unique_ptr<PhysicalOperator>>(phys_result);

    // 5.5. If EXPLAIN, format plan into generator without executing
    if (is_explain) {
        // Build output schema description for an operator
        auto formatOutput = [](const PhysicalOperator& op) -> std::string {
            const auto& schema = op.outputSchema();
            const auto& types = op.outputTypes();
            if (schema.empty())
                return "  output: []";
            std::string result = "  output: [";
            for (size_t i = 0; i < schema.size(); ++i) {
                if (i > 0)
                    result += ", ";
                result += schema[i] + ":" + types[i].toString();
            }
            result += "]";
            return result;
        };

        // Collect operator info: toString + output schema for each operator
        struct OpInfo {
            std::string to_string;
            std::string output;
        };
        std::vector<OpInfo> ops;
        std::function<void(const PhysicalOperator&)> collect;
        collect = [&](const PhysicalOperator& op) {
            ops.push_back({op.toString(), formatOutput(op)});
            for (const auto* child : op.children()) {
                collect(*child);
            }
        };
        collect(*phys_op);

        ctx->columns.clear();
        ctx->columns.push_back("Plan");

        // Calculate box width from all lines
        size_t box_width = 0;
        for (const auto& op : ops) {
            box_width = std::max(box_width, op.to_string.size());
            box_width = std::max(box_width, op.output.size());
        }
        box_width += 2; // padding inside box

        std::vector<Row> plan_rows;
        for (size_t i = 0; i < ops.size(); i++) {
            // Top border
            Row top_row;
            top_row.push_back("+" + std::string(box_width, '-') + "+");
            plan_rows.push_back(std::move(top_row));

            // Operator name line
            Row name_row;
            name_row.push_back("| " + ops[i].to_string + std::string(box_width - 1 - ops[i].to_string.size(), ' ') +
                               "|");
            plan_rows.push_back(std::move(name_row));

            // Output schema line
            Row out_row;
            out_row.push_back("| " + ops[i].output + std::string(box_width - 1 - ops[i].output.size(), ' ') + "|");
            plan_rows.push_back(std::move(out_row));

            // Bottom border
            Row bot_row;
            bot_row.push_back("+" + std::string(box_width, '-') + "+");
            plan_rows.push_back(std::move(bot_row));

            // Arrow between operators
            if (i + 1 < ops.size()) {
                size_t arrow_pad = box_width / 2;
                Row arrow_row;
                arrow_row.push_back(std::string(arrow_pad, ' ') + "\xe2\x86\x93");
                plan_rows.push_back(std::move(arrow_row));
            }
        }

        ctx->gen = wrapRowsToChunkGenerator(std::move(plan_rows));

        ctx->rollback();
        ctx->should_commit = false;
        co_return ctx;
    }

    ctx->phys_op = std::move(phys_op);
    // One attach call reaches the whole tree (base class recurses over children).
    ctx->phys_op->setQueryContext(ctx->query_context);
    ctx->gen = ctx->phys_op->executeChunkTraced();
    ctx->planning_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - planning_start)
            .count());

    co_return ctx;
}

folly::coro::Task<void> QueryExecutor::handleIndexDdl(const IndexDdlStatement& stmt, ExecutionResult& result) {
    if (stmt.type == IndexDdlStatement::CREATE_VERTEX_INDEX) {
        auto label_def = co_await async_meta_.getLabelDef(stmt.label_name);
        if (!label_def.has_value()) {
            result.error = "Label not found: " + stmt.label_name;
            co_return;
        }

        std::vector<IndexAccessorDef> accessors;
        for (const auto& src : stmt.accessors) {
            IndexAccessorDef acc;
            acc.is_strong = src.is_strong;
            acc.property_name = src.property_name;
            if (src.is_strong) {
                auto source_opt = co_await async_meta_.getLabelId(src.source_label);
                if (!source_opt.has_value()) {
                    result.error = "Source label not found: " + src.source_label;
                    co_return;
                }
                acc.source_label_id = *source_opt;
            }
            accessors.push_back(std::move(acc));
        }
        if (accessors.empty()) {
            result.error = "Index has no property accessors";
            co_return;
        }

        // A weak accessor (`ON (n.p)` without ::Label) resolves against every
        // label that declares p. If NO label declares it, the index would be
        // created successfully but stay empty forever -- a silent no-op that
        // only shows up as wrong (slow / empty) query results. Reject it here.
        {
            const auto& schema = async_meta_.schema();
            for (const auto& acc : accessors) {
                if (acc.is_strong)
                    continue;
                bool declared = false;
                for (const auto& [lid, ldef] : schema.labels) {
                    if (ldef.name == kAnonLabelName)
                        continue;
                    for (const auto& pd : ldef.properties) {
                        if (pd.name == acc.property_name) {
                            declared = true;
                            break;
                        }
                    }
                    if (declared)
                        break;
                }
                if (!declared) {
                    result.error = "Property '" + acc.property_name +
                                   "' is not declared by any label; create it with DDL before indexing it.";
                    co_return;
                }
            }
        }

        bool ok = co_await async_meta_.createVertexIndexWithAccessors(stmt.index_name, stmt.label_name, accessors,
                                                                      stmt.unique);
        if (!ok) {
            result.error = "Failed to create index (duplicate name?)";
            co_return;
        }

        const auto& schema = async_meta_.schema();
        auto idx_def = schema.findIndexByName(stmt.index_name);
        if (!idx_def) {
            result.error = "Created index not found in schema: " + stmt.index_name;
            co_return;
        }

        auto table = vidxTableById(idx_def->index_id);
        ok = co_await async_data_.createIndex(table);
        if (!ok) {
            result.error = "Failed to create index storage table";
            co_return;
        }

        // Pre-resolve strong accessors to prop ids.
        struct ResolvedAccessor {
            bool is_strong;
            LabelId source_label_id;
            uint16_t source_prop_id;
            std::string property_name;
        };
        std::vector<ResolvedAccessor> resolved;
        for (const auto& acc : idx_def->accessors) {
            ResolvedAccessor ra;
            ra.is_strong = acc.is_strong;
            ra.property_name = acc.property_name;
            ra.source_label_id = acc.source_label_id;
            ra.source_prop_id = UINT16_MAX;
            if (acc.is_strong) {
                auto src = schema.getLabel(acc.source_label_id);
                if (!src) {
                    result.error = "Source label not found for index '" + stmt.index_name + "'";
                    co_return;
                }
                bool found = false;
                for (const auto& pd : src->properties) {
                    if (pd.name == acc.property_name) {
                        ra.source_prop_id = pd.id;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    result.error =
                        "Property not found in source label for index '" + stmt.index_name + "': " + acc.property_name;
                    co_return;
                }
            }
            resolved.push_back(std::move(ra));
        }

        // Backfill in bounded transactions. A single transaction over an
        // sf1-sized label (millions of vertices) holds enough dirty pages that
        // eviction can force WT_ROLLBACK. Commit each batch independently so
        // eviction can make progress between batches.
        bool hasConflict = false;
        bool backfillOk = true;
        {
            auto scan_lease = session_pool_.acquire();
            auto write_lease = session_pool_.acquire();
            if (!scan_lease || !write_lease) {
                backfillOk = false;
                result.error = "Failed to acquire sessions for index backfill (pool exhausted)";
            } else {
                auto scan_store = async_data_.for_session(**scan_lease);
                auto write_store = async_data_.for_session(**write_lease);
                {
                    // Do not keep a long-lived read transaction open across
                    // the whole backfill: it would pin the oldest snapshot and
                    // defeat eviction just like the oversized write txn. The
                    // scan cursor runs in autocommit; write batches use a
                    // separate session.
                    const size_t batch_rows = indexBackfillBatchRows();
                    std::vector<VertexId> pending;
                    pending.reserve(batch_rows);

                    auto flush_batch = [&]() -> folly::coro::Task<bool> {
                        if (pending.empty())
                            co_return true;
                        if (!co_await write_store.beginSession(true)) {
                            result.error = "Failed to begin index backfill transaction";
                            co_return false;
                        }
                        for (auto vid : pending) {
                            std::vector<PropertyValue> values;
                            bool allPresent = true;
                            bool conflict = false;

                            auto vertex_labels = co_await write_store.getVertexLabels(vid);
                            for (const auto& ra : resolved) {
                                if (ra.is_strong) {
                                    auto props_opt = co_await write_store.getVertexProperties(vid, ra.source_label_id);
                                    if (!props_opt || ra.source_prop_id >= props_opt->size() ||
                                        !(*props_opt)[ra.source_prop_id].has_value()) {
                                        allPresent = false;
                                        break;
                                    }
                                    values.push_back((*props_opt)[ra.source_prop_id].value());
                                } else {
                                    std::optional<PropertyValue> found_value;
                                    for (LabelId lid : vertex_labels) {
                                        auto lab = schema.getLabel(lid);
                                        if (!lab)
                                            continue;
                                        uint16_t pid = UINT16_MAX;
                                        for (const auto& pd : lab->properties) {
                                            if (pd.name == ra.property_name) {
                                                pid = pd.id;
                                                break;
                                            }
                                        }
                                        if (pid == UINT16_MAX)
                                            continue;
                                        auto props_opt = co_await write_store.getVertexProperties(vid, lid);
                                        if (!props_opt || pid >= props_opt->size() || !(*props_opt)[pid].has_value())
                                            continue;
                                        const auto& candidate = (*props_opt)[pid].value();
                                        if (found_value.has_value()) {
                                            if (!(found_value.value() == candidate)) {
                                                conflict = true;
                                                break;
                                            }
                                        } else {
                                            found_value = candidate;
                                        }
                                    }
                                    if (conflict) {
                                        spdlog::warn(
                                            "Index '{}' weak accessor '{}' has conflicting values on vertex {}",
                                            stmt.index_name, ra.property_name, vid);
                                        break;
                                    }
                                    if (!found_value.has_value()) {
                                        allPresent = false;
                                        break;
                                    }
                                    values.push_back(found_value.value());
                                }
                            }

                            if (conflict) {
                                hasConflict = true;
                                break;
                            }
                            if (!allPresent)
                                continue;

                            if (stmt.unique) {
                                bool constraint_ok = co_await write_store.checkUniqueConstraint(table, values);
                                if (!constraint_ok) {
                                    spdlog::warn("Unique index '{}' backfill found duplicate value on vertex {}",
                                                 stmt.index_name, vid);
                                    hasConflict = true;
                                    break;
                                }
                            }
                            co_await write_store.insertIndexEntry(table, values, vid);
                        }
                        if (hasConflict) {
                            co_await write_store.rollbackSession();
                            co_return false;
                        }
                        const auto outcome = co_await write_store.commitSession();
                        if (!outcome.committed()) {
                            // 提交失败绝不能继续往下走：索引会被标成 PUBLIC 而条目为空，
                            // 规划器随后选中它 ⇒ 静默漏结果（空索引比没有索引更危险）。
                            result.error =
                                "Index backfill transaction failed to commit (index left in ERROR, not ONLINE); "
                                "see the server log for the WiredTiger error";
                            co_return false;
                        }
                        pending.clear();
                        co_return true;
                    };

                    {
                        std::vector<VertexId> scan_batch;
                        auto gen = scan_store.scanVerticesByLabel(label_def->id, scan_batch);
                        while (backfillOk && !hasConflict) {
                            auto batch = co_await gen.next();
                            if (!batch)
                                break;
                            for (auto vid : *batch) {
                                pending.push_back(vid);
                                if (pending.size() < batch_rows)
                                    continue;
                                if (!co_await flush_batch()) {
                                    backfillOk = false;
                                    break;
                                }
                            }
                        }
                    } // gen destroyed before ending the read transaction
                    if (backfillOk && !hasConflict && !pending.empty())
                        backfillOk = co_await flush_batch();
                }
            }
        }

        if (!backfillOk || hasConflict) {
            ok = co_await async_meta_.updateIndexState(stmt.index_name, IndexState::ERROR);
            if (result.error.empty())
                result.error = hasConflict
                                   ? "Index creation failed: conflicting values or duplicate values during backfill"
                                   : "Index backfill failed (index left in ERROR, not ONLINE)";
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
        auto edge_label_def = co_await async_meta_.getEdgeLabelDef(stmt.label_name);
        if (!edge_label_def.has_value()) {
            result.error = "Edge label not found: " + stmt.label_name;
            co_return;
        }
        // Resolve all property names to property IDs
        std::vector<uint16_t> prop_ids;
        std::vector<std::string> prop_names;
        for (const auto& acc : stmt.accessors) {
            const auto& pn = acc.property_name;
            bool found = false;
            for (const auto& p : edge_label_def->properties) {
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
            prop_names.push_back(pn);
        }

        bool ok = co_await async_meta_.createEdgeIndex(stmt.index_name, stmt.label_name, prop_names, stmt.unique);
        if (!ok) {
            result.error = "Failed to create edge index (duplicate name?)";
            co_return;
        }

        auto table = eidxCompositeTable(edge_label_def->id, prop_ids);
        ok = co_await async_data_.createIndex(table);
        if (!ok) {
            result.error = "Failed to create edge index storage table";
            co_return;
        }

        // Backfill in bounded transactions (same reason as vertex indexes:
        // a single sf1-sized transaction can be forced to WT_ROLLBACK).
        bool hasConflict = false;
        bool backfillOk = true;
        {
            auto scan_lease = session_pool_.acquire();
            auto write_lease = session_pool_.acquire();
            if (!scan_lease || !write_lease) {
                backfillOk = false;
                result.error = "Failed to acquire sessions for index backfill (pool exhausted)";
            } else {
                auto scan_store = async_data_.for_session(**scan_lease);
                auto write_store = async_data_.for_session(**write_lease);
                {
                    // Do not keep a long-lived read transaction open across
                    // the whole backfill: it would pin the oldest snapshot and
                    // defeat eviction just like the oversized write txn. The
                    // scan cursor runs in autocommit; write batches use a
                    // separate session.
                    const size_t batch_rows = indexBackfillBatchRows();
                    std::vector<ISyncGraphDataStore::EdgeTypeIndexEntry> pending;
                    pending.reserve(batch_rows);

                    auto flush_batch = [&]() -> folly::coro::Task<bool> {
                        if (pending.empty())
                            co_return true;
                        if (!co_await write_store.beginSession(true)) {
                            result.error = "Failed to begin index backfill transaction";
                            co_return false;
                        }
                        for (const auto& entry : pending) {
                            auto props_opt = co_await write_store.getEdgeProperties(edge_label_def->id, entry.edge_id);
                            if (!props_opt.has_value())
                                continue;
                            auto& props = *props_opt;
                            // Collect all indexed property values; skip if any is missing.
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
                                bool constraint_ok = co_await write_store.checkUniqueConstraint(table, values);
                                if (!constraint_ok) {
                                    spdlog::warn("Unique edge index '{}' backfill found duplicate value on edge {}",
                                                 stmt.index_name, entry.edge_id);
                                    hasConflict = true;
                                    break;
                                }
                            }
                            auto adj_value = ValueCodec::encodeEdgeAdjacency(entry.src_vertex_id, entry.dst_vertex_id,
                                                                             edge_label_def->id);
                            co_await write_store.insertIndexEntry(table, values, entry.edge_id, std::move(adj_value));
                        }
                        if (hasConflict) {
                            co_await write_store.rollbackSession();
                            co_return false;
                        }
                        const auto outcome = co_await write_store.commitSession();
                        if (!outcome.committed()) {
                            // 提交失败绝不能继续往下走：索引会被标成 PUBLIC 而条目为空，
                            // 规划器随后选中它 ⇒ 静默漏结果（空索引比没有索引更危险）。
                            result.error =
                                "Index backfill transaction failed to commit (index left in ERROR, not ONLINE); "
                                "see the server log for the WiredTiger error";
                            co_return false;
                        }
                        pending.clear();
                        co_return true;
                    };

                    {
                        std::vector<ISyncGraphDataStore::EdgeTypeIndexEntry> scan_batch;
                        auto gen =
                            scan_store.scanEdgesByType(edge_label_def->id, std::nullopt, std::nullopt, scan_batch);
                        while (backfillOk && !hasConflict) {
                            auto batch = co_await gen.next();
                            if (!batch)
                                break;
                            for (const auto& entry : *batch) {
                                pending.push_back(entry);
                                if (pending.size() < batch_rows)
                                    continue;
                                if (!co_await flush_batch()) {
                                    backfillOk = false;
                                    break;
                                }
                            }
                        }
                    } // gen destroyed before ending the read transaction
                    if (backfillOk && !hasConflict && !pending.empty())
                        backfillOk = co_await flush_batch();
                }
            }
        }

        if (!backfillOk || hasConflict) {
            ok = co_await async_meta_.updateIndexState(stmt.index_name, IndexState::ERROR);
            if (result.error.empty())
                result.error = hasConflict ? "Unique edge index creation failed: duplicate values found during backfill"
                                           : "Edge index backfill failed (index left in ERROR, not ONLINE)";
            co_return;
        }

        ok = co_await async_meta_.updateIndexState(stmt.index_name, IndexState::PUBLIC);
        if (!ok) {
            result.error = "Failed to set edge index state to PUBLIC";
            co_return;
        }

        result.columns.push_back("result");
        Row row;
        row.push_back(std::string("Edge index created: " + stmt.index_name));
        result.rows.push_back(std::move(row));

    } else if (stmt.type == IndexDdlStatement::DROP_INDEX) {
        const auto& schema = async_meta_.schema();
        auto idx_def = schema.findIndexByName(stmt.index_name);
        std::string new_table;
        if (idx_def && idx_def->index_id != 0)
            new_table = vidxTableById(idx_def->index_id);

        bool ok = co_await async_meta_.dropIndex(stmt.index_name);
        if (!ok) {
            result.error = "Failed to drop index: " + stmt.index_name;
            co_return;
        }
        if (!new_table.empty())
            co_await async_data_.dropIndex(new_table);
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
