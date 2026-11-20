/// 索引构建的 SCAN + FINALIZE 实现（与 QueryExecutor/DDL 解耦）。
///
/// 依赖只有 storage 层：AsyncGraphDataStore（语句会话门面）、SessionPool、
/// IndexRuntimeRegistry（进度/准入）和 GraphSchema 快照（weak accessor 解析）。
/// 不依赖 query 层 ⇒ 可用真实 store 或 fake 直接单测，后台线程也不会重入执行器。

#include "storage/index/index_builder.hpp"

#include "common/config/env_vars.hpp"
#include "storage/kv/value_codec.hpp"

#include <algorithm>
#include <cstdlib>
#include <spdlog/spdlog.h>

namespace eugraph {

namespace {

size_t backfillBatchRows() {
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

bool cancelled(const IndexBuildContext& ctx) {
    return ctx.is_cancelled && ctx.is_cancelled();
}

folly::coro::Task<IndexBuildOutcome> buildVertex(const IndexBuildContext& ctx, const IndexBuildSpec& spec) {
    auto scan_lease = ctx.sessions.acquire();
    auto write_lease = ctx.sessions.acquire();
    if (!scan_lease || !write_lease)
        co_return IndexBuildOutcome{false, false, "Failed to acquire sessions for index backfill (pool exhausted)"};

    auto scan_store = ctx.data.for_session(**scan_lease);
    auto write_store = ctx.data.for_session(**write_lease);

    size_t scan_scanned = 0;
    size_t scanned_with_all_props = 0;
    size_t emitted_entries = 0;
    std::string failure_message;

    const auto set_progress = [&]() {
        if (ctx.registry == nullptr || spec.total_entities == 0)
            return;
        if (auto runtime = ctx.registry->find(spec.index_id)) {
            runtime->setProgress(100.0 * static_cast<double>(std::min<uint64_t>(scan_scanned, spec.total_entities)) /
                                 static_cast<double>(spec.total_entities));
        }
    };

    {
        const size_t batch_rows = backfillBatchRows();
        std::vector<VertexId> pending;
        pending.reserve(batch_rows);

        auto flush_batch = [&]() -> folly::coro::Task<bool> {
            if (pending.empty())
                co_return true;
            if (cancelled(ctx)) {
                failure_message = "Index build cancelled";
                co_return false;
            }
            if (!co_await write_store.beginSession(true)) {
                failure_message = "Failed to begin index backfill transaction";
                co_return false;
            }

            for (auto vid : pending) {
                std::vector<PropertyValue> values;
                bool allPresent = true;
                bool conflict = false;

                auto vertex_labels = co_await write_store.getVertexLabels(vid);
                for (const auto& accessor : spec.accessors) {
                    if (accessor.is_strong) {
                        auto props_opt = co_await write_store.getVertexProperties(vid, accessor.source_label_id);
                        if (!props_opt || accessor.source_prop_id >= props_opt->size() ||
                            !(*props_opt)[accessor.source_prop_id].has_value()) {
                            allPresent = false;
                            break;
                        }
                        values.push_back((*props_opt)[accessor.source_prop_id].value());
                        continue;
                    }

                    // Weak accessor：一个顶点只能有一个 home；多个 home 是歧义 ⇒ 构建失败。
                    int home_count = 0;
                    LabelId home_label = INVALID_LABEL_ID;
                    bool home_anon = false;
                    std::optional<PropertyValue> anon_value;
                    for (LabelId lid : vertex_labels) {
                        if (lid == spec.anon_label_id)
                            continue;
                        auto lab = spec.labels.find(lid);
                        if (lab == spec.labels.end())
                            continue;
                        for (const auto& pd : lab->second.properties) {
                            if (pd.name == accessor.property_name) {
                                ++home_count;
                                home_label = lid;
                                break;
                            }
                        }
                    }
                    if (spec.anon_label_id != INVALID_LABEL_ID && vertex_labels.count(spec.anon_label_id)) {
                        auto alab = spec.labels.find(spec.anon_label_id);
                        if (alab != spec.labels.end()) {
                            auto props_opt = co_await write_store.getVertexProperties(vid, spec.anon_label_id);
                            for (const auto& pd : alab->second.properties) {
                                if (pd.name != accessor.property_name)
                                    continue;
                                if (props_opt && pd.id < props_opt->size() && (*props_opt)[pd.id].has_value()) {
                                    ++home_count;
                                    home_anon = true;
                                    anon_value = (*props_opt)[pd.id].value();
                                }
                                break;
                            }
                        }
                    }
                    if (home_count > 1) {
                        spdlog::warn("Index '{}' weak accessor '{}' is ambiguous on vertex {} (multiple homes)",
                                     spec.index_name, accessor.property_name, vid);
                        conflict = true;
                        break;
                    }
                    if (home_count == 0) {
                        allPresent = false;
                        break;
                    }
                    if (home_anon) {
                        values.push_back(std::move(*anon_value));
                        continue;
                    }
                    auto props_opt = co_await write_store.getVertexProperties(vid, home_label);
                    uint16_t pid = UINT16_MAX;
                    auto lab = spec.labels.find(home_label);
                    if (lab != spec.labels.end()) {
                        for (const auto& pd : lab->second.properties) {
                            if (pd.name == accessor.property_name) {
                                pid = pd.id;
                                break;
                            }
                        }
                    }
                    if (!props_opt || pid == UINT16_MAX || pid >= props_opt->size() || !(*props_opt)[pid].has_value()) {
                        allPresent = false;
                        break;
                    }
                    values.push_back((*props_opt)[pid].value());
                }

                if (conflict) {
                    failure_message = "Index creation failed: conflicting values or duplicate values during backfill";
                    co_await write_store.rollbackSession();
                    co_return false;
                }
                if (!allPresent)
                    continue;
                ++scanned_with_all_props;

                if (spec.unique && !(co_await write_store.checkUniqueConstraint(spec.index_table, values))) {
                    spdlog::warn("Unique index '{}' backfill found duplicate value on vertex {}", spec.index_name, vid);
                    failure_message = "Index creation failed: duplicate values found during backfill";
                    co_await write_store.rollbackSession();
                    co_return false;
                }
                if (!co_await write_store.insertIndexEntry(spec.index_table, values, vid)) {
                    failure_message = "Index backfill failed to insert an entry";
                    co_await write_store.rollbackSession();
                    co_return false;
                }
                ++emitted_entries;
            }

            const auto outcome = co_await write_store.commitSession();
            if (!outcome.committed()) {
                // 不能忽略：否则索引会被标成 PUBLIC 而条目为空 ⇒ 静默漏结果。
                failure_message = "Index backfill transaction failed to commit (index left in ERROR, not ONLINE)";
                co_return false;
            }
            pending.clear();
            set_progress();
            index_hooks::point("scan.batch_committed", spec.index_id);
            co_return true;
        };

        std::vector<VertexId> scan_batch;
        auto gen = scan_store.scanVerticesByLabel(spec.label_id, scan_batch);
        bool ok = true;
        while (ok) {
            auto batch = co_await gen.next();
            if (!batch)
                break;
            for (auto vid : *batch) {
                pending.push_back(vid);
                ++scan_scanned;
                if (pending.size() < batch_rows)
                    continue;
                if (!(co_await flush_batch())) {
                    ok = false;
                    break;
                }
            }
        }
        if (ok && !pending.empty())
            ok = co_await flush_batch();
        if (!ok) {
            if (failure_message.empty())
                failure_message = "Index backfill failed (index left in ERROR)";
            co_return IndexBuildOutcome{false, cancelled(ctx), std::move(failure_message)};
        }
    }

    if (cancelled(ctx))
        co_return IndexBuildOutcome{false, true, "Index build cancelled"};

    IndexFinalizeContext finalize_ctx{ctx.data,      ctx.registry,           ctx.sessions,
                                      spec.index_id, spec.index_table,       spec.delta_table,
                                      spec.unique,   scanned_with_all_props, emitted_entries};
    const auto finalized = co_await finalizeIndexBuild(finalize_ctx);
    if (!finalized.ok)
        co_return IndexBuildOutcome{false, false, finalized.error};
    co_return IndexBuildOutcome{true, false, {}};
}

folly::coro::Task<IndexBuildOutcome> buildEdge(const IndexBuildContext& ctx, const IndexBuildSpec& spec) {
    auto scan_lease = ctx.sessions.acquire();
    auto write_lease = ctx.sessions.acquire();
    if (!scan_lease || !write_lease)
        co_return IndexBuildOutcome{false, false,
                                    "Failed to acquire sessions for edge index backfill (pool exhausted)"};

    auto scan_store = ctx.data.for_session(**scan_lease);
    auto write_store = ctx.data.for_session(**write_lease);

    size_t scanned_with_all_props = 0;
    size_t emitted_entries = 0;
    std::string failure_message;

    {
        const size_t batch_rows = backfillBatchRows();
        std::vector<ISyncGraphDataStore::EdgeTypeIndexEntry> pending;
        pending.reserve(batch_rows);

        auto flush_batch = [&]() -> folly::coro::Task<bool> {
            if (pending.empty())
                co_return true;
            if (cancelled(ctx)) {
                failure_message = "Edge index build cancelled";
                co_return false;
            }
            if (!co_await write_store.beginSession(true)) {
                failure_message = "Failed to begin edge index backfill transaction";
                co_return false;
            }

            for (const auto& entry : pending) {
                auto props_opt = co_await write_store.getEdgeProperties(spec.label_id, entry.edge_id);
                if (!props_opt.has_value())
                    continue;
                auto& props = *props_opt;
                std::vector<PropertyValue> values;
                bool allPresent = true;
                for (auto pid : spec.edge_prop_ids) {
                    if (pid < props.size() && props[pid].has_value()) {
                        values.push_back(props[pid].value());
                    } else {
                        allPresent = false;
                        break;
                    }
                }
                if (!allPresent)
                    continue;
                ++scanned_with_all_props;

                if (spec.unique && !(co_await write_store.checkUniqueConstraint(spec.index_table, values))) {
                    spdlog::warn("Unique edge index '{}' backfill found duplicate value on edge {}", spec.index_name,
                                 entry.edge_id);
                    failure_message = "Unique edge index creation failed: duplicate values found during backfill";
                    co_await write_store.rollbackSession();
                    co_return false;
                }
                auto adj_value =
                    ValueCodec::encodeEdgeAdjacency(entry.src_vertex_id, entry.dst_vertex_id, spec.label_id);
                if (!co_await write_store.insertIndexEntry(spec.index_table, values, entry.edge_id,
                                                           std::move(adj_value))) {
                    failure_message = "Edge index backfill failed to insert an entry";
                    co_await write_store.rollbackSession();
                    co_return false;
                }
                ++emitted_entries;
            }

            const auto outcome = co_await write_store.commitSession();
            if (!outcome.committed()) {
                failure_message = "Edge index backfill transaction failed to commit";
                co_return false;
            }
            pending.clear();
            index_hooks::point("scan.batch_committed", spec.index_id);
            co_return true;
        };

        std::vector<ISyncGraphDataStore::EdgeTypeIndexEntry> scan_batch;
        auto gen = scan_store.scanEdgesByType(spec.label_id, std::nullopt, std::nullopt, scan_batch);
        bool ok = true;
        while (ok) {
            auto batch = co_await gen.next();
            if (!batch)
                break;
            for (const auto& entry : *batch) {
                pending.push_back(entry);
                if (pending.size() < batch_rows)
                    continue;
                if (!(co_await flush_batch())) {
                    ok = false;
                    break;
                }
            }
        }
        if (ok && !pending.empty())
            ok = co_await flush_batch();
        if (!ok) {
            if (failure_message.empty())
                failure_message = "Edge index backfill failed";
            co_return IndexBuildOutcome{false, cancelled(ctx), std::move(failure_message)};
        }
    }

    if (cancelled(ctx))
        co_return IndexBuildOutcome{false, true, "Index build cancelled"};

    IndexFinalizeContext finalize_ctx{ctx.data,      ctx.registry,           ctx.sessions,
                                      spec.index_id, spec.index_table,       spec.delta_table,
                                      spec.unique,   scanned_with_all_props, emitted_entries};
    const auto finalized = co_await finalizeIndexBuild(finalize_ctx);
    if (!finalized.ok)
        co_return IndexBuildOutcome{false, false, finalized.error};
    co_return IndexBuildOutcome{true, false, {}};
}

} // namespace

folly::coro::Task<IndexBuildOutcome> runIndexBuild(const IndexBuildContext& ctx, const IndexBuildSpec& spec) {
    if (spec.is_edge)
        co_return co_await buildEdge(ctx, spec);
    co_return co_await buildVertex(ctx, spec);
}

} // namespace eugraph
