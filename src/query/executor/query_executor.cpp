#include "query/executor/query_executor.hpp"

#include <thread>

#include "storage/index/index_build_gate.hpp"
#include "storage/index/index_build_service.hpp"

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

namespace eugraph {
namespace compute {

QueryExecutor::QueryExecutor(IAsyncGraphDataStore& async_data, IAsyncGraphMetaStore& async_meta, Config config)
    : async_data_(async_data), async_meta_(async_meta), config_(config),
      compute_pool_(std::make_shared<folly::CPUThreadPoolExecutor>(config.compute_threads)) {}

QueryExecutor::~QueryExecutor() = default;

folly::coro::Task<std::shared_ptr<StreamContext>>
QueryExecutor::prepareStream(const std::string& cypher_query, const std::unordered_map<std::string, Value>& params,
                             QueryCancel cancel) {
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

    // 1.5. Extract EXPLAIN flag from AST
    bool is_explain = false;
    cypher::Statement stmt;
    if (auto* es = std::get_if<std::unique_ptr<cypher::ExplainStatement>>(&stmt_var)) {
        is_explain = true;
        stmt = cypher::Statement(std::move((*es)->query));
    } else {
        stmt = std::move(stmt_var);
    }

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

    // Begin transaction. Fork a dedicated store wrapper so concurrent
    // queries can never overwrite each other's transaction handle.
    GraphTxnHandle txn = co_await async_data_.beginTran();
    if (txn == INVALID_GRAPH_TXN) {
        // Fail loudly. Continuing would run the query through the store's shared
        // default session in autocommit mode: writes would land one row at a time
        // with nothing to roll back, which is exactly what a transaction is for.
        ctx->error = "Failed to begin transaction";
        spdlog::error("[QueryExecutor] beginTran failed; refusing to run '{}'", cypher_query);
        co_return ctx;
    }
    // Statement execution context: the one place statement-scoped state (today the
    // cancellation flag, later deadlines/budgets) belongs. Operators hold it through
    // a shared_ptr, and the store stays unaware of it -- cancellation is observed on
    // the operator side, where upstream chunks are consumed.
    auto query_ctx = std::make_shared<QueryContext>(std::move(cancel));
    ctx->query_context = query_ctx;
    auto query_store = async_data_.forkTransaction(txn);
    IAsyncGraphDataStore& query_data = *query_store;
    ctx->query_store = std::move(query_store);

    // Store label/edge_label defs + name→id maps in StreamContext so physical operator
    // raw pointers remain valid throughout streaming consumption
    for (const auto& l : labels)
        ctx->label_defs[l.id] = l;
    for (const auto& el : edge_labels)
        ctx->edge_label_defs[el.id] = el;
    ctx->label_name_to_id = std::move(label_name_to_id);
    ctx->edge_label_name_to_id = std::move(edge_label_name_to_id);

    PlanContext plan_ctx{
        .label_name_to_id = ctx->label_name_to_id,
        .edge_label_name_to_id = ctx->edge_label_name_to_id,
        .label_defs = ctx->label_defs,
        .edge_label_defs = ctx->edge_label_defs,
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

    // 2.5. Logical optimization
    optimizer::LogicalOptimizer logical_optimizer;
    logical_optimizer.optimize(bound_stmt->plan, ctx->catalog.get());

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
        co_await query_data.rollbackTran(txn);
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

        co_await query_data.rollbackTran(txn);
        ctx->should_commit = false;
        co_return ctx;
    }

    ctx->phys_op = std::move(phys_op);
    // One attach call reaches the whole tree (base class recurses over children).
    ctx->phys_op->setQueryContext(ctx->query_context);
    ctx->gen = ctx->phys_op->executeChunk();
    ctx->txn = txn;

    co_return ctx;
}

void QueryExecutor::setIndexBuildService(std::shared_ptr<IndexBuildService> service) {
    index_builds_ = std::move(service);
}

namespace {
/// 待回收的孤儿表（DROP 时删表失败留下的表）。DROP 是幂等的，因此在**后续每次 DROP** 时机会式重试回收——
/// 无需新增持久化（进程重启后遗留的孤儿表由后续 §20.18 B 的持久化登记方案处理）。
std::mutex& orphanMu() {
    static std::mutex m;
    return m;
}
std::vector<std::string>& orphanTables() {
    static std::vector<std::string> v;
    return v;
}
void rememberOrphan(const std::string& table) {
    std::lock_guard<std::mutex> lock(orphanMu());
    auto& v = orphanTables();
    if (std::find(v.begin(), v.end(), table) == v.end())
        v.push_back(table);
}
std::vector<std::string> takeOrphans() {
    std::lock_guard<std::mutex> lock(orphanMu());
    auto v = orphanTables();
    orphanTables().clear();
    return v;
}
} // namespace

folly::coro::Task<bool> QueryExecutor::catchUpAndPublish(const std::string& index_table,
                                                         const std::string& index_name) {
    const auto& schema_now = async_meta_.schema();
    auto def_now = schema_now.findIndexByName(index_name);
    if (!def_now || def_now->index_id == 0)
        co_return true; // 定义已消失（例如构建中被 DROP）⇒ 无需追赶
    const std::string delta_table = idxDeltaTable(def_now->index_id);
    constexpr size_t kReplayBatch = 1024;

    // ⓪ **关闸并等在飞写者退出**（设计 §7.1/§15.1）：此后到达的写者会直写索引（不再进变更表），
    // 因此"追平 + 翻 PUBLIC"之后不会再有落在变更表里的写入 ⇒ 丢写窗口被彻底封死。
    auto gate = IndexBuildGateRegistry::instance().gate(def_now->index_id);
    if (!gate->closeAndWait(5000))
        spdlog::warn("[index-build] '{}' 关闸等待在飞写者超时（窗口可能变大，但不会静默丢写）", index_name);

    // ① 追平（多轮；每轮从表首开始、轮内 last_key 续扫；应用后从变更表删除 ⇒ 可终止）
    for (int pass = 0; pass < 50; ++pass) {
        size_t applied_total = 0;
        std::string last_key;
        while (true) {
            const size_t applied =
                co_await async_data_.replayDeltaBatch(index_table, delta_table, kReplayBatch, last_key);
            applied_total += applied;
            if (applied < kReplayBatch)
                break;
        }
        if (applied_total == 0)
            break;
    }

    // ② **先翻 PUBLIC**：此后新写入直写索引、不再进变更表（关闸的近似实现）
    if (!(co_await async_meta_.updateIndexState(index_name, IndexState::PUBLIC)))
        co_return false;

    // ③ **再排空一次**：收走"翻状态瞬间仍在飞"的写入
    {
        std::string last_key;
        while (true) {
            const size_t applied =
                co_await async_data_.replayDeltaBatch(index_table, delta_table, kReplayBatch, last_key);
            if (applied < kReplayBatch)
                break;
        }
    }
    co_return true;
}

folly::coro::Task<IndexBuildResult> QueryExecutor::backfillVertexIndex(IndexDdlStatement stmt, std::string table,
                                                                       LabelId label_id,
                                                                       std::vector<ResolvedIndexAccessor> resolved,
                                                                       std::function<bool()> cancelled) {
    // 顶点索引回填 + 提交。**不落状态**：由调用方（同步路径）或发布回调（后台路径）落，
    // 以保持"先提交数据、后翻状态"的两库顺序（设计 §7.3）。
    // 弱 accessor 需要按属性名在各标签里查 prop id ⇒ 取当前 schema（构建与模式 DDL 互斥，§8）。
    const auto& schema = async_meta_.schema();
    bool hasConflict = false;
    bool cancelled_midway = false;
    {
        GraphTxnHandle txn = co_await async_data_.beginTran();
        async_data_.setTransaction(txn);

        {
            auto gen = async_data_.scanVerticesByLabel(label_id);
            while (auto batch = co_await gen.next()) {
                if (cancelled && cancelled()) {
                    cancelled_midway = true;
                    break; // 顶点侧原先没有取消检查（补齐）
                }
                for (auto vid : *batch) {
                    std::vector<PropertyValue> values;
                    bool allPresent = true;
                    bool conflict = false;

                    auto vertex_labels = co_await async_data_.getVertexLabels(vid);
                    for (const auto& ra : resolved) {
                        if (ra.is_strong) {
                            auto props_opt = co_await async_data_.getVertexProperties(vid, ra.source_label_id);
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
                                auto props_opt = co_await async_data_.getVertexProperties(vid, lid);
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
                                spdlog::warn("Index '{}' weak accessor '{}' has conflicting values on vertex {}",
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

        if (cancelled_midway) {
            // **显式同步回滚**：只置取消位就返回会让索引表仍被本会话占用 ⇒ DROP 删表失败
            // （"Device or resource busy"）。同步回滚确保任务退出时表已释放。
            async_data_.rollbackTranNow(txn);
            co_return IndexBuildResult{IndexBuildOutcome::CANCELLED, "cancelled during backfill"};
        }

        // 提交失败绝不能继续往下走：索引会被标成 PUBLIC 而条目为空，
        // 规划器随后选中它 ⇒ 静默漏结果（空索引比没有索引更危险）。
        const bool committed = co_await async_data_.commitTran(txn);
        if (!committed) {
            co_return IndexBuildResult{IndexBuildOutcome::ERROR,
                                       "Index backfill transaction failed to commit (index left in ERROR, not "
                                       "ONLINE); see the server log for the WiredTiger error"};
        }
    }

    if (hasConflict) {
        co_return IndexBuildResult{IndexBuildOutcome::ERROR,
                                   "Index creation failed: conflicting values or duplicate values during backfill"};
    }

    // ==================== P2 追赶：重放构建期间的变更表（§6.1）====================
    // 构建期（BUILDING）的维护写入不再直写索引，而是进变更表 ⇒ 这里按键序、分批把它追平。
    // **注意（已知边界）**：尚未实现 §7.1 的"关闸 + 排空在飞写者"，因此**极窄窗口内**与最后一批重放竞争的
    // 写入仍可能丢失；已用"多轮直到某轮应用 0 行"缩小窗口，完整协议见设计 §20.2 第 2 步。
    {
        const auto& schema_now = async_meta_.schema();
        auto def_now = schema_now.findIndexByName(stmt.index_name);
        if (def_now && def_now->index_id != 0) {
            const std::string delta_table = idxDeltaTable(def_now->index_id);
            constexpr size_t kReplayBatch = 1024;
            for (int pass = 0; pass < 50; ++pass) { // 有界轮数，避免持续写入下无限追赶
                size_t applied_total = 0;
                std::string last_key; // **每轮**从变更表表首开始；轮内靠 last_key 续扫（此前误为每批清空 ⇒ 重复应用）
                while (true) {
                    const size_t applied =
                        co_await async_data_.replayDeltaBatch(table, delta_table, kReplayBatch, last_key);
                    applied_total += applied;
                    if (applied < kReplayBatch)
                        break; // 本批未满 ⇒ 变更表已到末尾
                }
                if (applied_total == 0)
                    break; // 本轮无新变更 ⇒ 追平
            }
        }
    }

    // P2 追赶 + **近似关闸**：追平 → 翻 PUBLIC → 再排空一次（见 catchUpAndPublish 注释）
    if (!(co_await catchUpAndPublish(table, stmt.index_name)))
        co_return IndexBuildResult{IndexBuildOutcome::ERROR, "Failed to publish index state (PUBLIC)"};

    co_return IndexBuildResult{IndexBuildOutcome::PUBLIC, {}};
}

folly::coro::Task<IndexBuildResult> QueryExecutor::backfillEdgeIndex(IndexDdlStatement stmt, std::string table,
                                                                     EdgeLabelId edge_label_id,
                                                                     std::vector<uint16_t> prop_ids,
                                                                     std::function<bool()> cancelled) {
    // 边索引回填 + 提交（**不落状态**，理由同顶点版本）。
    const auto& schema = async_meta_.schema();
    bool hasConflict = false;
    bool cancelled_midway = false;
    {
        GraphTxnHandle txn = co_await async_data_.beginTran();
        async_data_.setTransaction(txn);

        {
            auto gen = async_data_.scanEdgesByType(edge_label_id, std::nullopt, std::nullopt);
            while (auto batch = co_await gen.next()) {
                if (cancelled && cancelled()) {
                    cancelled_midway = true;
                    break; // 跳出扫描：先让 gen 析构，再显式回滚（见提交前）
                }
                for (const auto& entry : *batch) {
                    auto props_opt = co_await async_data_.getEdgeProperties(edge_label_id, entry.edge_id);
                    // Note: getEdgeProperties not currently exposed in IAsyncGraphDataStore
                    // For now skip properties; index entries will be created when properties API is added
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
                            spdlog::warn("Unique edge index '{}' backfill found duplicate value on edge {}",
                                         stmt.index_name, entry.edge_id);
                            hasConflict = true;
                            break;
                        }
                    }
                    auto adj_value = ValueCodec::encodeEdgeAdjacency(entry.src_vertex_id, entry.dst_vertex_id,
                                                                     entry.seq, edge_label_id);
                    co_await async_data_.insertIndexEntry(table, values, entry.edge_id, std::move(adj_value));
                }
                if (hasConflict)
                    break;
            }
        } // gen destroyed before commit

        if (cancelled_midway) {
            // **显式同步回滚**：只置取消位就返回会让索引表仍被本会话占用 ⇒ DROP 删表失败
            // （"Device or resource busy"）。同步回滚确保任务退出时表已释放。
            async_data_.rollbackTranNow(txn);
            co_return IndexBuildResult{IndexBuildOutcome::CANCELLED, "cancelled during backfill"};
        }

        // 提交失败绝不能继续往下走：索引会被标成 PUBLIC 而条目为空，
        // 规划器随后选中它 ⇒ 静默漏结果（空索引比没有索引更危险）。
        const bool committed = co_await async_data_.commitTran(txn);
        if (!committed) {
            co_return IndexBuildResult{IndexBuildOutcome::ERROR,
                                       "Index backfill transaction failed to commit (index left in ERROR, not "
                                       "ONLINE); see the server log for the WiredTiger error"};
        }
    }

    if (hasConflict) {
        co_return IndexBuildResult{IndexBuildOutcome::ERROR,
                                   "Unique edge index creation failed: duplicate values found during backfill"};
    }

    // ==================== P2 追赶：重放构建期间的变更表（**边路径此前完全缺失**）====================
    // 顶点路径有同样的收尾；边路径遗漏 ⇒ 构建期写入变更表的边**永远不会进索引**（实测 3/6 丢失）。
    {
        const auto& schema_now = async_meta_.schema();
        auto def_now = schema_now.findIndexByName(stmt.index_name);
        if (def_now && def_now->index_id != 0) {
            const std::string delta_table = idxDeltaTable(def_now->index_id);
            constexpr size_t kReplayBatch = 1024;
            for (int pass = 0; pass < 50; ++pass) {
                size_t applied_total = 0;
                std::string last_key; // 每轮从表首开始，轮内靠 last_key 续扫
                while (true) {
                    const size_t applied =
                        co_await async_data_.replayDeltaBatch(table, delta_table, kReplayBatch, last_key);
                    applied_total += applied;
                    if (applied < kReplayBatch)
                        break;
                }
                if (applied_total == 0)
                    break; // 追平（已应用的行在重放时从变更表删除）
            }
        }
    }

    // P2 追赶 + **近似关闸**：追平 → 翻 PUBLIC → 再排空一次（见 catchUpAndPublish 注释）
    if (!(co_await catchUpAndPublish(table, stmt.index_name)))
        co_return IndexBuildResult{IndexBuildOutcome::ERROR, "Failed to publish index state (PUBLIC)"};

    co_return IndexBuildResult{IndexBuildOutcome::PUBLIC, {}};
}

folly::coro::Task<void> QueryExecutor::handleIndexDdl(const IndexDdlStatement& stmt, ExecutionResult& result) {
    // 异步索引构建开关：会话已按线程隔离（AsyncGraphDataStore 的 txn 为 thread-local，§20.18 A）
    // ⇒ 后台构建线程与查询线程不再共享 txn/session，可安全启用。
    static constexpr bool kEnableAsyncIndexBuild = true;
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
        // P2：变更表与索引同生命周期（构建期写入进它，追赶后删除，§5.0/§5.2）
        ok = co_await async_data_.createIndex(idxDeltaTable(idx_def->index_id));
        if (!ok) {
            result.error = "Failed to create index delta table";
            co_return;
        }

        // Pre-resolve strong accessors to prop ids.
        std::vector<ResolvedIndexAccessor> resolved;
        for (const auto& acc : idx_def->accessors) {
            ResolvedIndexAccessor ra;
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

        // 注入了"每图构建服务" ⇒ 提交后台任务后**立即返回**（异步构建；状态由服务的发布回调落 PUBLIC/ERROR）。
        // 未注入（例如单测里的极小化装配）则走下面的同步路径，保证任何装配下都可用。
        // **临时安全开关（2026-10）**：后台构建与 DML 写入并发时 WT 会话被并发使用
        // ⇒ `session_dhandle` 损坏 + SIGSEGV（ASan 与 release 均复现，设计 §20.17）。
        // 强怀疑根因：后台任务与查询线程**共享 async store 的 `txn_`**。在改为"后台任务独立 txn/会话"前，
        // 只走**同步构建**以消除崩溃路径。
        if (index_builds_ && kEnableAsyncIndexBuild) {
            auto job = [this, stmt, table, label_id = label_def->id, id = idx_def->index_id,
                        resolved = std::move(resolved)]() mutable {
                return folly::coro::blockingWait(
                    backfillVertexIndex(stmt, table, label_id, std::move(resolved),
                                        [svc = index_builds_, id] { return svc->isCancelled(id); }));
            };
            if (index_builds_->submit(idx_def->index_id, stmt.index_name, std::move(job))) {
                result.columns.push_back("result");
                Row row;
                row.push_back(std::string("Index created (building): " + stmt.index_name));
                result.rows.push_back(std::move(row));
                co_return;
            }
            // 提交失败（该索引已在构建中）⇒ 回落到同步路径继续本次请求
        }

        // Backfill（已抽成协程：同一实现对同步路径与后台任务都可用）
        IndexBuildResult build = co_await backfillVertexIndex(stmt, table, label_def->id, std::move(resolved));
        if (build.outcome != IndexBuildOutcome::PUBLIC) {
            ok = co_await async_meta_.updateIndexState(stmt.index_name, IndexState::ERROR);
            result.error = build.error;
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

        // 取回刚创建的边索引定义（后面据此拿 index_id 提交后台构建；与顶点分支同一做法）
        const auto& schema_edge = async_meta_.schema();
        auto idx_def_edge = schema_edge.findIndexByName(stmt.index_name);
        if (!idx_def_edge) {
            result.error = "Created edge index not found in schema: " + stmt.index_name;
            co_return;
        }

        // 表名与写入/维护/扫描三处保持同一写法（都按属性个数选择）。
        // 注意：单属性时 eidxCompositeTable(label, {p}) 与 eidxTable(label, p) **生成同一个名字**
        // （均为 table:eidx_<label>_<p>），故本处并非"表名不一致"缺陷的修复点（曾误判，已更正）。
        auto table = prop_ids.size() == 1 ? eidxTable(edge_label_def->id, prop_ids[0])
                                          : eidxCompositeTable(edge_label_def->id, prop_ids);
        ok = co_await async_data_.createIndex(table);
        if (!ok) {
            result.error = "Failed to create edge index storage table";
            co_return;
        }
        // P2：边索引的变更表（与索引同生命周期）。
        // **必须在上面 if 之外**——此前它被插到了 `co_return` 之后 ⇒ **死代码** ⇒ 边索引的变更表从未创建
        // ⇒ 构建期分流写入变更表时 "Failed to open cursor … error 2" ⇒ 写入失败丢数据（设计 §20.14/§20.15）。
        ok = co_await async_data_.createIndex(idxDeltaTable(idx_def_edge->index_id));
        if (!ok) {
            result.error = "Failed to create edge index delta table";
            co_return;
        }

        // Backfill: scan existing edges and insert index entries
        // 同顶点分支：注入服务 ⇒ 后台异步构建并立即返回
        if (index_builds_ && kEnableAsyncIndexBuild) { // 同上：与顶点分支共用同一开关
            auto job = [this, stmt, table, elid = edge_label_def->id, id = idx_def_edge->index_id, prop_ids]() mutable {
                return folly::coro::blockingWait(
                    backfillEdgeIndex(stmt, table, elid, std::move(prop_ids),
                                      [svc = index_builds_, id] { return svc->isCancelled(id); }));
            };
            if (index_builds_->submit(idx_def_edge->index_id, stmt.index_name, std::move(job))) {
                result.columns.push_back("result");
                Row row;
                row.push_back(std::string("Edge index created (building): " + stmt.index_name));
                result.rows.push_back(std::move(row));
                co_return;
            }
        }

        // Backfill（已抽成协程）
        IndexBuildResult build = co_await backfillEdgeIndex(stmt, table, edge_label_def->id, prop_ids);
        if (build.outcome != IndexBuildOutcome::PUBLIC) {
            ok = co_await async_meta_.updateIndexState(stmt.index_name, IndexState::ERROR);
            result.error = build.error;
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

        // ① 构建中 ⇒ **取消并等待任务退出**；**但绝不在此时删表** —— 实测在"取消等待期间删表"会与
        //   构建侧的 WT 会话并发使用同一会话，导致 `session_dhandle` 损坏并 **SIGSEGV**
        //   （ASan 与 release 均可复现，设计 §20.17）。表交给**构建任务退出后/启动清理**回收（孤儿表）。
        // ⓪ 机会式回收：重试此前登记失败的孤儿表（DROP 幂等 ⇒ 安全）
        for (const auto& orphan : takeOrphans()) {
            if (!(co_await async_data_.dropIndex(orphan)))
                rememberOrphan(orphan); // 仍占用 ⇒ 留待下次
            else
                spdlog::info("dropIndex: 孤儿表 {} 已回收", orphan);
        }

        // ① 构建中 ⇒ **拒绝删除**（安全优先）。实测"取消 + 删定义 + 删表"的组合会与在飞构建的 WT 会话
        //   并发使用同一会话 ⇒ `session_dhandle` 损坏并 **SIGSEGV**（ASan 与 release 均复现，§20.17）。
        //   在竞态根因定位前，先返回明确错误，避免崩溃与半删状态；用户可等构建结束（秒级）再删。
        if (idx_def && index_builds_ && index_builds_->isBuilding(idx_def->index_id)) {
            result.error = "Index is still building; retry DROP after it finishes (state: BUILDING)";
            co_return;
        }

        std::string new_table;
        if (idx_def && idx_def->index_id != 0)
            new_table = vidxTableById(idx_def->index_id);

        // ② **先删存储表，成功后再删定义**。顺序颠倒会在"表删不掉"时留下"定义已消失、表还在"的坏状态。
        if (!new_table.empty()) {
            // **只尝试一次**：索引表可能仍被构建侧的会话/游标占用（WT 报 "Device or resource busy"）。
            // 曾试过"200ms 紧循环重试"，结果在 ASan 下暴露 **WT 会话并发使用** 的 SEGV
            // （`session_dhandle.c:77 __session_add_dhandle`：重试的 drop 与构建侧会话收尾并发）⇒ 撤销重试。
            // 失败即降级为**孤儿表**（定义照删，见下），由后续清理回收。
            // 有界重试：非构建期删表偶发被并发读/游标占用（WT "Device or resource busy"），
            // 稍后即释放。**构建中的 DROP 已在上方被拒绝**，故此处不存在"与构建并发删表"的竞态。
            bool table_dropped = false;
            for (int attempt = 0; attempt < 100 && !table_dropped; ++attempt) {
                table_dropped = co_await async_data_.dropIndex(new_table);
                if (!table_dropped)
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            if (!table_dropped) {
                // **设计调整**：物理表暂时删不掉（实测：构建侧会话可能仍缓存表句柄）时**不再保留定义**——
                // 否则该属性集**永久无法重建索引**（用户可见事故）。改为：定义照删（继续往下走），
                // 表降级为**孤儿表**，由启动清理/后续清理回收。DROP 的语义是"索引不再存在"，
                // 物理回收只是实现细节，不应阻塞语义。
                spdlog::warn(
                    "dropIndex: 索引表 {} 暂时无法删除，登记为孤儿表（定义已删除；后续 DROP 会机会式重试回收）",
                    new_table);
                rememberOrphan(new_table);
            }
        }
        // P2：连同变更表一起删（若存在；失败不致命，记日志即可——启动清理会兜底，§5.2）
        if (idx_def && idx_def->index_id != 0) {
            const std::string delta_table = idxDeltaTable(idx_def->index_id);
            if (!(co_await async_data_.dropIndex(delta_table)))
                spdlog::warn("dropIndex: 变更表 {} 删除失败（启动清理会兜底）", delta_table);
        }
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
                // 对外只暴露**语义状态**：`WRITE_ONLY` 就是"构建中"（读路径只认 PUBLIC，见 I1）。
                // 内部枚举名不再泄漏给用户，避免把实现细节当约定。
                state_str = "BUILDING";
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
