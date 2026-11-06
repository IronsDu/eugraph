#pragma once

#include "common/types/graph_types.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>

namespace eugraph {
namespace optimizer {

// ============================================================
// MaterializationReq — what semantic data a variable needs loaded
//
// Per-variable requirement carried by PhysProp. Encoded as:
//   need_labels              — the variable's label set must be loaded
//                              (triggered by labels(n), weak-mode access,
//                              or runtime label-filter expressions)
//   need_props               — per-label property ID set that must be loaded
//                              (strong-mode access resolved by binder to
//                              (label_id, prop_id) pairs)
//
// A variable with an empty MaterializationReq needs nothing — topology-
// stage operators (Scan/Expand/VarLenExpand) can carry it as a bare ID.
// Enricher enforcers exist to satisfy non-empty requirements.
//
// Set semantics: need_props is a set union (a property loaded once stays
// loaded). need_labels is monotonic (once true, stays true).
// ============================================================
struct MaterializationReq {
    bool need_entire = false; // RETURN n → need full VertexValue/EdgeValue/PathValue
    bool need_labels = false;
    std::map<LabelId, std::set<uint16_t>> need_props;

    bool empty() const {
        return !need_entire && !need_labels && need_props.empty();
    }

    // Set-union another requirement into this one. Used when multiple
    // consumers of the same variable each declare their own needs.
    MaterializationReq& merge(const MaterializationReq& o) {
        need_entire = need_entire || o.need_entire;
        need_labels = need_labels || o.need_labels;
        for (const auto& [lid, props] : o.need_props) {
            auto& dst = need_props[lid];
            dst.insert(props.begin(), props.end());
        }
        return *this;
    }

    // Intersect another requirement with this one. Used to compute the
    // common subset that satisfies both (e.g. when picking the cheapest
    // Enricher placement that satisfies multiple consumers).
    MaterializationReq intersect(const MaterializationReq& o) const {
        MaterializationReq result;
        result.need_entire = need_entire && o.need_entire;
        result.need_labels = need_labels && o.need_labels;
        for (const auto& [lid, props] : need_props) {
            auto it = o.need_props.find(lid);
            if (it == o.need_props.end())
                continue;
            std::set<uint16_t> common;
            std::set_intersection(props.begin(), props.end(), it->second.begin(), it->second.end(),
                                  std::inserter(common, common.begin()));
            if (!common.empty())
                result.need_props[lid] = std::move(common);
        }
        return result;
    }

    // Does `this` (as provided materialization) satisfy `required`?
    // A provider satisfies a requirement if it loads at least the
    // required labels and at least the required properties per label.
    bool satisfies(const MaterializationReq& required) const {
        if (required.need_entire && !need_entire)
            return false;
        if (required.need_labels && !need_labels)
            return false;
        for (const auto& [lid, props] : required.need_props) {
            auto it = need_props.find(lid);
            if (it == need_props.end())
                return false;
            for (uint16_t pid : props) {
                if (it->second.find(pid) == it->second.end())
                    return false;
            }
        }
        return true;
    }

    bool operator==(const MaterializationReq& o) const {
        return need_entire == o.need_entire && need_labels == o.need_labels && need_props == o.need_props;
    }
    bool operator!=(const MaterializationReq& o) const {
        return !(*this == o);
    }

    // Total number of property cells requested across all labels.
    // Used as a coarse width estimate for Enricher cost.
    size_t totalPropertyCount() const {
        size_t n = 0;
        for (const auto& [_, props] : need_props)
            n += props.size();
        return n;
    }
};

// Variable name → materialization requirement. Keyed by variable name
// (stable across Cascades topology rewrites — variable names are part
// of the logical equivalence class).
using VarRequirements = std::map<std::string, MaterializationReq>;

// Set-union merge of two VarRequirements maps.
inline VarRequirements& mergeVarRequirements(VarRequirements& dst, const VarRequirements& src) {
    for (const auto& [var, req] : src) {
        dst[var].merge(req);
    }
    return dst;
}

// Does the provided set of materializations satisfy the required set?
// Empty entries in `required` are no-ops; missing variables in `provided`
// fail the check only if `required` actually needs something from them.
inline bool satisfiesVarRequirements(const VarRequirements& provided, const VarRequirements& required) {
    for (const auto& [var, req] : required) {
        if (req.empty())
            continue;
        auto it = provided.find(var);
        if (it == provided.end())
            return false;
        if (!it->second.satisfies(req))
            return false;
    }
    return true;
}

} // namespace optimizer
} // namespace eugraph
