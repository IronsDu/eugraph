#include "query/physical_plan/operator/create_edge_physical_op.hpp"
#include "common/types/constants.hpp"
#include "common/types/graph_types.hpp"
#include "query/executor/row.hpp"
#include "storage/kv/value_codec.hpp"

#include <spdlog/spdlog.h>

namespace eugraph {
namespace compute {

static PropertyValue valueToPropertyValue(const Value& v) {
    if (std::holds_alternative<bool>(v))
        return std::get<bool>(v);
    if (std::holds_alternative<int64_t>(v))
        return std::get<int64_t>(v);
    if (std::holds_alternative<double>(v))
        return std::get<double>(v);
    if (std::holds_alternative<std::string>(v))
        return std::get<std::string>(v);
    return PropertyValue{};
}

folly::coro::AsyncGenerator<DataChunk> CreateEdgePhysicalOp::executeChunk() {
    if (child_) {
        auto child_gen = child_->executeChunk();
        while (auto chunk = co_await child_gen.next()) {
            // Discard child output
        }
    }

    // Resolve effective label_id: use label_id_ if present, otherwise try label_name_
    EdgeLabelId effective_label_id = INVALID_EDGE_LABEL_ID;
    if (label_id_.has_value()) {
        effective_label_id = *label_id_;
    } else if (label_name_.has_value()) {
        auto it = edge_label_name_to_id_.find(*label_name_);
        if (it != edge_label_name_to_id_.end()) {
            effective_label_id = it->second;
        }
    }

    const EdgeLabelDef* edge_label_def = nullptr;
    if (effective_label_id != INVALID_EDGE_LABEL_ID) {
        auto def_it = edge_label_defs_.find(effective_label_id);
        if (def_it != edge_label_defs_.end()) {
            edge_label_def = &def_it->second;
        }
    }

    // Resolve pending_props_ (by name) after child operators may have created/altered the edge label
    for (auto& [prop_name, expr] : pending_props_) {
        if (!edge_label_def)
            continue;
        for (const auto& pd : edge_label_def->properties) {
            if (pd.name == prop_name) {
                if (std::holds_alternative<binder::BoundLiteral>(expr)) {
                    auto& lit = std::get<binder::BoundLiteral>(expr);
                    if (!isNull(lit.value)) {
                        if (props_.size() <= pd.id)
                            props_.resize(pd.id + 1);
                        props_[pd.id] = valueToPropertyValue(lit.value);
                    }
                }
                break;
            }
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

            auto table = idx.prop_ids.size() == 1 ? eidxTable(effective_label_id, idx.prop_ids[0])
                                                  : eidxCompositeTable(effective_label_id, idx.prop_ids);
            bool constraint_ok = co_await store_.checkUniqueConstraint(table, values);
            if (!constraint_ok) {
                spdlog::warn("Unique edge index constraint violated on index '{}'", idx.name);
                ok = false;
                break;
            }
        }
    }

    if (ok)
        ok = co_await store_.insertEdge(assigned_eid_, src_id_, dst_id_, effective_label_id, 0, props_);

    // Pass 2: insert index entries for all WRITE_ONLY/PUBLIC indexes
    if (ok && edge_label_def) {
        for (const auto& idx : edge_label_def->indexes) {
            if (idx.state != IndexState::WRITE_ONLY && idx.state != IndexState::PUBLIC)
                continue;

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

            auto adj_value = ValueCodec::encodeEdgeAdjacency(src_id_, dst_id_, 0, effective_label_id);
            auto table = idx.prop_ids.size() == 1 ? eidxTable(effective_label_id, idx.prop_ids[0])
                                                  : eidxCompositeTable(effective_label_id, idx.prop_ids);
            co_await store_.insertIndexEntry(table, values, assigned_eid_, std::move(adj_value));
        }
    }

    if (ok) {
        EdgeValue ev;
        ev.id = assigned_eid_;
        ev.src_id = src_id_;
        ev.dst_id = dst_id_;
        ev.label_id = effective_label_id;

        DataChunk output;
        output.columns.push_back(Column::flat(binder::BoundTypeKind::EDGE, 1));
        output.columns[0].setValue(0, Value(std::move(ev)));
        output.count = 1;
        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
