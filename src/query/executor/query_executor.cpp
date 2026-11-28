#include "query/executor/query_executor.hpp"
#include "storage/session/schema_pin.hpp"

#include "storage/index/index_builder.hpp"
#include "storage/index/index_runtime_factory.hpp"
#include "storage/index/index_test_hooks.hpp"

#include "common/types/constants.hpp"
#include "query/catalog/catalog.hpp"
#include "query/function/function_registry.hpp"
#include "query/optimizer/analysis/logical_plan_walk.hpp"
#include "query/optimizer/logical_optimizer.hpp"
#include "query/parser/index_ddl_parser.hpp"
#include "query/physical_plan/physical_operator_base.hpp"

#include <folly/coro/BlockingWait.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace eugraph {
namespace compute {

namespace {

/// 0/未设置 = 无限等待；>0 = 每次构建最多等这么久旧 schema pin。
uint32_t indexPinWaitTimeoutMs() {
    const char* env = std::getenv(config::kEnvIndexPinWaitTimeoutMs);
    if (env == nullptr || *env == '\0')
        return 0;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(env, &end, 10);
    if (end == env || end == nullptr || *end != '\0')
        return 0;
    return static_cast<uint32_t>(std::min<unsigned long>(parsed, std::numeric_limits<uint32_t>::max()));
}

/// Bound the WiredTiger transaction used by index backfill. A single
/// transaction over an sf1-sized label (millions of vertices) holds enough
/// dirty pages that eviction can force WT_ROLLBACK; the backfill is therefore
/// split into independent commit batches.
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
    // 事务级 schema 版本：显式事务用 BEGIN 时 pin 的版本，否则原子读取当前版本。
    // generation/schema 必须来自同一个发布点，不能分开读。
    SchemaVersion version_info;
    if (existing_session && existing_session->get().schemaPin().valid) {
        const auto& pin = existing_session->get().schemaPin();
        version_info = SchemaVersion{pin.schema, pin.generation, pin.compat_version};
    } else {
        version_info = async_meta_.schemaVersion();
    }
    if (!version_info.schema) {
        version_info.schema = std::make_shared<GraphSchema>();
        version_info.generation = async_meta_.schemaGeneration();
        version_info.compat_version = async_meta_.compatVersion();
    }
    const auto schema_version = version_info.schema;

    auto ctx = std::make_shared<StreamContext>(schema_version);
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

    auto label_name_to_id = schema_version->label_name_to_id;
    auto edge_label_name_to_id = schema_version->edge_label_name_to_id;

    auto catalog = std::make_unique<catalog::Catalog>();
    catalog->load(schema_version);

    // 规划统计：优先使用 GraphStatsStore 的一致快照（journal 维护），
    // 缺失时回退 schema 近似计数。一次规划只取一次，计划内部数字不跳变。
    std::unordered_map<LabelId, uint64_t> stats_counts;
    const std::unordered_map<LabelId, uint64_t>* effective_counts = &ctx->label_vertex_counts;
    if (stats_store_ != nullptr) {
        auto stats_snapshot = stats_store_->snapshot();
        if (stats_snapshot && !stats_snapshot->labels.empty()) {
            // 旧数据/新写入混合：以 schema 计数为 baseline，只覆盖 stats 明确拥有的
            // label。不能因为 stats 非空就丢掉没有 journal 覆盖的旧 label。
            stats_counts = ctx->label_vertex_counts;
            for (const auto& [lid, st] : stats_snapshot->labels)
                stats_counts[lid] = st.vertices > 0 ? static_cast<uint64_t>(st.vertices) : 0;
            effective_counts = &stats_counts;
        }
    }
    if (effective_counts != &ctx->label_vertex_counts)
        catalog->setLabelVertexCounts(*effective_counts);

    if (stats_store_ != nullptr) {
        auto stats_snapshot = stats_store_->snapshot();
        if (stats_snapshot) {
            for (const auto& [elid, st] : stats_snapshot->edge_labels) {
                if (st.edges >= 0)
                    catalog->setEdgeLabelStats(elid, static_cast<uint64_t>(st.edges), st.avg_out_degree);
            }
        }
    }

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

    // 写语句需要本地可变 schema 副本（DML 隐式建标签/属性会更新它）；
    // 只读语句继续共享事务级不可变版本。事务 pin 仍固定原始版本。
    const std::shared_ptr<const GraphSchema> pinned_schema = schema_version;
    if (binder.ctx().has_mutation) {
        auto write_schema = std::make_shared<GraphSchema>(*schema_version);
        auto write_ctx = std::make_shared<StreamContext>(write_schema);
        write_ctx->query_text = ctx->query_text;
        write_ctx->profile_requested = ctx->profile_requested;
        write_ctx->columns = std::move(ctx->columns);
        ctx = std::move(write_ctx);
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

    // schema pin：显式事务由 BEGIN 设置；自动提交/未设置的调用方在语句边界兜底。
    // 统一入口负责 pin 计数与事务结束 unpin，批量装载复用同一 helper。
    ensureSchemaPin(active_session->get(), version_info, schema_generations_.get());

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

    // label/edge_label 定义由 ctx 引用 schema_version，不再逐语句复制。
    ctx->label_name_to_id = std::move(label_name_to_id);
    ctx->edge_label_name_to_id = std::move(edge_label_name_to_id);

    PlanContext plan_ctx{
        .label_name_to_id = ctx->label_name_to_id,
        .edge_label_name_to_id = ctx->edge_label_name_to_id,
        .label_defs = ctx->label_defs,
        .edge_label_defs = ctx->edge_label_defs,
        .label_vertex_counts = *effective_counts,
        .schema = ctx->schema,
        .mutable_label_defs = ctx->owned_schema ? &ctx->owned_schema->labels : nullptr,
        .mutable_edge_label_defs = ctx->owned_schema ? &ctx->owned_schema->edge_labels : nullptr,
        .index_registry = index_registry_,
        .txn_start_generation = active_session->get().schemaPin().generation,
        .eval_ctx = {},
        .requirements = {},
        .extraction_info = {},
        .var_slots = {},
        .scoped_var_slots = {},
        .label_order_by_name = {},
        .alias_map = {},
        .slot_allocator = {},
        .fresh_expands = {},
        .func_registry = ctx->func_registry.get(),
        .deferred_slots = std::nullopt,
    };

    plan_ctx.eval_ctx.catalog = ctx->catalog.get();
    plan_ctx.eval_ctx.label_defs = &ctx->label_defs;
    // Static property pruning is only valid when the statement itself cannot
    // create labels/properties between bind time and execution time.
    plan_ctx.eval_ctx.allow_static_schema_pruning = !binder.ctx().has_mutation;

    // SlotIds are finalized by the Binder (logical identity); reuse them.
    plan_ctx.var_slots = binder.ctx().var_slots;
    for (const auto& [name, info] : binder.ctx().symbols) {
        if (info.slot_id != binder::INVALID_SLOT_ID)
            plan_ctx.var_slots[name] = info.slot_id;
    }
    plan_ctx.scoped_var_slots = binder.ctx().scoped_bindings;
    for (const auto& [name, info] : binder.ctx().symbols) {
        if (!info.source_labels.empty())
            plan_ctx.label_order_by_name[name] = info.source_labels;
    }
    ctx->label_order = plan_ctx.label_order_by_name;
    plan_ctx.slot_allocator.seed(binder.ctx().slot_allocator.current());

    std::function<void(binder::BoundLogicalOperator&)> collect_slot_semantics = [&](binder::BoundLogicalOperator& op) {
        std::visit(
            [&](auto& val) {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                    if (!val)
                        return;
                    for (const auto& item : val->items) {
                        binder::SlotId slot =
                            item.output_slot != binder::INVALID_SLOT_ID ? item.output_slot : item.alias_slot;
                        if (slot == binder::INVALID_SLOT_ID)
                            continue;
                        binder::SlotSemantics sem;
                        sem.slot = slot;
                        sem.name = item.alias;
                        sem.type = item.result_type;
                        if (auto* prop = std::get_if<std::unique_ptr<binder::BoundPropertyRef>>(&item.expr);
                            prop && *prop) {
                            sem.kind = binder::SlotSemanticKind::Property;
                            sem.property_name = (*prop)->property_name;
                            for (const auto& cand : (*prop)->candidates) {
                                sem.property_labels.push_back(cand.label_id);
                                sem.property_ids.push_back(cand.prop_id);
                            }
                            sem.strong = (*prop)->strong_mode;
                        } else if (auto* ref = std::get_if<binder::BoundColumnRef>(&item.expr); ref) {
                            auto src = binder.ctx().slot_semantics.find(ref->slot_id);
                            if (src != binder.ctx().slot_semantics.end())
                                sem = src->second, sem.slot = slot;
                        }
                        binder.ctx().recordSlotSemantics(sem);
                    }
                }
                optimizer::forEachLogicalChild(
                    op, [&](binder::BoundLogicalOperator& child) { collect_slot_semantics(child); });
            },
            op);
    };
    collect_slot_semantics(bound_stmt->plan.root);

    // 2.5. Logical optimization. Projection pushdown is a pre-Cascades
    // optimizer pass: the binder collected semantic property references, the
    // optimizer turns them into scan/expand property demand.
    optimizer::LogicalOptimizer logical_optimizer;
    logical_optimizer.optimize(bound_stmt->plan, ctx->catalog.get(), &binder.ctx().slot_semantics, effective_counts);

    // 3. Physical planning. Try CBO-chosen plan first (Phase 4); fall back
    // to planBound (RBO over the optimized logical tree) when no winner.
    PhysicalPlanner physical_planner;
    std::variant<std::unique_ptr<PhysicalOperator>, std::string> phys_result = std::string("");
    bool used_cbo = false;
    if (bound_stmt->plan.chosen) {
        phys_result = physical_planner.planChosen(*bound_stmt->plan.chosen, query_data, async_meta_, plan_ctx);
        used_cbo = !std::holds_alternative<std::string>(phys_result);
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

    // 5.5. If EXPLAIN, format the physical tree without executing it.
    if (is_explain) {
        ExplainContext explain_ctx;
        explain_ctx.schema = schema_version.get();
        explain_ctx.plan_source = used_cbo ? "CBO" : "RBO";
        explain_ctx.slot_semantics = &binder.ctx().slot_semantics;
        explain_ctx.symbols = &plan_ctx.explain_symbols;
        auto plan_root = std::make_shared<ExplainNode>(ExplainBuilder::build(*phys_op, explain_ctx));
        plan_root->plan_source = explain_ctx.plan_source;
        ctx->explain_node = plan_root;
        const std::string plan_text = formatExplainTree(*plan_root, explain_ctx);
        ctx->columns.clear();
        ctx->columns.push_back("Plan");
        std::vector<Row> plan_rows;
        size_t begin = 0;
        while (begin <= plan_text.size()) {
            size_t end = plan_text.find('\n', begin);
            Row row;
            row.push_back(plan_text.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
            plan_rows.push_back(std::move(row));
            if (end == std::string::npos)
                break;
            begin = end + 1;
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

// ==================== 索引运行时注册表维护 ====================

void QueryExecutor::registerIndexRuntime(const std::string& index_name, uint32_t label_id, bool is_edge) {
    if (index_registry_ == nullptr)
        return;
    auto def = async_meta_.schemaPtr()->findIndexByName(index_name);
    if (!def || def->index_id == 0)
        return;
    auto desc = makeIndexRuntimeDescriptor(*def, label_id, is_edge);
    // 新建索引从 SCAN 开始（持久化的 WRITE_ONLY 是它的投影）。
    desc.initial_phase = IndexPhase::kScan;
    index_registry_->createOrUpdate(std::move(desc));
}

void QueryExecutor::publishIndexRuntime(const std::string& index_name) {
    if (index_registry_ == nullptr)
        return;
    auto def = async_meta_.schemaPtr()->findIndexByName(index_name);
    if (!def)
        return;
    auto runtime = index_registry_->find(def->index_id);
    if (!runtime)
        return;
    // 发布时刻的代数：更早 pin 的事务（public_generation > pin.generation）看不到它。
    runtime->setPublicGeneration(async_meta_.schemaGeneration());
    runtime->setProgress(100.0);
    runtime->transitionPhase(IndexPhase::kPublic);
    // 唤醒 FINALIZE 期间等待准入的写者：PUBLIC 后它们应重新 route 到直写索引表。
    runtime->reopenAdmission();
}

void QueryExecutor::failIndexRuntime(const std::string& index_name, std::string_view error) {
    if (index_registry_ == nullptr)
        return;
    auto def = async_meta_.schemaPtr()->findIndexByName(index_name);
    if (!def)
        return;
    auto runtime = index_registry_->find(def->index_id);
    if (runtime) {
        runtime->setLastError(error.empty() ? std::string("Index build failed (see server log)") : std::string(error));
        runtime->transitionPhase(IndexPhase::kError);
        // 失败/取消也必须唤醒等待者：它们应重新 route 到 kSkip，而不是永久挂起。
        runtime->reopenAdmission();
    }
}

void QueryExecutor::retireIndexRuntime(uint32_t index_id) {
    if (index_registry_ == nullptr || index_id == 0)
        return;
    auto runtime = index_registry_->find(index_id);
    if (!runtime)
        return;
    // 先转 lifecycle，再唤醒等待者：醒来后 route 看到的是 skip，不会重新拿到 delta 准入。
    runtime->transitionLifecycle(IndexLifecycle::kDropping);
    runtime->transitionLifecycle(IndexLifecycle::kPurging);
    runtime->transitionLifecycle(IndexLifecycle::kPurged);
    runtime->reopenAdmission();
    index_registry_->erase(index_id);
}

folly::coro::Task<void> QueryExecutor::retryPendingIndexCleanups() {
    const auto records = co_await async_meta_.listCleanupRecords();
    for (const auto& record : records) {
        if (record.kind != "pending_drop" && record.kind != "orphan")
            continue;

        std::shared_ptr<IndexRuntime> runtime;
        if (index_registry_ != nullptr)
            runtime = index_registry_->find(record.index_id);
        if (runtime != nullptr) {
            if (runtime->lifecycle() != IndexLifecycle::kPendingPurge)
                continue; // 构建/正常/已删除的索引不归这里管
            if (runtime->readers() != 0)
                continue; // 还有在飞读者，下一轮
        }

        bool all_dropped = true;
        for (const auto& table : record.tables) {
            if (!(co_await async_data_.dropIndex(table))) {
                all_dropped = false;
                break;
            }
        }
        if (all_dropped) {
            co_await async_meta_.removeCleanupRecord(record.kind, record.index_id);
            if (runtime != nullptr) {
                runtime->transitionLifecycle(IndexLifecycle::kPurging);
                runtime->transitionLifecycle(IndexLifecycle::kPurged);
                index_registry_->erase(record.index_id);
            }
            spdlog::info("[index] pending purge for index {} done", record.index_id);
            continue;
        }

        // 仍失败：pending_drop 转 orphan（下次打开重试），避免 pending 记录长期滞留。
        if (record.kind == "pending_drop") {
            co_await async_meta_.putCleanupRecord("orphan", record.index_id, record.tables);
            co_await async_meta_.removeCleanupRecord("pending_drop", record.index_id);
        }
        spdlog::warn("[index] pending purge for index {} still busy; kept for next retry", record.index_id);
    }
}

folly::coro::Task<void> QueryExecutor::cleanupDroppedIndex(IAsyncGraphMetaStore::IndexCleanupRecord record,
                                                           uint64_t drop_generation) {
    // 先等"删除时刻之前 pin 的事务"结束（它们可能已计划、稍后才来扫这张表）。
    if (schema_generations_ != nullptr && drop_generation != 0 &&
        !schema_generations_->waitNoPinsOlderThan(drop_generation, index_purge_wait_ms_)) {
        if (index_registry_ != nullptr) {
            if (auto runtime = index_registry_->find(record.index_id))
                runtime->transitionLifecycle(IndexLifecycle::kPendingPurge);
        }
        spdlog::warn("[index] drop cleanup for index {}: old schema pins still active; kept as pending_drop",
                     record.index_id);
        co_return;
    }
    // 再等读句柄归零：在飞查询可能正在扫这张表（§9.3）。
    if (index_registry_ != nullptr) {
        if (auto runtime = index_registry_->find(record.index_id);
            runtime && !runtime->waitReadersZero(index_purge_wait_ms_)) {
            runtime->transitionLifecycle(IndexLifecycle::kPendingPurge);
            spdlog::warn("[index] drop cleanup for index {}: readers still active after timeout; "
                         "kept as pending_drop (retry on next open)",
                         record.index_id);
            co_return; // 保留清理记录
        }
    }

    bool all_dropped = true;
    for (const auto& table : record.tables) {
        if (!(co_await async_data_.dropIndex(table))) {
            all_dropped = false;
            break;
        }
    }

    if (all_dropped) {
        co_await async_meta_.removeCleanupRecord(record.kind, record.index_id);
        if (index_registry_ != nullptr) {
            if (auto runtime = index_registry_->find(record.index_id)) {
                runtime->transitionLifecycle(IndexLifecycle::kPurging);
                runtime->transitionLifecycle(IndexLifecycle::kPurged);
                index_registry_->erase(record.index_id);
            }
        }
        spdlog::info("[index] drop cleanup for index {} done", record.index_id);
        co_return;
    }

    // 表仍被同进程的会话缓存占用（§9.2）：转 orphan，启动时重试。
    co_await async_meta_.putCleanupRecord("orphan", record.index_id, record.tables);
    co_await async_meta_.removeCleanupRecord(record.kind, record.index_id);
    if (index_registry_ != nullptr) {
        if (auto runtime = index_registry_->find(record.index_id))
            runtime->transitionLifecycle(IndexLifecycle::kPendingPurge);
    }
    spdlog::warn("[index] drop cleanup for index {}: tables still busy; recorded as orphan", record.index_id);
}

folly::coro::Task<std::string> QueryExecutor::applyIndexBuildOutcome(const IndexBuildSpec& spec,
                                                                     const IndexBuildOutcome& outcome) {
    if (!outcome.ok) {
        // DROP 可能已删定义：updateIndexState 返回 false 是正常竞态，不影响清理。
        if (!outcome.cancelled)
            co_await async_meta_.updateIndexState(spec.index_name, IndexState::ERROR);
        failIndexRuntime(spec.index_name, outcome.error);
        co_return outcome.error.empty() ? std::string("Index build failed") : outcome.error;
    }

    index_hooks::point("finalize.before_public", spec.index_id);
    if (!(co_await async_meta_.updateIndexState(spec.index_name, IndexState::PUBLIC))) {
        failIndexRuntime(spec.index_name, "Failed to set index state to PUBLIC");
        co_return std::string("Failed to set index state to PUBLIC");
    }
    publishIndexRuntime(spec.index_name);
    // PUBLIC 后写路径直写索引表，delta 不再使用；清掉空表（失败只告警，
    // DROP INDEX/启动回收仍是兜底）。
    if (!(co_await async_data_.dropIndex(indexDeltaTable(spec.index_id))))
        spdlog::warn("[index] failed to drop empty delta table for {} (index {}); DROP INDEX will retry",
                     spec.index_name, spec.index_id);
    co_return std::string{};
}

folly::coro::Task<void> QueryExecutor::handleIndexDdl(const IndexDdlStatement& stmt, ExecutionResult& result) {
    if (schema_generations_ == nullptr) {
        result.error = "Index DDL requires a SchemaGenerationTracker; graph assembly is incomplete";
        co_return;
    }
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
                // Strong accessor: the indexed value comes ONLY from the source
                // label's declared column. It must exist before the index is
                // created (and before any storage/table side effect).
                auto source_opt = co_await async_meta_.getLabelId(src.source_label);
                if (!source_opt.has_value()) {
                    result.error = "Source label not found: " + src.source_label;
                    co_return;
                }
                acc.source_label_id = *source_opt;
                auto source_def = co_await async_meta_.getLabelDefById(*source_opt);
                bool declared = false;
                if (source_def.has_value()) {
                    for (const auto& pd : source_def->properties) {
                        if (pd.name == src.property_name) {
                            declared = true;
                            break;
                        }
                    }
                }
                if (!declared) {
                    result.error = "Property '" + src.property_name + "' is not declared on label '" +
                                   src.source_label + "'; declare it with DDL before indexing it.";
                    co_return;
                }
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

        const auto schema_ptr = async_meta_.schemaPtr();
        const GraphSchema& schema = *schema_ptr;
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
        registerIndexRuntime(stmt.index_name, label_def->id, false);
        ok = co_await async_data_.createIndex(indexDeltaTable(idx_def->index_id));
        if (!ok) {
            result.error = "Failed to create index delta table";
            failIndexRuntime(stmt.index_name);
            co_return;
        }
        // 预解析 strong accessor + 拷贝 schema 快照（weak accessor/anon 解析用），
        // 之后把 SCAN/FINALIZE 完全交给 storage/index/index_builder。
        IndexBuildSpec spec;
        spec.index_id = idx_def->index_id;
        spec.index_name = stmt.index_name;
        spec.label_id = label_def->id;
        spec.is_edge = false;
        spec.unique = idx_def->unique;
        spec.index_table = table;
        spec.delta_table = indexDeltaTable(idx_def->index_id);
        spec.labels = schema.labels;
        for (const auto& [lid, ldef] : schema.labels) {
            if (ldef.name == kAnonLabelName) {
                spec.anon_label_id = lid;
                break;
            }
        }
        {
            const auto counts = async_meta_.schemaPtr()->label_vertex_counts;
            auto count_it = counts.find(label_def->id);
            if (count_it != counts.end())
                spec.total_entities = count_it->second;
        }
        for (const auto& acc : idx_def->accessors) {
            IndexBuildAccessor accessor;
            accessor.is_strong = acc.is_strong;
            accessor.property_name = acc.property_name;
            accessor.source_label_id = acc.source_label_id;
            if (acc.is_strong) {
                auto src = schema.getLabel(acc.source_label_id);
                if (!src) {
                    result.error = "Source label not found for index '" + stmt.index_name + "'";
                    co_return;
                }
                bool found = false;
                for (const auto& pd : src->properties) {
                    if (pd.name == acc.property_name) {
                        accessor.source_prop_id = pd.id;
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
            spec.accessors.push_back(std::move(accessor));
        }

        const auto make_context = [&]() {
            return IndexBuildContext{async_data_, session_pool_, index_registry_, [this, name = stmt.index_name]() {
                                         return index_builds_ != nullptr && index_builds_->isCancelled(name);
                                     }};
        };

        if (index_builds_ != nullptr) {
            IndexBuildService::Job job = [this, spec]() {
                auto usage_guard = usage_gate_ ? usage_gate_->tryEnter() : GraphUsageGate::Guard{};
                if (usage_gate_ && !usage_guard) {
                    (void)folly::coro::blockingWait(
                        applyIndexBuildOutcome(spec, IndexBuildOutcome{false, false, "graph is shutting down"}));
                    return;
                }
                // 构建开始前等“索引创建之前 BEGIN 的旧事务”结束：
                // 它们的 schema 快照里没有新索引，写路径不会写 delta；
                // 必须让这些写入先落数据，再开始 SCAN，否则索引会漏行。
                const uint64_t index_generation = async_meta_.schemaGeneration();
                if (schema_generations_ && index_generation != 0) {
                    const uint32_t configured_wait = indexPinWaitTimeoutMs();
                    const uint32_t wait_ms =
                        configured_wait == 0 ? std::numeric_limits<uint32_t>::max() : configured_wait;
                    if (!schema_generations_->waitNoPinsOlderThan(index_generation, wait_ms)) {
                        spdlog::warn("[index] build for {} timed out waiting for old schema pins "
                                     "(oldest generation {}, active pins {}); mark ERROR",
                                     spec.index_name, schema_generations_->minActiveGeneration(),
                                     schema_generations_->pinCount());
                        const auto error = folly::coro::blockingWait(applyIndexBuildOutcome(
                            spec, IndexBuildOutcome{false, false, "old transaction schema pin wait timed out"}));
                        (void)error;
                        return;
                    }
                }
                auto context =
                    IndexBuildContext{async_data_, session_pool_, index_registry_, [this, name = spec.index_name]() {
                                          return index_builds_ != nullptr && index_builds_->isCancelled(name);
                                      }};
                const auto outcome = folly::coro::blockingWait(runIndexBuild(context, spec));
                const auto error = folly::coro::blockingWait(applyIndexBuildOutcome(spec, outcome));
                if (!error.empty())
                    spdlog::error("[index] background build '{}' failed: {}", spec.index_name, error);
            };
            if (!index_builds_->submit(spec.index_name, std::move(job))) {
                result.error = "Index build already in progress: " + spec.index_name;
                co_await async_meta_.updateIndexState(spec.index_name, IndexState::ERROR);
                failIndexRuntime(spec.index_name, result.error);
                co_return;
            }
            result.columns.push_back("result");
            Row row;
            row.push_back(std::string("Index created (building): " + spec.index_name));
            result.rows.push_back(std::move(row));
            co_return;
        }

        auto usage_guard = usage_gate_ ? usage_gate_->tryEnter() : GraphUsageGate::Guard{};
        if (usage_gate_ && !usage_guard) {
            result.error = "Graph is shutting down; index build not started";
            co_return;
        }
        const auto outcome = co_await runIndexBuild(make_context(), spec);
        const auto error = co_await applyIndexBuildOutcome(spec, outcome);
        if (!error.empty()) {
            result.error = error;
            co_return;
        }
        result.columns.push_back("result");
        Row row;
        row.push_back(std::string("Index created: " + spec.index_name));
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
        registerIndexRuntime(stmt.index_name, edge_label_def->id, true);
        auto schema_ptr = async_meta_.schemaPtr();
        auto edge_idx_def = schema_ptr->findIndexByName(stmt.index_name);
        if (!edge_idx_def) {
            result.error = "Created edge index not found in schema: " + stmt.index_name;
            failIndexRuntime(stmt.index_name);
            co_return;
        }
        ok = co_await async_data_.createIndex(indexDeltaTable(edge_idx_def->index_id));
        if (!ok) {
            result.error = "Failed to create edge index delta table";
            failIndexRuntime(stmt.index_name);
            co_return;
        }
        IndexBuildSpec spec;
        spec.index_id = edge_idx_def->index_id;
        spec.index_name = stmt.index_name;
        spec.label_id = edge_label_def->id;
        spec.is_edge = true;
        spec.unique = edge_idx_def->unique;
        spec.index_table = table;
        spec.delta_table = indexDeltaTable(edge_idx_def->index_id);
        spec.edge_prop_ids = edge_idx_def->prop_ids;

        const auto make_context = [&]() {
            return IndexBuildContext{async_data_, session_pool_, index_registry_, [this, name = stmt.index_name]() {
                                         return index_builds_ != nullptr && index_builds_->isCancelled(name);
                                     }};
        };

        if (index_builds_ != nullptr) {
            IndexBuildService::Job job = [this, spec]() {
                auto usage_guard = usage_gate_ ? usage_gate_->tryEnter() : GraphUsageGate::Guard{};
                if (usage_gate_ && !usage_guard) {
                    (void)folly::coro::blockingWait(
                        applyIndexBuildOutcome(spec, IndexBuildOutcome{false, false, "graph is shutting down"}));
                    return;
                }
                // 构建开始前等“索引创建之前 BEGIN 的旧事务”结束：
                // 它们的 schema 快照里没有新索引，写路径不会写 delta；
                // 必须让这些写入先落数据，再开始 SCAN，否则索引会漏行。
                const uint64_t index_generation = async_meta_.schemaGeneration();
                if (schema_generations_ && index_generation != 0) {
                    const uint32_t configured_wait = indexPinWaitTimeoutMs();
                    const uint32_t wait_ms =
                        configured_wait == 0 ? std::numeric_limits<uint32_t>::max() : configured_wait;
                    if (!schema_generations_->waitNoPinsOlderThan(index_generation, wait_ms)) {
                        spdlog::warn("[index] build for {} timed out waiting for old schema pins "
                                     "(oldest generation {}, active pins {}); mark ERROR",
                                     spec.index_name, schema_generations_->minActiveGeneration(),
                                     schema_generations_->pinCount());
                        const auto error = folly::coro::blockingWait(applyIndexBuildOutcome(
                            spec, IndexBuildOutcome{false, false, "old transaction schema pin wait timed out"}));
                        (void)error;
                        return;
                    }
                }
                auto context =
                    IndexBuildContext{async_data_, session_pool_, index_registry_, [this, name = spec.index_name]() {
                                          return index_builds_ != nullptr && index_builds_->isCancelled(name);
                                      }};
                const auto outcome = folly::coro::blockingWait(runIndexBuild(context, spec));
                const auto error = folly::coro::blockingWait(applyIndexBuildOutcome(spec, outcome));
                if (!error.empty())
                    spdlog::error("[index] background edge build '{}' failed: {}", spec.index_name, error);
            };
            if (!index_builds_->submit(spec.index_name, std::move(job))) {
                result.error = "Index build already in progress: " + spec.index_name;
                co_await async_meta_.updateIndexState(spec.index_name, IndexState::ERROR);
                failIndexRuntime(spec.index_name, result.error);
                co_return;
            }
            result.columns.push_back("result");
            Row row;
            row.push_back(std::string("Edge index created (building): " + spec.index_name));
            result.rows.push_back(std::move(row));
            co_return;
        }

        auto usage_guard = usage_gate_ ? usage_gate_->tryEnter() : GraphUsageGate::Guard{};
        if (usage_gate_ && !usage_guard) {
            result.error = "Graph is shutting down; index build not started";
            co_return;
        }
        const auto outcome = co_await runIndexBuild(make_context(), spec);
        const auto error = co_await applyIndexBuildOutcome(spec, outcome);
        if (!error.empty()) {
            result.error = error;
            co_return;
        }
        result.columns.push_back("result");
        Row row;
        row.push_back(std::string("Edge index created: " + spec.index_name));
        result.rows.push_back(std::move(row));

    } else if (stmt.type == IndexDdlStatement::DROP_INDEX) {
        const auto schema_ptr = async_meta_.schemaPtr();
        const GraphSchema& schema = *schema_ptr;
        auto idx_def = schema.findIndexByName(stmt.index_name);
        std::string new_table;
        if (idx_def && idx_def->index_id != 0)
            new_table = vidxTableById(idx_def->index_id);

        const uint32_t drop_index_id = (idx_def && idx_def->index_id != 0) ? idx_def->index_id : 0;
        std::vector<std::string> cleanup_tables;
        if (!new_table.empty())
            cleanup_tables.push_back(new_table);
        if (drop_index_id != 0)
            cleanup_tables.push_back(indexDeltaTable(drop_index_id));

        if (index_builds_ != nullptr && drop_index_id != 0) {
            // 异步 DROP：先取消 → 登记待清理（先记后删）→ 删定义 → 投递清理任务 → 立即返回。
            // 清理任务与构建任务在**同一队列**（1 线程），因此一定在构建退出后执行。
            index_builds_->cancel(stmt.index_name);
            if (!(co_await async_meta_.putCleanupRecord("pending_drop", drop_index_id, cleanup_tables))) {
                result.error = "Failed to record pending drop for: " + stmt.index_name;
                co_return;
            }
            bool ok = co_await async_meta_.dropIndex(stmt.index_name);
            if (!ok) {
                co_await async_meta_.removeCleanupRecord("pending_drop", drop_index_id);
                result.error = "Failed to drop index: " + stmt.index_name;
                co_return;
            }
            if (index_registry_ != nullptr) {
                if (auto runtime = index_registry_->find(drop_index_id))
                    runtime->transitionLifecycle(IndexLifecycle::kDropping);
            }
            const uint64_t drop_generation = async_meta_.schemaGeneration();
            IAsyncGraphMetaStore::IndexCleanupRecord record{"pending_drop", drop_index_id, cleanup_tables};
            IndexBuildService::Job cleanup = [this, record, drop_generation]() {
                folly::coro::blockingWait(cleanupDroppedIndex(record, drop_generation));
            };
            if (!index_builds_->submit(stmt.index_name + "\x01drop", std::move(cleanup))) {
                spdlog::warn("[index] drop cleanup for '{}' already queued; startup cleanup will retry",
                             stmt.index_name);
            }
            result.columns.push_back("result");
            Row row;
            row.push_back(std::string("Index dropped (dropping): " + stmt.index_name));
            result.rows.push_back(std::move(row));
            co_return;
        }

        // 无构建服务（单测/极小化装配）：保持同步删除语义。
        bool ok = co_await async_meta_.dropIndex(stmt.index_name);
        if (!ok) {
            result.error = "Failed to drop index: " + stmt.index_name;
            co_return;
        }
        if (!new_table.empty())
            co_await async_data_.dropIndex(new_table);
        if (drop_index_id != 0) {
            co_await async_data_.dropIndex(indexDeltaTable(drop_index_id));
            retireIndexRuntime(drop_index_id);
        }
        result.columns.push_back("result");
        Row row;
        row.push_back(std::string("Index dropped: " + stmt.index_name));
        result.rows.push_back(std::move(row));

    } else if (stmt.type == IndexDdlStatement::SHOW_INDEXES || stmt.type == IndexDdlStatement::SHOW_INDEX) {
        auto indexes = co_await async_meta_.listIndexes();
        result.columns = {"name",  "label",    "property",      "unique",     "state",
                          "phase", "progress", "delta_backlog", "started_at", "last_error"};

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
                state_str = "BUILDING"; // 内部值不泄漏给用户（设计 §13.1）
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

            // P3 可观测：phase / progress（0–100）来自运行时；没有运行时（旧索引/非本进程）用兜底值。
            std::string phase_str = "-";
            double progress = idx.state == IndexState::PUBLIC ? 100.0 : 0.0;
            uint32_t delta_backlog = 0;
            int64_t started_at = 0;
            std::string last_error;
            if (index_registry_ != nullptr && idx.index_id != 0) {
                if (auto runtime = index_registry_->find(idx.index_id)) {
                    switch (runtime->phase()) {
                    case IndexPhase::kScan:
                        phase_str = "SCAN";
                        break;
                    case IndexPhase::kCatchup:
                        phase_str = "CATCHUP";
                        break;
                    case IndexPhase::kFinalize:
                        phase_str = "FINALIZE";
                        break;
                    case IndexPhase::kPublic:
                        phase_str = "PUBLIC";
                        break;
                    case IndexPhase::kError:
                        phase_str = "ERROR";
                        break;
                    case IndexPhase::kAbsent:
                        phase_str = "-";
                        break;
                    }
                    if (runtime->lifecycle() != IndexLifecycle::kActive) {
                        switch (runtime->lifecycle()) {
                        case IndexLifecycle::kDropping:
                            phase_str = "DROPPING";
                            break;
                        case IndexLifecycle::kPendingPurge:
                            phase_str = "PENDING_PURGE";
                            break;
                        case IndexLifecycle::kPurging:
                            phase_str = "PURGING";
                            break;
                        case IndexLifecycle::kPurged:
                            phase_str = "PURGED";
                            break;
                        default:
                            break;
                        }
                    }
                    progress = runtime->progress();
                    delta_backlog = runtime->deltaBacklog();
                    started_at = runtime->startedAtUnixMs();
                    last_error = runtime->lastError();
                }
            }
            row.push_back(std::move(phase_str));
            row.push_back(std::to_string(static_cast<int64_t>(progress)));
            row.push_back(std::to_string(delta_backlog));
            row.push_back(std::to_string(started_at));
            row.push_back(std::move(last_error));
            result.rows.push_back(std::move(row));
        }
    }
}

} // namespace compute
} // namespace eugraph
