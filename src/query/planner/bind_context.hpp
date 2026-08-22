#pragma once

#include "common/types/graph_types.hpp"
#include "query/planner/bound_type.hpp"
#include "query/planner/slot_id.hpp"

#include <algorithm>
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
    /// Column index in the DataChunk (0-based).
    uint32_t column_index = 0;
    /// Globally-unique logical slot (assigned by Binder, immutable).
    SlotId slot_id = INVALID_SLOT_ID;
    /// For columns sourced from specific labels (e.g., multi-label nodes).
    std::vector<LabelId> source_labels;
    std::optional<uint16_t> source_prop_id;
    /// Whether this column came from a strong-typed access (n::Label.prop).
    bool strong_typed = false;
    /// Whether this variable was introduced by a CREATE clause.
    bool is_create_variable = false;
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
    /// Monotonic identifier for a binding scope. Scope is a visibility /
    /// provenance concept; the semantic identity of a variable is its
    /// SlotId (VariableId), never ScopeId.
    using ScopeId = uint32_t;
    static constexpr ScopeId kRootScope = 0;

    /// Map from variable name to column information.
    /// Scope-local: WITH clauses reset this to just their outputs, so names
    /// projected by an earlier WITH disappear here even though operators in
    /// the bound tree (Aggregate output_names, Filter predicates) still
    /// reference their original slot_id.
    std::unordered_map<std::string, ColumnInfo> symbols;
    /// Scope-aware binding record: (ScopeId, name) → SlotId.
    std::unordered_map<ScopeId, std::unordered_map<std::string, SlotId>> scoped_bindings;
    /// Ordered log of bindings. Used to seed the planner's name-based
    /// var_slots in binding order until DPL consumes scoped_bindings directly.
    struct BindingRecord {
        ScopeId scope = kRootScope;
        std::string name;
        SlotId slot = INVALID_SLOT_ID;
    };
    std::vector<BindingRecord> binding_order;
    /// Scope chain (root first). Used for visibility lookup.
    struct ScopeInfo {
        ScopeId id = kRootScope;
        ScopeId parent = kRootScope;
    };
    std::vector<ScopeInfo> scope_stack{{kRootScope, kRootScope}};
    /// Current binding scope. Root scope is kRootScope.
    ScopeId current_scope = kRootScope;
    /// Next ScopeId to hand out. ScopeIds are never reused within a query.
    ScopeId next_scope_id = kRootScope + 1;
    /// Accumulated property requirements for projection pushdown.
    std::vector<PropertyRequirement> property_requirements;
    /// Ordered output columns from RETURN clause (populated by bindReturn).
    std::vector<ColumnInfo> return_columns;
    /// Next column index to assign.
    uint32_t next_column_index = 0;
    /// Global SlotId allocator.  SlotIds survive sub-scope resets
    /// (beginSubScope) — they are query-global, not scope-local.
    SlotAllocator slot_allocator;

    /// Record a new binding in the current scope. A new binding must always
    /// carry a freshly allocated SlotId; callers must not reuse a slot across
    /// bindings.
    void registerBinding(const std::string& name, SlotId slot) {
        scoped_bindings[current_scope][name] = slot;
        binding_order.push_back({current_scope, name, slot});
    }

    /// Look up a binding in the current scope only. Returns INVALID_SLOT_ID
    /// when absent.
    SlotId lookupBindingInCurrentScope(const std::string& name) const {
        auto scope_it = scoped_bindings.find(current_scope);
        if (scope_it == scoped_bindings.end())
            return INVALID_SLOT_ID;
        auto it = scope_it->second.find(name);
        return it == scope_it->second.end() ? INVALID_SLOT_ID : it->second;
    }

    /// Visibility lookup: current scope, then parents.
    SlotId lookupBinding(const std::string& name) const {
        ScopeId scope = current_scope;
        while (true) {
            auto scope_it = scoped_bindings.find(scope);
            if (scope_it != scoped_bindings.end()) {
                auto it = scope_it->second.find(name);
                if (it != scope_it->second.end())
                    return it->second;
            }
            auto info_it = std::find_if(scope_stack.begin(), scope_stack.end(),
                                        [scope](const ScopeInfo& s) { return s.id == scope; });
            if (info_it == scope_stack.end() || info_it->parent == scope)
                return INVALID_SLOT_ID;
            scope = info_it->parent;
        }
    }

    /// Register a new variable in the symbol table. Returns the assigned column index.
    uint32_t registerVariable(const std::string& name, BoundType type) {
        auto [it, inserted] = symbols.emplace(name, ColumnInfo{name, std::move(type), 0, 0, {}, std::nullopt, false});
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

    /// Save the current binding state for EXISTS sub-plan scoping.
    /// Returns a copy of the state that can be restored later.
    struct Snapshot {
        std::unordered_map<std::string, ColumnInfo> symbols;
        uint32_t next_column_index = 0;
        ScopeId current_scope = kRootScope;
        std::vector<ScopeInfo> scope_stack{{kRootScope, kRootScope}};
    };

    Snapshot save() const {
        return {symbols, next_column_index, current_scope, scope_stack};
    }

    /// Restore binding state from a previously saved snapshot.
    void restore(const Snapshot& snap) {
        symbols = snap.symbols;
        next_column_index = snap.next_column_index;
        current_scope = snap.current_scope;
        scope_stack = snap.scope_stack;
    }

    /// Reset to an independent scope for EXISTS sub-plan binding.
    /// Only the given correlated variables are visible (registered in the sub-scope).
    void beginSubScope() {
        symbols.clear();
        next_column_index = 0;
        ScopeId child = next_scope_id++;
        scope_stack.push_back({child, current_scope});
        current_scope = child;
    }
};

} // namespace binder
} // namespace eugraph
