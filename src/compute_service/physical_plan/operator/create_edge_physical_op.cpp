#include "compute_service/physical_plan/operator/create_edge_physical_op.hpp"
#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "compute_service/executor/row.hpp"
#include "storage/kv/value_codec.hpp"

#include <spdlog/spdlog.h>

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<RowBatch> CreateEdgePhysicalOp::execute() {
    if (child_) {
        auto child_gen = child_->execute();
        while (auto batch = co_await child_gen.next()) {
            // Discard child output
        }
    }

    // Look up edge label definition for index maintenance
    const EdgeLabelDef* edge_label_def = nullptr;
    {
        auto def_it = edge_label_defs_.find(label_id_);
        if (def_it != edge_label_defs_.end()) {
            edge_label_def = &def_it->second;
        }
    }

    // Pass 1: check unique constraints for all relevant indexes
    bool ok = true;
    if (edge_label_def) {
        for (const auto& idx : edge_label_def->indexes) {
            if (!idx.unique)
                continue;
            if (idx.state != IndexState::WRITE_ONLY && idx.state != IndexState::PUBLIC)
                continue;

            // Collect all indexed property values; skip if any is missing
            std::vector<PropertyValue> values;
            bool allPresent = true;
            for (auto prop_id : idx.prop_ids) {
                if (prop_id < props_.size() && props_[prop_id].has_value()) {
                    values.push_back(props_[prop_id].value());
                } else {
                    allPresent = false;
                    break;
                }
            }
            if (!allPresent)
                continue;

            auto table = idx.prop_ids.size() == 1 ? eidxTable(label_id_, idx.prop_ids[0])
                                                  : eidxCompositeTable(label_id_, idx.prop_ids);
            bool constraint_ok = co_await store_.checkUniqueConstraint(table, values);
            if (!constraint_ok) {
                spdlog::warn("Unique edge index constraint violated on index '{}'", idx.name);
                ok = false;
                break;
            }
        }
    }

    if (ok)
        ok = co_await store_.insertEdge(assigned_eid_, src_id_, dst_id_, label_id_, 0, props_);

    // Pass 2: insert index entries for all WRITE_ONLY/PUBLIC indexes
    if (ok && edge_label_def) {
        for (const auto& idx : edge_label_def->indexes) {
            if (idx.state != IndexState::WRITE_ONLY && idx.state != IndexState::PUBLIC)
                continue;

            // Collect all indexed property values; skip if any is missing
            std::vector<PropertyValue> values;
            bool allPresent = true;
            for (auto prop_id : idx.prop_ids) {
                if (prop_id < props_.size() && props_[prop_id].has_value()) {
                    values.push_back(props_[prop_id].value());
                } else {
                    allPresent = false;
                    break;
                }
            }
            if (!allPresent)
                continue;

            auto adj_value = ValueCodec::encodeEdgeAdjacency(src_id_, dst_id_, 0, label_id_);
            auto table = idx.prop_ids.size() == 1 ? eidxTable(label_id_, idx.prop_ids[0])
                                                  : eidxCompositeTable(label_id_, idx.prop_ids);
            co_await store_.insertIndexEntry(table, values, assigned_eid_, std::move(adj_value));
        }
    }

    RowBatch output;
    if (ok) {
        EdgeValue ev;
        ev.id = assigned_eid_;
        ev.src_id = src_id_;
        ev.dst_id = dst_id_;
        ev.label_id = label_id_;
        // ev.properties left as nullopt; edge properties not fetched here
        Row row;
        row.push_back(Value(std::move(ev)));
        output.push_back(std::move(row));
    }
    if (!output.empty()) {
        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
