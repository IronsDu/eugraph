#include "compute_service/physical_plan/operator/expand_physical_op.hpp"
#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "compute_service/executor/row.hpp"

namespace eugraph {
namespace compute {

std::string ExpandPhysicalOp::toString() const {
    std::string dir;
    switch (direction_) {
    case cypher::RelationshipDirection::LEFT_TO_RIGHT:
        dir = "OUT";
        break;
    case cypher::RelationshipDirection::RIGHT_TO_LEFT:
        dir = "IN";
        break;
    case cypher::RelationshipDirection::UNDIRECTED:
        dir = "ANY";
        break;
    }
    auto s = "Expand(src=" + src_var_ + ", dst=" + dst_var_;
    if (!edge_var_.empty())
        s += ", edge=" + edge_var_;
    if (label_filters_ && !label_filters_->empty()) {
        s += ", labels=[";
        for (size_t i = 0; i < label_filters_->size(); ++i) {
            if (i > 0)
                s += ",";
            s += std::to_string((*label_filters_)[i]);
        }
        s += "]";
    }
    s += ", direction=" + dir + ")";
    return s;
}

folly::coro::AsyncGenerator<RowBatch> ExpandPhysicalOp::execute() {
    auto child_gen = child_->execute();

    auto dir = Direction::OUT;
    if (direction_ == cypher::RelationshipDirection::RIGHT_TO_LEFT) {
        dir = Direction::IN;
    } else if (direction_ == cypher::RelationshipDirection::UNDIRECTED) {
        dir = Direction::BOTH;
    }

    std::vector<std::optional<EdgeLabelId>> scan_filters;
    if (!label_filters_.has_value()) {
        scan_filters.push_back(std::nullopt);
    } else {
        for (auto lid : *label_filters_) {
            scan_filters.push_back(lid);
        }
    }

    while (auto child_batch = co_await child_gen.next()) {
        RowBatch output;

        for (const auto& row : child_batch->rows) {
            VertexId src_id = INVALID_VERTEX_ID;
            if (src_col_idx_ >= 0 && static_cast<size_t>(src_col_idx_) < row.size()) {
                const auto& val = row[src_col_idx_];
                if (std::holds_alternative<VertexValue>(val)) {
                    src_id = std::get<VertexValue>(val).id;
                } else if (std::holds_alternative<int64_t>(val)) {
                    src_id = static_cast<VertexId>(std::get<int64_t>(val));
                }
            }
            if (src_id == INVALID_VERTEX_ID) {
                continue;
            }

            for (const auto& label_filter : scan_filters) {
                auto edge_gen = store_.scanEdges(src_id, dir, label_filter);
                while (auto edge_batch = co_await edge_gen.next()) {
                    for (const auto& entry : *edge_batch) {
                        Row new_row = row;

                        auto dst_labels = co_await store_.getVertexLabels(entry.neighbor_id);
                        VertexValue dst_vv;
                        dst_vv.id = entry.neighbor_id;
                        dst_vv.labels = dst_labels;
                        Properties merged_props;
                        for (LabelId lid : dst_labels) {
                            auto props = co_await store_.getVertexProperties(entry.neighbor_id, lid);
                            if (props) {
                                if (merged_props.size() < props->size()) {
                                    merged_props.resize(props->size());
                                }
                                for (size_t i = 0; i < props->size(); i++) {
                                    if ((*props)[i].has_value()) {
                                        merged_props[i] = std::move((*props)[i]);
                                    }
                                }
                            }
                        }
                        dst_vv.properties = std::move(merged_props);
                        new_row.push_back(Value(std::move(dst_vv)));

                        if (!edge_var_.empty()) {
                            EdgeValue ev;
                            ev.id = entry.edge_id;
                            ev.src_id = src_id;
                            ev.dst_id = entry.neighbor_id;
                            ev.label_id = entry.edge_label_id;
                            new_row.push_back(Value(std::move(ev)));
                        }
                        output.push_back(std::move(new_row));

                        if (output.size() >= RowBatch::CAPACITY) {
                            co_yield std::move(output);
                            output.clear();
                        }
                    }
                }
            }
        }

        if (!output.empty()) {
            co_yield std::move(output);
        }
    }
}

} // namespace compute
} // namespace eugraph
