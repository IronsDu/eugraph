#include "query/physical_plan/operator/create_edge_label_physical_op.hpp"

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> CreateEdgeLabelPhysicalOp::executeChunk() {
    // Build PropertyDef vector from prop_defs_
    std::vector<PropertyDef> property_defs;
    uint16_t next_pid = 0;
    for (const auto& [name, type] : prop_defs_) {
        PropertyDef pd;
        pd.id = next_pid++;
        pd.name = name;
        pd.type = type;
        property_defs.push_back(std::move(pd));
    }

    EdgeLabelId id = co_await meta_.createEdgeLabel(label_name_, property_defs);

    // Also create the underlying WiredTiger data table
    if (id != INVALID_EDGE_LABEL_ID) {
        co_await store_.createEdgeLabel(id);
    }

    // Reload the full def (with property IDs) from meta store to keep shared maps in sync
    auto updated = co_await meta_.getEdgeLabelDef(label_name_);
    if (updated.has_value()) {
        name_to_id_[label_name_] = updated->id;
        defs_[updated->id] = std::move(*updated);
    } else {
        name_to_id_[label_name_] = id;
        EdgeLabelDef def;
        def.id = id;
        def.name = label_name_;
        def.properties = std::move(property_defs);
        defs_[id] = std::move(def);
    }

    auto child_gen = child_->executeChunk();
    while (auto chunk = co_await child_gen.next()) {
        co_yield std::move(*chunk);
    }
}

} // namespace compute
} // namespace eugraph
