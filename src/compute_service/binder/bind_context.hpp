#pragma once

#include "common/types/graph_types.hpp"
#include "compute_service/binder/bound_type.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace binder {

/// Describes a column in the row/chunk schema after binding.
struct ColumnInfo {
    std::string name;
    BoundType type;
    /// For columns sourced from a specific property on a label.
    std::optional<LabelId> source_label;
    std::optional<uint16_t> source_prop_id;
    /// Whether this column came from a strong-typed access (n::Label.prop).
    bool strong_typed = false;
};

/// Property requirement pushed down to scan operators.
struct PropertyRequirement {
    /// The variable name whose properties are needed.
    std::string variable_name;
    /// If set, only properties for this label are needed (strong-type access).
    std::optional<LabelId> label_id;
    /// Required property IDs for the given label (or all labels if label_id is nullopt).
    std::vector<uint16_t> prop_ids;
};

/// Binding context — shared state during AST traversal.
struct BindContext {
    /// Map from variable name to column information.
    std::unordered_map<std::string, ColumnInfo> symbols;
    /// Accumulated property requirements for projection pushdown.
    std::vector<PropertyRequirement> property_requirements;
    /// Next column index to assign.
    uint32_t next_column_index = 0;

    /// Register a new variable in the symbol table. Returns the assigned column index.
    uint32_t registerVariable(const std::string& name, BoundType type) {
        auto [it, inserted] =
            symbols.emplace(name, ColumnInfo{name, std::move(type), std::nullopt, std::nullopt, false});
        if (inserted) {
            // Assign column index only on first registration
            // (we use a separate pass to assign indices in order)
        }
        return 0; // caller should assign the column index
    }

    /// Look up a variable. Returns nullptr if not found.
    const ColumnInfo* lookup(const std::string& name) const {
        auto it = symbols.find(name);
        if (it == symbols.end())
            return nullptr;
        return &it->second;
    }

    /// Add a property requirement for projection pushdown.
    void addPropertyRequirement(const std::string& var_name, std::optional<LabelId> label_id, uint16_t prop_id) {
        for (auto& req : property_requirements) {
            if (req.variable_name == var_name && req.label_id == label_id) {
                // Deduplicate
                for (uint16_t existing : req.prop_ids) {
                    if (existing == prop_id)
                        return;
                }
                req.prop_ids.push_back(prop_id);
                return;
            }
        }
        property_requirements.push_back({var_name, label_id, {prop_id}});
    }
};

} // namespace binder
} // namespace eugraph
