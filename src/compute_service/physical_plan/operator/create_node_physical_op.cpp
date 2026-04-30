#include "compute_service/physical_plan/operator/create_node_physical_op.hpp"
#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "compute_service/executor/row.hpp"

namespace eugraph {
namespace compute {

std::string CreateNodePhysicalOp::toString() const {
    std::string s;
    for (size_t i = 0; i < label_ids_.size(); i++) {
        if (i > 0)
            s += ", ";
        if (label_defs_) {
            auto it = label_defs_->find(label_ids_[i]);
            s += (it != label_defs_->end()) ? it->second.name : std::to_string(label_ids_[i]);
        } else {
            s += std::to_string(label_ids_[i]);
        }
    }
    return "CreateNode(variable=" + variable_ + ", labels=[" + s + "])";
}

folly::coro::AsyncGenerator<RowBatch> CreateNodePhysicalOp::execute() {
    if (child_) {
        auto child_gen = child_->execute();
        while (auto batch = co_await child_gen.next()) {
            // Discard child output — we just need the side effects
        }
    }

    bool ok = co_await store_.insertVertex(assigned_vid_, label_props_,
                                           nullptr // no primary key in Phase 1
    );

    if (ok && label_defs_) {
        for (const auto& [label_id, props] : label_props_) {
            auto def_it = label_defs_->find(label_id);
            if (def_it == label_defs_->end())
                continue;
            for (const auto& idx : def_it->second.indexes) {
                if (idx.state != IndexState::WRITE_ONLY && idx.state != IndexState::PUBLIC)
                    continue;
                for (auto prop_id : idx.prop_ids) {
                    if (prop_id < props.size() && props[prop_id].has_value()) {
                        auto table = vidxTable(label_id, prop_id);
                        co_await store_.insertIndexEntry(table, props[prop_id].value(), assigned_vid_);
                    }
                }
            }
        }
    }

    RowBatch output;
    if (ok) {
        // Build VertexValue with the created labels and properties
        VertexValue vv;
        vv.id = assigned_vid_;
        vv.labels = LabelIdSet(label_ids_.begin(), label_ids_.end());
        for (const auto& [lid, props] : label_props_) {
            vv.properties[lid] = props;
        }
        Row row;
        row.push_back(Value(std::move(vv)));
        output.push_back(std::move(row));
    }
    if (!output.empty()) {
        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
