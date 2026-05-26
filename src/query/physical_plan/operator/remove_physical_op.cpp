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
        for (size_t row_idx = 0; row_idx < n; ++row_idx) {
            for (const auto& item : items_) {
                if (item.var_name.empty())
                    continue;

                int col = findColumn(input_schema_, item.var_name);
                if (col < 0 || static_cast<size_t>(col) >= chunk->numColumns())
                    continue;

                Value val = chunk->getValue(static_cast<size_t>(col), row_idx);

                if (std::holds_alternative<VertexValue>(val)) {
                    const auto& vertex = std::get<VertexValue>(val);
                    VertexId vid = vertex.id;

                    if (item.kind == BoundRemoveItem::Kind::LABEL) {
                        auto lit = label_name_to_id_.find(item.name);
                        if (lit == label_name_to_id_.end())
                            continue;
                        co_await store_.removeVertexLabel(vid, lit->second);
                    } else if (item.kind == BoundRemoveItem::Kind::PROPERTY) {
                        if (item.strong_mode && item.resolved_label_id && item.resolved_prop_id) {
                            // Strong mode: delete from specific label
                            co_await store_.deleteVertexProperty(vid, *item.resolved_label_id, *item.resolved_prop_id);
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
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
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
