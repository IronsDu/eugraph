#include "query/physical_plan/operator/create_node_physical_op.hpp"
#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "query/executor/row.hpp"
#include <spdlog/spdlog.h>

namespace eugraph {
namespace compute {

std::string CreateNodePhysicalOp::toString() const {
    std::string s;
    for (size_t i = 0; i < label_ids_.size(); i++) {
        if (i > 0)
            s += ", ";
        if (!label_defs_.empty()) {
            auto it = label_defs_.find(label_ids_[i]);
            s += (it != label_defs_.end()) ? it->second.name : std::to_string(label_ids_[i]);
        } else {
            s += std::to_string(label_ids_[i]);
        }
    }
    return "CreateNode(variable=" + variable_ + ", labels=[" + s + "])";
}

folly::coro::AsyncGenerator<DataChunk> CreateNodePhysicalOp::executeChunk() {
    if (child_) {
        auto child_gen = child_->executeChunk();
        while (auto chunk = co_await child_gen.next()) {
            // Discard child output — we just need the side effects
        }
    }

    // Check unique constraints before inserting vertex
    bool ok = true;
    if (!label_defs_.empty()) {
        for (const auto& [label_id, props] : label_props_) {
            auto def_it = label_defs_.find(label_id);
            if (def_it == label_defs_.end())
                continue;
            for (const auto& idx : def_it->second.indexes) {
                if (!idx.unique)
                    continue;
                if (idx.state != IndexState::WRITE_ONLY && idx.state != IndexState::PUBLIC)
                    continue;
                std::vector<PropertyValue> values;
                bool allPresent = true;
                for (auto prop_id : idx.prop_ids) {
                    if (prop_id < props.size() && props[prop_id].has_value()) {
                        values.push_back(props[prop_id].value());
                    } else {
                        allPresent = false;
                        break;
                    }
                }
                if (!allPresent)
                    continue;
                auto table = idx.prop_ids.size() == 1 ? vidxTable(label_id, idx.prop_ids[0])
                                                      : vidxCompositeTable(label_id, idx.prop_ids);
                bool constraint_ok = co_await store_.checkUniqueConstraint(table, values);
                if (!constraint_ok) {
                    spdlog::warn("Unique index constraint violated on index '{}'", idx.name);
                    ok = false;
                    break;
                }
            }
            if (!ok)
                break;
        }
    }

    if (ok)
        ok = co_await store_.insertVertex(assigned_vid_, label_props_);

    if (ok && !label_defs_.empty()) {
        for (const auto& [label_id, props] : label_props_) {
            auto def_it = label_defs_.find(label_id);
            if (def_it == label_defs_.end())
                continue;
            for (const auto& idx : def_it->second.indexes) {
                if (idx.state != IndexState::WRITE_ONLY && idx.state != IndexState::PUBLIC)
                    continue;
                std::vector<PropertyValue> values;
                bool allPresent = true;
                for (auto prop_id : idx.prop_ids) {
                    if (prop_id < props.size() && props[prop_id].has_value()) {
                        values.push_back(props[prop_id].value());
                    } else {
                        allPresent = false;
                        break;
                    }
                }
                if (!allPresent)
                    continue;
                auto table = idx.prop_ids.size() == 1 ? vidxTable(label_id, idx.prop_ids[0])
                                                      : vidxCompositeTable(label_id, idx.prop_ids);
                co_await store_.insertIndexEntry(table, values, assigned_vid_);
            }
        }
    }

    if (ok) {
        VertexValue vv;
        vv.id = assigned_vid_;
        vv.labels = LabelIdSet(label_ids_.begin(), label_ids_.end());
        for (const auto& [lid, props] : label_props_) {
            vv.properties[lid] = props;
        }

        DataChunk output;
        output.columns.push_back(Column::flat(binder::BoundTypeKind::VERTEX, 1));
        output.columns[0].setValue(0, Value(std::move(vv)));
        output.count = 1;
        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
