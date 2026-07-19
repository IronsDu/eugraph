#pragma once

#include "common/types/graph_types.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"
#include "query/planner/bound_logical_plan.hpp"
#include "query/planner/slot_id.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace optimizer {

/// Rewrite pass that lowers graph semantics (BoundPropertyRef / standalone
/// variable refs) into slot-based references resolvable at runtime via
/// TupleSlotLayout. Implements the Demand-Pull + Expression Lowering design
/// (see docs/query/demand-pull-lowering-design.md).
///
/// Pipeline:
///   0. allocateAllSlots       — ensure every variable name (incl. aliases)
///                              has a slot_id in name_to_slot
///   1. collectAliasSlotMap    — for each Project `BoundVariableRef(X) AS Y`,
///                              record alias_slot → source_slot
///   2. collectPlanRequirements — gather per-slot demand (canonicalized)
///   3. collectSourceTypes     — record BoundTypeKind per source slot
///   4. buildExtractionInfo    — decide PE output columns + allocate slots
///   5. lowerAliasPassthrough  — propagate derived slots through alias chains
///   6. rewriteColumnIndices   — lower expressions using slot_ids
///
/// After this pass, every BoundColumnRef in the tree has a slot_id that
/// ProjectionExtract's output layout will provide at runtime.

/// Per-variable extraction plan. One entry per canonical source slot.
/// Replaces the old PropertyExtractionInfo; carries enough structure for
/// dispatchProjectionExtract to act as a pure consumer.
struct PEPlan {
    /// Canonical source slot of the variable (LabelScan/Expand/Create*).
    binder::SlotId source_slot_id = binder::INVALID_SLOT_ID;

    /// Slot of the appended full-object column. INVALID if no whole-object
    /// demand. Covering principle: when set, prop_*_slot_ids are unused —
    /// the runtime evaluator reads properties out of the constructed object.
    ///
    /// When the source column is already a constructed VERTEX/EDGE (e.g.
    /// CreateNode / Merge output), object_slot_id aliases source_slot_id —
    /// no new column is appended and dispatchProjectionExtract skips the
    /// Construct emit. The Lower stage still routes PropertyRef through
    /// this slot, which transparently hits the source column.
    binder::SlotId object_slot_id = binder::INVALID_SLOT_ID;

    /// Flat vertex property columns appended by PE. prop_order and
    /// prop_slot_ids are parallel: prop_slot_ids[i] is the slot for
    /// prop_order[i] = (label_id, prop_id).
    std::vector<std::pair<LabelId, uint16_t>> prop_order;
    std::vector<binder::SlotId> prop_slot_ids;

    /// Flat edge property columns appended by PE (parallel arrays).
    std::vector<std::pair<EdgeLabelId, uint16_t>> edge_prop_order;
    std::vector<binder::SlotId> edge_prop_slot_ids;

    /// Slot id for LoadVertexLabels column (need_vertex_labels).
    binder::SlotId labels_slot_id = binder::INVALID_SLOT_ID;
    /// Slot id for LoadEdgeType column (need_edge_type).
    binder::SlotId type_slot_id = binder::INVALID_SLOT_ID;
};

/// Per-variable requirement collected by scanning downstream operators.
/// Determines which ColumnSpec list ProjectionExtractPhysicalOp must emit.
struct VariableRequirement {
    /// RETURN n / SET n.x / REMOVE n.x / BoundDynamicPropertyRef on vertex:
    /// extract must construct a full VertexValue column (appended after the
    /// source VertexRef column).
    bool need_whole_vertex = false;
    /// RETURN r / BoundDynamicPropertyRef on edge: extract must append a
    /// full EdgeValue column.
    bool need_whole_edge = false;
    /// labels(n) / n::Label: extract appends a List<String> column.
    bool need_vertex_labels = false;
    /// type(r): extract appends a String column with the edge type name.
    bool need_edge_type = false;
    /// (label_id, prop_id) list for vertex property access. De-duplicated.
    std::vector<std::pair<LabelId, uint16_t>> vertex_props;
    /// (edge_label_id, prop_id) list for edge property access. De-duplicated.
    std::vector<std::pair<EdgeLabelId, uint16_t>> edge_props;

    bool empty() const {
        return !need_whole_vertex && !need_whole_edge && !need_vertex_labels && !need_edge_type &&
               vertex_props.empty() && edge_props.empty();
    }
};

using PlanRequirements = std::unordered_map<binder::SlotId, VariableRequirement>;
using PEPlans = std::unordered_map<binder::SlotId, PEPlan>;
using AliasSlotMap = std::unordered_map<binder::SlotId, binder::SlotId>;
using NameSlotMap = std::unordered_map<std::string, binder::SlotId>;
using SourceTypes = std::unordered_map<binder::SlotId, binder::BoundTypeKind>;

/// Resolve a slot through alias_map to its canonical root. Returns `slot`
/// unchanged if not in alias_map. Cycle-detected: returns the slot unchanged
/// after `kMaxDepth` hops (defensive; should not happen in well-formed plans).
binder::SlotId getCanonicalSlot(const AliasSlotMap& alias_map, binder::SlotId slot);

/// Read-only view over (NameSlotMap, AliasSlotMap) that funnels every
/// name→slot and slot→canonical resolution through one path. The maps remain
/// the storage — mutated only by allocateAllSlots / lowerAliasPassthrough —
/// but every read site goes through SlotResolver so the canonical-chain
/// invariant (alias_map entry must point at a slot that exists in
/// name_to_slot's value set) is enforced at a single query point.
///
/// Why this exists: before SlotResolver, collectExprReqs / collectOpReqs /
/// collectSourceTypesOp / rewriteExpr / lowerAliasPassthroughOp each had
/// their own local `canonicalOfName` lambda duplicating the
/// slotForName + getCanonicalSlot composition. Any future pass copying that
/// pattern could forget the canonical step and silently key on the alias
/// slot instead of the root, fragmenting requirements across two keys.
/// Funneling through canonicalForName / canonicalOf makes the correct path
/// the only path.
class SlotResolver {
public:
    SlotResolver(const NameSlotMap& names, const AliasSlotMap& aliases) : names_(names), aliases_(aliases) {}

    /// name → raw slot (no canonicalization). INVALID if name is empty/unknown.
    binder::SlotId slotForName(const std::string& name) const {
        if (name.empty())
            return binder::INVALID_SLOT_ID;
        auto it = names_.find(name);
        return it != names_.end() ? it->second : binder::INVALID_SLOT_ID;
    }

    /// slot → canonical slot (root of alias chain). Same slot if not aliased.
    binder::SlotId canonicalOf(binder::SlotId slot) const {
        return getCanonicalSlot(aliases_, slot);
    }

    /// name → canonical slot. INVALID if name unknown. Use this whenever a
    /// downstream collector / rewriter needs to key or look up by variable —
    /// skipping the canonical step fragments state across alias and root.
    binder::SlotId canonicalForName(const std::string& name) const {
        binder::SlotId slot = slotForName(name);
        return slot == binder::INVALID_SLOT_ID ? binder::INVALID_SLOT_ID : canonicalOf(slot);
    }

    const NameSlotMap& names() const {
        return names_;
    }
    const AliasSlotMap& aliases() const {
        return aliases_;
    }

private:
    const NameSlotMap& names_;
    const AliasSlotMap& aliases_;
};

/// Walk the bound tree and ensure every variable name (from BoundVariableRef,
/// BoundColumnRef, Project/Aggregate aliases, source-op output variables, etc.)
/// has a slot_id in `name_to_slot`. New slots come from `alloc.next()`.
/// Idempotent: existing entries are preserved.
void allocateAllSlots(const binder::BoundLogicalOperator& root, NameSlotMap& name_to_slot,
                      binder::SlotAllocator& alloc);

/// Build alias_slot_map by scanning Project items. For each item of the form
/// `BoundVariableRef(X) AS Y`, records alias_map[slot(Y)] = slot(X). Aggregate
/// group_keys of the same form are also recorded. Cycle-safe by construction
/// (a chain like `n→m→k` resolves via repeated lookup; cycles would indicate
/// a binder bug).
AliasSlotMap collectAliasSlotMap(const binder::BoundLogicalOperator& root, const NameSlotMap& name_to_slot);

/// Walk the bound tree and collect planner slot ids for every Expand /
/// VarLenExpand that (re-)introduces a variable name (binder dst/edge_slot_id
/// == INVALID). The returned map (name → slot) is passed to
/// collectPlanRequirements so whole-object demand is also keyed under the
/// fresh slot, preventing alias-chain conflation (§6.2).
NameSlotMap buildFreshExpandMap(const binder::BoundLogicalOperator& root);

/// Walk the entire plan and collect per-slot requirements from all downstream
/// consumers (Project / Filter / Sort / Aggregate / Set / Remove / Delete /
/// Merge). Variable references are resolved via `resolver.slotForName` and
/// canonicalized via `resolver.canonicalOf` before keying the result.
/// When `fresh_expands` is non-null, BoundVariableRef demand is additionally
/// keyed under the fresh expand slot so PEPlans are created for reintroduced
/// names that differ from the alias-chain result (§6.2).
PlanRequirements collectPlanRequirements(const binder::BoundLogicalOperator& root, const SlotResolver& resolver,
                                         const NameSlotMap* fresh_expands = nullptr);

/// Walk the bound plan and record, for each source slot, the BoundTypeKind of
/// the column produced by its defining operator. This tells buildExtractionInfo
/// whether the source is already a constructed object (VERTEX / EDGE — e.g.
/// from CreateNode / CreateEdge / Merge) or a topology form (VERTEX_REF /
/// EDGE_KEY — from LabelScan / Expand). When the source is already
/// constructed, PE must NOT append a redundant Construct column.
SourceTypes collectSourceTypes(const binder::BoundLogicalOperator& root, const SlotResolver& resolver,
                               const NameSlotMap* fresh_expands = nullptr);

/// Convert PlanRequirements into PEPlans, allocating slot_ids for every
/// appended PE column. New ids come from `alloc.next()`.
///
/// `source_types` drives the covering decision: when need_whole_* is raised
/// but the source column is already a constructed VERTEX/EDGE, no new object
/// column is allocated — object_slot_id aliases source_slot_id, and
/// dispatchProjectionExtract skips the Construct emit.
PEPlans buildExtractionInfo(const PlanRequirements& reqs, const SourceTypes& source_types, const SlotResolver& resolver,
                            binder::SlotAllocator& alloc);

/// Propagate derived PE columns through variable-alias chains. For each
/// Project item of the form `BoundVariableRef(X) AS Y`:
///   - Look up PEPlan[X_slot] and enumerate its derived slots (object / props
///     / labels / type).
///   - For each derived slot, allocate a fresh alias slot, record
///     alias_map[alias_slot] = derived_slot, and append a passthrough item
///     `BoundColumnRef(derived_slot) AS __alias_<Y>_<kind>` to the Project so
///     the physical planner emits it in the Project's output.
///
/// Must run AFTER buildExtractionInfo and BEFORE rewriteColumnIndices so the
/// Lower stage sees the propagated alias slots in name_to_slot / alias_map.
/// This pass mutates name_to_slot / alias_map, so it takes them by reference
/// directly rather than through a (read-only) SlotResolver.
void lowerAliasPassthrough(binder::BoundLogicalOperator& root, const PEPlans& plans, NameSlotMap& name_to_slot,
                           AliasSlotMap& alias_map, binder::SlotAllocator& alloc);

/// Walk a BoundExpression and rewrite BoundPropertyRef → BoundColumnRef
/// using the given PEPlans. Returns true if any rewrite occurred.
bool rewriteExpression(binder::BoundExpression& expr, const PEPlans& plans, const SlotResolver& resolver);

/// Walk a BoundLogicalOperator tree and lower all BoundExpressions to
/// slot-based references. Unlike the legacy column-index rewriter, this
/// pass requires no base_col / ProjectResetMap / LeftJoinColMap machinery
/// because slot_ids are stable across schema-changing operators.
void rewriteColumnIndices(binder::BoundLogicalOperator& op, const PEPlans& plans, const SlotResolver& resolver);

} // namespace optimizer
} // namespace eugraph
