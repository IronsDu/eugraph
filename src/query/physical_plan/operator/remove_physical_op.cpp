#include "query/physical_plan/operator/remove_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "query/physical_plan/operator/edge_index_maintenance.hpp"
#include "query/physical_plan/operator/vertex_index_maintenance.hpp"

namespace eugraph {
namespace compute {

namespace {

int findColumn(const Schema& schema, const std::string& name) {
    for (size_t i = 0; i < schema.size(); ++i) {
        if (schema[i] == name)
            return static_cast<int>(i);
    }
    return -1;
}

} // anonymous namespace

folly::coro::AsyncGenerator<DataChunk> RemovePhysicalOp::executeChunk() {
    auto child_gen = cancellable(child_->executeChunkTraced());

    while (auto chunk = co_await child_gen.next()) {
        size_t n = chunk->numRows();

        // Ensure columns that will be mutated are writable (FLAT).
        // DICTIONARY columns share data with other operators and are read-only.
        for (const auto& item : items_) {
            if (item.var_name.empty())
                continue;
            int col = item.object_col >= 0 ? item.object_col : findColumn(input_schema_, item.var_name);
            if (col < 0 || static_cast<size_t>(col) >= chunk->numColumns())
                continue;
            auto& column = chunk->columns[static_cast<size_t>(col)];
            if (column.form == VectorForm::FLAT)
                continue;
            // Copy DICTIONARY / CONSTANT column into a new FLAT column
            auto new_col = Column::flat(column.type, n);
            for (size_t i = 0; i < n; ++i)
                new_col.setValue(i, column.getValue(i));
            column = std::move(new_col);
        }

        for (size_t row_idx = 0; row_idx < n; ++row_idx) {
            for (const auto& item : items_) {
                if (item.var_name.empty())
                    continue;

                int col = item.object_col >= 0 ? item.object_col : findColumn(input_schema_, item.var_name);
                if (col < 0 || static_cast<size_t>(col) >= chunk->numColumns())
                    continue;

                // Re-read value each iteration so that multiple REMOVE items
                // on the same variable accumulate their modifications.
                Value val = chunk->getValue(static_cast<size_t>(col), row_idx);

                if (std::holds_alternative<VertexValuePtr>(val)) {
                    const auto& vertex = (*std::get<VertexValuePtr>(val));
                    VertexId vid = vertex.id;
                    VertexValue updated = vertex;
                    bool modified = false;
                    std::optional<std::vector<VertexIndexEntry>> old_index_entries;

                    if (item.kind == BoundRemoveItem::Kind::LABEL) {
                        auto lit = label_name_to_id_.find(item.name);
                        if (lit == label_name_to_id_.end())
                            continue;
                        auto old_index_entries =
                            co_await collectVertexIndexEntries(store_, label_defs_, vid, anon_label_id_);
                        // Cypher 语义：属性归属 vertex 而非 label。removeVertexLabel
                        // 内部会清空 vpropTable(label_id) 下属于该 vertex 的属性。无论
                        // 此 label 是不是最后一个，都要先把它的属性副本迁到 __anon__，
                        // 否则 REMOVE n:L1:L3 这种"部分移除"也会丢失属性（多 label
                        // CREATE 时属性只挂在第一个 label 下，参见 bind_mutation 的
                        // pending_props re-resolution）。
                        bool migrated_anon = false;
                        if (anon_label_id_ != INVALID_LABEL_ID && anon_label_id_ != lit->second) {
                            auto src_def_it = label_defs_.find(lit->second);
                            if (src_def_it != label_defs_.end()) {
                                // 属性不一定已随 VertexValue 预取（ProjectionExtract 只
                                // 物化查询需要的列）。REMOVE label 会删除该 label 的
                                // vprop 表，所以缺失时必须按 label 回读，否则属性丢失。
                                const Properties* src_props = nullptr;
                                std::optional<Properties> fetched_props;
                                auto src_props_it = updated.properties.find(lit->second);
                                if (src_props_it != updated.properties.end() && !src_props_it->second.empty()) {
                                    src_props = &src_props_it->second;
                                } else {
                                    fetched_props = co_await store_.getVertexProperties(vid, lit->second);
                                    if (fetched_props.has_value())
                                        src_props = &*fetched_props;
                                }
                                if (src_props != nullptr) {
                                    const auto& src_def = src_def_it->second;
                                    Properties& anon_props = updated.properties[anon_label_id_];
                                    for (const auto& pd : src_def.properties) {
                                        if (pd.id >= src_props->size())
                                            continue;
                                        const auto& pv = (*src_props)[pd.id];
                                        if (!pv.has_value())
                                            continue;
                                        uint16_t anon_pid =
                                            co_await meta_.getOrCreateAnonPropId(pd.name, PropertyType::ANY);
                                        co_await store_.putVertexProperty(vid, anon_label_id_, anon_pid, *pv);
                                        if (anon_props.size() <= anon_pid)
                                            anon_props.resize(anon_pid + 1);
                                        anon_props[anon_pid] = pv;
                                        migrated_anon = true;
                                    }
                                }
                            }
                        }
                        co_await store_.removeVertexLabel(vid, lit->second);
                        auto new_index_entries =
                            co_await collectVertexIndexEntries(store_, label_defs_, vid, anon_label_id_);
                        co_await deleteVertexIndexEntries(store_, old_index_entries);
                        co_await insertVertexIndexEntries(store_, new_index_entries);
                        // 最后一个用户 label 被移除后，顶点必须挂上内部 __anon__
                        // label：属性已迁到 __anon__ 表，ProjectionExtract 只对
                        // “带 anon label”的顶点预取 anon 属性。缺少这一步时属性
                        // 仍在存储里，但 keys(n)/n.prop 会看不到（REMOVE TCK 回归）。
                        {
                            LabelIdSet remaining =
                                updated.labels.has_value() ? *updated.labels : co_await store_.getVertexLabels(vid);
                            remaining.erase(lit->second);
                            // 两种情况都要挂内部 __anon__ label：
                            //  1) 已无用户 label（无标签顶点的属性容器）；
                            //  2) 本次把属性迁到了 __anon__，但还有其它 label —— 否则
                            //     ProjectionExtract 不会预取 anon 属性，keys(n) 丢属性。
                            const bool need_anon_label =
                                (remaining.empty() || migrated_anon) && anon_label_id_ != INVALID_LABEL_ID &&
                                anon_label_id_ != lit->second && !remaining.count(anon_label_id_);
                            if (need_anon_label) {
                                co_await store_.addVertexLabel(vid, anon_label_id_);
                                remaining.insert(anon_label_id_);
                            }
                            updated.labels = std::move(remaining);
                            modified = true;
                        }
                    } else if (item.kind == BoundRemoveItem::Kind::PROPERTY) {
                        old_index_entries =
                            co_await collectVertexIndexEntries(store_, label_defs_, vid, anon_label_id_);
                        if (item.strong_mode && item.resolved_label_id && item.resolved_prop_id) {
                            co_await store_.deleteVertexProperty(vid, *item.resolved_label_id, *item.resolved_prop_id);
                            auto pit = updated.properties.find(*item.resolved_label_id);
                            if (pit != updated.properties.end() && pit->second.size() > *item.resolved_prop_id) {
                                pit->second[*item.resolved_prop_id] = std::nullopt;
                                modified = true;
                            }
                        } else {
                            // Convenience mode: delete from ALL matching labels (no conflict error)
                            bool any_deleted = false;
                            if (vertex.labels.has_value()) {
                                for (LabelId lid : *vertex.labels) {
                                    auto def_it = label_defs_.find(lid);
                                    if (def_it == label_defs_.end())
                                        continue;
                                    for (const auto& pd : def_it->second.properties) {
                                        if (pd.name == item.name) {
                                            co_await store_.deleteVertexProperty(vid, lid, pd.id);
                                            any_deleted = true;
                                            auto pit = updated.properties.find(lid);
                                            if (pit != updated.properties.end() && pit->second.size() > pd.id) {
                                                pit->second[pd.id] = std::nullopt;
                                                modified = true;
                                            }
                                            break;
                                        }
                                    }
                                }
                            }
                            if (!any_deleted) {
                                // No match in actual labels: try __anon__
                                auto anon_it = label_defs_.find(anon_label_id_);
                                if (anon_it != label_defs_.end()) {
                                    for (const auto& pd : anon_it->second.properties) {
                                        if (pd.name == item.name) {
                                            co_await store_.deleteVertexProperty(vid, anon_label_id_, pd.id);
                                            auto pit = updated.properties.find(anon_label_id_);
                                            if (pit != updated.properties.end() && pit->second.size() > pd.id) {
                                                pit->second[pd.id] = std::nullopt;
                                                modified = true;
                                            }
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }

                    if (old_index_entries.has_value()) {
                        auto new_index_entries =
                            co_await collectVertexIndexEntries(store_, label_defs_, vid, anon_label_id_);
                        co_await deleteVertexIndexEntries(store_, *old_index_entries);
                        co_await insertVertexIndexEntries(store_, new_index_entries);
                    }

                    if (modified)
                        chunk->setValue(static_cast<size_t>(col), row_idx, Value(mk<VertexValue>(std::move(updated))));
                } else if (std::holds_alternative<EdgeValuePtr>(val)) {
                    if (item.kind != BoundRemoveItem::Kind::PROPERTY)
                        continue;

                    const auto& edge = (*std::get<EdgeValuePtr>(val));
                    auto def_it = edge_label_defs_.find(edge.label_id);
                    if (def_it == edge_label_defs_.end())
                        continue;
                    auto old_index_entries =
                        co_await collectEdgeIndexEntries(store_, edge_label_defs_, edge.id, edge.label_id);
                    for (const auto& pd : def_it->second.properties) {
                        if (pd.name == item.name) {
                            co_await store_.deleteEdgeProperty(edge.id, edge.label_id, pd.id);
                            // Mirror the deletion into the in-memory edge value so
                            // downstream RETURN/WITH observes the removed property.
                            EdgeValue updated = edge;
                            if (updated.properties.has_value() && updated.properties->size() > pd.id) {
                                (*updated.properties)[pd.id] = std::nullopt;
                                chunk->setValue(static_cast<size_t>(col), row_idx,
                                                Value(mk<EdgeValue>(std::move(updated))));
                            }
                            break;
                        }
                    }
                    auto new_index_entries =
                        co_await collectEdgeIndexEntries(store_, edge_label_defs_, edge.id, edge.label_id);
                    co_await deleteEdgeIndexEntries(store_, old_index_entries);
                    co_await insertEdgeIndexEntries(store_, new_index_entries);
                }
            }
        }
        co_yield std::move(*chunk);
    }
}

} // namespace compute
} // namespace eugraph
