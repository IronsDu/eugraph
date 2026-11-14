#include "query/physical_plan/operator/remove_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "query/dataset/cell_access.hpp"
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
                copyColumnCell(column, i, new_col, i);
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
                        // A label's properties belong to that label scope: removing
                        // the label deletes its record and every property row under
                        // it (removeVertexLabel does the cascade). They are NOT
                        // migrated into the weak space -- the weak space only holds
                        // keys that no label ever declared
                        // (docs/features/property-ownership.md §5).
                        co_await store_.removeVertexLabel(vid, lit->second);
                        auto new_index_entries =
                            co_await collectVertexIndexEntries(store_, label_defs_, vid, anon_label_id_);
                        co_await deleteVertexIndexEntries(store_, old_index_entries);
                        co_await insertVertexIndexEntries(store_, new_index_entries);
                        {
                            LabelIdSet remaining =
                                updated.labels.has_value() ? *updated.labels : co_await store_.getVertexLabels(vid);
                            remaining.erase(lit->second);
                            // The removed label's in-memory copy must go too, or a
                            // RETURN properties(n) in the same statement still shows
                            // the deleted values.
                            updated.properties.erase(lit->second);
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
