#include "query/physical_plan/operator/remove_physical_op.hpp"
#include "common/types/graph_types.hpp"

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
    auto child_gen = child_->executeChunk();

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

                if (std::holds_alternative<VertexValue>(val)) {
                    const auto& vertex = std::get<VertexValue>(val);
                    VertexId vid = vertex.id;
                    VertexValue updated = vertex;
                    bool modified = false;

                    if (item.kind == BoundRemoveItem::Kind::LABEL) {
                        auto lit = label_name_to_id_.find(item.name);
                        if (lit == label_name_to_id_.end())
                            continue;
                        co_await store_.removeVertexLabel(vid, lit->second);
                        if (updated.labels.has_value()) {
                            updated.labels->erase(lit->second);
                            modified = true;
                        }
                    } else if (item.kind == BoundRemoveItem::Kind::PROPERTY) {
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

                    if (modified)
                        chunk->setValue(static_cast<size_t>(col), row_idx, Value(std::move(updated)));
                } else if (std::holds_alternative<EdgeValue>(val)) {
                    if (item.kind != BoundRemoveItem::Kind::PROPERTY)
                        continue;

                    const auto& edge = std::get<EdgeValue>(val);
                    auto def_it = edge_label_defs_.find(edge.label_id);
                    if (def_it == edge_label_defs_.end())
                        continue;
                    for (const auto& pd : def_it->second.properties) {
                        if (pd.name == item.name) {
                            co_await store_.deleteEdgeProperty(edge.id, edge.label_id, pd.id);
                            // Mirror the deletion into the in-memory edge value so
                            // downstream RETURN/WITH observes the removed property.
                            EdgeValue updated = edge;
                            if (updated.properties.has_value() && updated.properties->size() > pd.id) {
                                (*updated.properties)[pd.id] = std::nullopt;
                                chunk->setValue(static_cast<size_t>(col), row_idx, Value(std::move(updated)));
                            }
                            break;
                        }
                    }
                }
            }
        }
        co_yield std::move(*chunk);
    }
}

} // namespace compute
} // namespace eugraph
