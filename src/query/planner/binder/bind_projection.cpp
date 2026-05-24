#include "query/planner/binder.hpp"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace eugraph {
namespace binder {

// ==================== Projection Pushdown ====================

void Binder::addAllPropertiesForVariable(const std::string& var_name) {
    auto* col = ctx_.lookup(var_name);
    if (!col)
        return;

    // RETURN n means the whole vertex — we need all properties from all labels
    // because the vertex may have multiple labels at runtime
    for (const auto& [lid, ldef] : catalog_.allLabels()) {
        for (const auto& pd : ldef.properties) {
            ctx_.addPropertyRequirement(var_name, lid, pd.id);
        }
    }
}

void Binder::collectLabelPropIds(const std::string& var_name, std::unordered_map<LabelId, std::vector<uint16_t>>& out) {
    for (const auto& req : ctx_.property_requirements) {
        if (req.variable_name != var_name)
            continue;
        if (req.label_id) {
            auto& ids = out[*req.label_id];
            for (uint16_t pid : req.prop_ids) {
                if (std::find(ids.begin(), ids.end(), pid) == ids.end())
                    ids.push_back(pid);
            }
        } else {
            // Weak-typed: apply to all labels that have these properties
            for (const auto& [lid, ldef] : catalog_.allLabels()) {
                auto& ids = out[lid];
                for (uint16_t pid : req.prop_ids) {
                    bool has_prop = false;
                    for (const auto& pd : ldef.properties) {
                        if (pd.id == pid) {
                            has_prop = true;
                            break;
                        }
                    }
                    if (has_prop && std::find(ids.begin(), ids.end(), pid) == ids.end())
                        ids.push_back(pid);
                }
            }
        }
    }
}

void Binder::applyProjectionPushdown(BoundLogicalOperator& op) {
    std::visit(
        [this](auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, BoundSingletonOp>) {
                // No children, no properties to collect
            } else if constexpr (std::is_same_v<T, BoundCorrelatedSourceOp>) {
                // Leaf operator, no children
            } else if constexpr (std::is_same_v<T, BoundLabelScanOp>) {
                collectLabelPropIds(val.variable, val.label_prop_ids);
            } else if constexpr (std::is_same_v<T, BoundScanOp>) {
                collectLabelPropIds(val.variable, val.label_prop_ids);
            } else {
                using Elem = typename T::element_type;
                auto& v = *val;
                if constexpr (std::is_same_v<Elem, BoundExpandOp>) {
                    collectLabelPropIds(v.dst_variable, v.dst_label_prop_ids);
                    for (const auto& req : ctx_.property_requirements) {
                        if (req.variable_name == v.edge_variable && req.label_id) {
                            for (uint16_t pid : req.prop_ids) {
                                if (std::find(v.edge_prop_ids.begin(), v.edge_prop_ids.end(), pid) ==
                                    v.edge_prop_ids.end())
                                    v.edge_prop_ids.push_back(pid);
                            }
                        }
                    }
                    applyProjectionPushdown(v.child);
                } else if constexpr (std::is_same_v<Elem, BoundVarLenExpandOp>) {
                    collectLabelPropIds(v.dst_variable, v.dst_label_prop_ids);
                    applyProjectionPushdown(v.child);
                } else if constexpr (std::is_same_v<Elem, BoundFilterOp>) {
                    applyProjectionPushdown(v.child);
                } else if constexpr (std::is_same_v<Elem, BoundProjectOp>) {
                    applyProjectionPushdown(v.child);
                } else if constexpr (std::is_same_v<Elem, BoundSortOp>) {
                    applyProjectionPushdown(v.child);
                } else if constexpr (std::is_same_v<Elem, BoundAggregateOp>) {
                    applyProjectionPushdown(v.child);
                } else if constexpr (std::is_same_v<Elem, BoundSkipOp>) {
                    applyProjectionPushdown(v.child);
                } else if constexpr (std::is_same_v<Elem, BoundLimitOp>) {
                    applyProjectionPushdown(v.child);
                } else if constexpr (std::is_same_v<Elem, BoundDistinctOp>) {
                    applyProjectionPushdown(v.child);
                } else if constexpr (std::is_same_v<Elem, BoundSetOp>) {
                    applyProjectionPushdown(v.child);
                } else if constexpr (std::is_same_v<Elem, BoundRemoveOp>) {
                    applyProjectionPushdown(v.child);
                } else if constexpr (std::is_same_v<Elem, BoundDeleteOp>) {
                    applyProjectionPushdown(v.child);
                } else if constexpr (std::is_same_v<Elem, BoundPathBuildOp>) {
                    applyProjectionPushdown(v.child);
                } else if constexpr (std::is_same_v<Elem, BoundBinaryJoinOp>) {
                    applyProjectionPushdown(v.left);
                    applyProjectionPushdown(v.right);
                } else if constexpr (std::is_same_v<Elem, BoundSemiJoinOp>) {
                    applyProjectionPushdown(v.left);
                    applyProjectionPushdown(v.right);
                } else if constexpr (std::is_same_v<Elem, BoundUnwindOp>) {
                    applyProjectionPushdown(v.child);
                }
                // BoundCreateNodeOp, BoundCreateEdgeOp: no child traversal needed
            }
        },
        op);
}

} // namespace binder
} // namespace eugraph
