#include "query/physical_plan/physical_planner.hpp"
#include "common/types/graph_types.hpp"
#include "query/optimizer/chosen_plan.hpp"
#include "query/optimizer/column_rewrite.hpp"
#include "query/optimizer/memo.hpp"
#include "query/physical_plan/operator/apply_physical_op.hpp"
#include "query/physical_plan/operator/call_physical_op.hpp"
#include "query/physical_plan/operator/correlated_source_physical_op.hpp"
#include "query/physical_plan/operator/cross_product_physical_op.hpp"
#include "query/physical_plan/operator/delete_physical_op.hpp"
#include "query/physical_plan/operator/distinct_physical_op.hpp"
#include "query/physical_plan/operator/hash_join_physical_op.hpp"
#include "query/physical_plan/operator/index_scan_values_physical_op.hpp"
#include "query/physical_plan/operator/left_join_physical_op.hpp"
#include "query/physical_plan/operator/list_index_join_physical_op.hpp"
#include "query/physical_plan/operator/merge_physical_op.hpp"
#include "query/physical_plan/operator/path_element_property_read_physical_op.hpp"
#include "query/physical_plan/operator/pattern_comprehension_apply_physical_op.hpp"
#include "query/physical_plan/operator/projection_extract_physical_op.hpp"
#include "query/physical_plan/operator/semi_join_physical_op.hpp"
#include "query/physical_plan/operator/singleton_physical_op.hpp"
#include "query/physical_plan/operator/union_physical_op.hpp"
#include "query/physical_plan/operator/unwind_physical_op.hpp"
#include "query/physical_plan/operator/varlen_expand_physical_op.hpp"
#include "query/planner/bound_logical_plan.hpp"

#include <functional>
#include <set>

#include <spdlog/spdlog.h>

namespace eugraph {
namespace compute {

// ==================== Helpers ====================

static binder::BoundType propertyTypeToBoundType(PropertyType pt) {
    switch (pt) {
    case PropertyType::BOOL:
        return binder::BoundType::Bool();
    case PropertyType::INT64:
        return binder::BoundType::Int64();
    case PropertyType::DOUBLE:
        return binder::BoundType::Double();
    case PropertyType::STRING:
        return binder::BoundType::String();
    case PropertyType::DATETIME:
        return binder::BoundType::DateTime();
    case PropertyType::TIME:
        return binder::BoundType::Time();
    case PropertyType::DURATION:
        return binder::BoundType::Duration();
    case PropertyType::INT64_ARRAY:
    case PropertyType::DOUBLE_ARRAY:
    case PropertyType::STRING_ARRAY:
        return binder::BoundType::List(binder::BoundType::Any());
    case PropertyType::DATETIME_ARRAY:
        return binder::BoundType::List(binder::BoundType::DateTime());
    case PropertyType::TIME_ARRAY:
        return binder::BoundType::List(binder::BoundType::Time());
    case PropertyType::DURATION_ARRAY:
        return binder::BoundType::List(binder::BoundType::Duration());
    default:
        return binder::BoundType::Any();
    }
}

/// Build a TupleSlotLayout from an output_schema + the var_slots map
/// populated from the bound plan.  Uses the actual globally-unique
/// slot_ids assigned by the Binder rather than sequential numbers.
/// For anonymous ProjectionExtract columns (a.name, etc.), allocates
/// fresh slot_ids from the planner's SlotAllocator and writes them back
/// to ctx.var_slots so subsequent operators see the same slot_id for
/// the same column name.
static TupleSlotLayout makeSlotLayout(const Schema& output_schema, PlanContext& ctx) {
    TupleSlotLayout layout;
    for (size_t i = 0; i < output_schema.size(); ++i) {
        auto it = ctx.var_slots.find(output_schema[i]);
        binder::SlotId sid;
        if (it != ctx.var_slots.end()) {
            sid = it->second;
        } else {
            sid = ctx.slot_allocator.next();
            ctx.var_slots[output_schema[i]] = sid;
        }
        layout.append(sid);
    }
    return layout;
}

/// Resolve the physical column that holds a mutation target's constructed
/// VertexValue / EdgeValue. Append-only ProjectionExtract keeps the source
/// VertexRef/EdgeKey in the variable's own column and appends the constructed
/// object as a separate column (object_slot_id in the PEPlan). SET / REMOVE /
/// DELETE need the object, so we resolve object_slot_id → column via the
/// child's slot layout. Falls back to the variable's own slot when no PEPlan
/// exists for the canonical slot (e.g. CREATE outputs a fully constructed
/// VERTEX/EDGE column under the variable's own slot, so no append was needed).
/// Returns -1 when the variable is unknown, letting the op fall back to
/// name-based lookup. Emits spdlog::warn on the two bug-signal paths
/// (PEPlan present but object_slot_id invalid; resolved slot missing from the
/// child layout) so silent regressions of the append-only / mutation
/// integration contract are observable.
static int resolveMutationColumn(PlanContext& ctx, const TupleSlotLayout& layout, const std::string& var) {
    auto resolver = ctx.resolver();
    binder::SlotId target_slot = resolver.slotForName(var);
    if (target_slot == binder::INVALID_SLOT_ID)
        return -1;
    binder::SlotId canon = resolver.canonicalOf(target_slot);
    auto pe = ctx.extraction_info.find(canon);
    if (pe != ctx.extraction_info.end()) {
        if (pe->second.object_slot_id == binder::INVALID_SLOT_ID) {
            spdlog::warn("[resolveMutationColumn] PEPlan for var '{}' (canonical slot {}) exists but "
                         "object_slot_id is INVALID — mutation will fall back to the variable's own slot "
                         "and likely read VertexRef instead of VertexValue",
                         var, canon);
        } else {
            target_slot = pe->second.object_slot_id;
        }
    }
    int col = layout.getColumnIndex(target_slot);
    if (col < 0) {
        spdlog::warn("[resolveMutationColumn] resolved slot {} for var '{}' is not present in the child "
                     "slot layout (size {}) — mutation will fall back to name-based lookup and may no-op",
                     target_slot, var, layout.size());
    }
    return col;
}

///   - need_whole_vertex  → Passthrough source + APPEND ConstructVertex column
///   - need_whole_edge    → Passthrough source + APPEND ConstructEdge column
///   - vertex_props(p)    → Passthrough source + APPEND LoadVertexProp column per p
///   - edge_props(p)      → Passthrough source + APPEND LoadEdgeProp column per p
///   - need_vertex_labels → Passthrough source + APPEND LoadVertexLabels column
///   - need_edge_type     → Passthrough source + APPEND LoadEdgeType column
///
/// Per the demand-pull-lowering design: PE NEVER overwrites an upstream
/// column. The source VertexRef / EdgeKey column is always preserved so
/// topology-reading operators (Expand, etc.) keep working unchanged.
///
/// Slot ids for appended columns come straight from the PEPlan (allocated
/// by buildExtractionInfo). Output column names are derived from the
/// canonical slot id — no string naming convention, slot ids are the
/// single source of truth.
///
/// Returns the input untouched when no PEPlan exists for any input column.
static PlanOperatorResult dispatchProjectionExtract(PlanOperatorResult&& child_result, IAsyncGraphDataStore& store,
                                                    PlanContext& ctx) {
    const auto& info = ctx.extraction_info;
    if (info.empty())
        return std::move(child_result);

    std::vector<ColumnSpec> specs;
    Schema output_schema;
    std::vector<binder::BoundType> output_types;
    const size_t input_cols = child_result.output_schema.size();

    std::unordered_map<LabelId, std::string> vertex_label_names;
    LabelId anon_label_id = INVALID_LABEL_ID;
    for (const auto& [lid, ldef] : ctx.label_defs) {
        if (ldef.name == kAnonLabelName) {
            anon_label_id = lid;
            continue;
        }
        vertex_label_names[lid] = ldef.name;
    }
    std::unordered_map<EdgeLabelId, std::string> edge_label_names;
    for (const auto& [elid, eldef] : ctx.edge_label_defs)
        edge_label_names[elid] = eldef.name;

    bool any_load = false;

    auto slotForName = [&ctx](const std::string& name) -> binder::SlotId { return ctx.resolver().slotForName(name); };

    /// Build a stable output name for an appended PE column. Slot id is
    /// unique, so the name is purely diagnostic.
    auto peName = [](binder::SlotId sid) { return std::string("__pe_") + std::to_string(sid); };

    auto emitSpec = [&](ColumnSpec s) {
        output_schema.push_back(s.output_name);
        output_types.push_back(s.output_type);
        specs.push_back(std::move(s));
    };

    // dedup across multiple PE calls in same pipeline — keyed by slot_id
    std::set<binder::SlotId> emitted_slots;
    for (const auto& s : child_result.output_schema) {
        binder::SlotId sid = slotForName(s);
        if (sid != binder::INVALID_SLOT_ID)
            emitted_slots.insert(sid);
    }

    for (size_t col = 0; col < input_cols; ++col) {
        const std::string& var = child_result.output_schema[col];

        // Always passthrough the source column. PE is append-only.
        ColumnSpec pasm;
        pasm.kind = ColumnSpec::Kind::Passthrough;
        pasm.output_name = var;
        pasm.output_type = child_result.output_types[col];
        pasm.source_col = col;
        binder::SlotId passthrough_slot = slotForName(var);
        pasm.slot_id = passthrough_slot;
        emitSpec(std::move(pasm));

        if (passthrough_slot == binder::INVALID_SLOT_ID)
            continue;
        binder::SlotId canon = optimizer::getCanonicalSlot(ctx.alias_map, passthrough_slot);
        auto it = info.find(canon);
        if (it == info.end())
            continue;
        const auto& pi = it->second;

        // Object column (Construct). Emitted iff Decide allocated a fresh
        // slot (object_slot_id != source_slot_id). When source is already
        // a constructed VERTEX/EDGE, Decide aliased object_slot_id to
        // source_slot_id and we skip — the source column itself satisfies
        // the whole-object demand.
        if (pi.object_slot_id != binder::INVALID_SLOT_ID && pi.object_slot_id != pi.source_slot_id) {
            if (!emitted_slots.count(pi.object_slot_id)) {
                bool col_is_edge = child_result.output_types[col].kind == binder::BoundTypeKind::EDGE ||
                                   child_result.output_types[col].kind == binder::BoundTypeKind::EDGE_KEY;
                ColumnSpec s;
                s.kind = col_is_edge ? ColumnSpec::Kind::ConstructEdge : ColumnSpec::Kind::ConstructVertex;
                s.output_name = peName(pi.object_slot_id);
                s.output_type = col_is_edge ? binder::BoundType::Edge() : binder::BoundType::Vertex();
                s.source_col = col;
                s.slot_id = pi.object_slot_id;
                ctx.var_slots[s.output_name] = pi.object_slot_id;
                emitSpec(std::move(s));
                emitted_slots.insert(pi.object_slot_id);
                any_load = true;
            }
        }

        // Vertex prop loads — slot_ids come straight from PEPlan.
        for (size_t i = 0; i < pi.prop_order.size(); ++i) {
            auto [lid, pid] = pi.prop_order[i];
            binder::SlotId pslot = pi.prop_slot_ids[i];
            if (emitted_slots.count(pslot))
                continue;
            ColumnSpec s;
            s.kind = ColumnSpec::Kind::LoadVertexProp;
            s.label_id = lid;
            s.prop_id = pid;
            s.source_col = col;
            s.output_name = peName(pslot);
            s.slot_id = pslot;
            binder::BoundType pt = binder::BoundType::Any();
            auto def_it = ctx.label_defs.find(lid);
            if (def_it != ctx.label_defs.end()) {
                for (const auto& pd : def_it->second.properties) {
                    if (pd.id == pid) {
                        pt = propertyTypeToBoundType(pd.type);
                        break;
                    }
                }
            }
            s.output_type = pt;
            ctx.var_slots[s.output_name] = pslot;
            emitSpec(std::move(s));
            emitted_slots.insert(pslot);
            any_load = true;
        }

        // Coalesced multi-candidate vertex property loads.
        for (const auto& [prop, co] : pi.coalesce_vertices) {
            if (emitted_slots.count(co.slot_id))
                continue;
            ColumnSpec s;
            s.kind = ColumnSpec::Kind::LoadVertexPropCoalesce;
            s.source_col = col;
            s.output_name = peName(co.slot_id);
            s.slot_id = co.slot_id;
            s.output_type = co.type;
            s.coalesce_candidates = co.candidates;
            ctx.var_slots[s.output_name] = co.slot_id;
            emitSpec(std::move(s));
            emitted_slots.insert(co.slot_id);
            any_load = true;
        }

        // Edge prop loads.
        for (size_t i = 0; i < pi.edge_prop_order.size(); ++i) {
            auto [elid, pid] = pi.edge_prop_order[i];
            binder::SlotId pslot = pi.edge_prop_slot_ids[i];
            if (emitted_slots.count(pslot))
                continue;
            ColumnSpec s;
            s.kind = ColumnSpec::Kind::LoadEdgeProp;
            s.edge_label_id = elid;
            s.prop_id = pid;
            s.source_col = col;
            s.output_name = peName(pslot);
            s.slot_id = pslot;
            binder::BoundType pt = binder::BoundType::Any();
            auto def_it = ctx.edge_label_defs.find(elid);
            if (def_it != ctx.edge_label_defs.end()) {
                for (const auto& pd : def_it->second.properties) {
                    if (pd.id == pid) {
                        pt = propertyTypeToBoundType(pd.type);
                        break;
                    }
                }
            }
            s.output_type = pt;
            ctx.var_slots[s.output_name] = pslot;
            emitSpec(std::move(s));
            emitted_slots.insert(pslot);
            any_load = true;
        }

        // Vertex labels.
        if (pi.labels_slot_id != binder::INVALID_SLOT_ID && !emitted_slots.count(pi.labels_slot_id)) {
            ColumnSpec s;
            s.kind = ColumnSpec::Kind::LoadVertexLabels;
            s.output_name = peName(pi.labels_slot_id);
            s.output_type = binder::BoundType::List(binder::BoundType::String());
            s.source_col = col;
            s.slot_id = pi.labels_slot_id;
            ctx.var_slots[s.output_name] = pi.labels_slot_id;
            emitSpec(std::move(s));
            emitted_slots.insert(pi.labels_slot_id);
            any_load = true;
        }

        // Edge type.
        if (pi.type_slot_id != binder::INVALID_SLOT_ID && !emitted_slots.count(pi.type_slot_id)) {
            ColumnSpec s;
            s.kind = ColumnSpec::Kind::LoadEdgeType;
            s.output_name = peName(pi.type_slot_id);
            s.output_type = binder::BoundType::String();
            s.source_col = col;
            s.slot_id = pi.type_slot_id;
            ctx.var_slots[s.output_name] = pi.type_slot_id;
            emitSpec(std::move(s));
            emitted_slots.insert(pi.type_slot_id);
            any_load = true;
        }

        // If a descendant Expand reintroduced `var` as a fresh binding,
        // the name-based slotForName above may have returned the alias-
        // chain slot. The fresh slot has its own PEPlan (from
        // collectPlanRequirements) — also emit a Construct column here
        // so RETURN-level consumers get a fully constructed object from
        // the correct source column (§6.2).
        auto fresh_it = ctx.fresh_expands.find(var);
        if (fresh_it != ctx.fresh_expands.end()) {
            binder::SlotId fresh_slot = fresh_it->second;
            binder::SlotId fresh_canon = optimizer::getCanonicalSlot(ctx.alias_map, fresh_slot);
            if (fresh_canon != passthrough_slot && fresh_canon != binder::INVALID_SLOT_ID) {
                auto fi = info.find(fresh_canon);
                if (fi != info.end() && fi->second.object_slot_id != binder::INVALID_SLOT_ID &&
                    fi->second.object_slot_id != fi->second.source_slot_id &&
                    !emitted_slots.count(fi->second.object_slot_id)) {
                    ColumnSpec s;
                    s.kind = ColumnSpec::Kind::ConstructVertex;
                    s.output_name = peName(fi->second.object_slot_id);
                    s.output_type = binder::BoundType::Vertex();
                    s.source_col = col;
                    s.slot_id = fi->second.object_slot_id;
                    ctx.var_slots[s.output_name] = fi->second.object_slot_id;
                    emitSpec(std::move(s));
                    emitted_slots.insert(fi->second.object_slot_id);
                    any_load = true;
                }
            }
        }
    }

    if (!any_load)
        return std::move(child_result);

    auto op = std::make_unique<ProjectionExtractPhysicalOp>(
        std::move(specs), store, std::move(vertex_label_names), std::move(edge_label_names),
        Schema(child_result.output_schema), std::vector<binder::BoundType>(child_result.output_types),
        std::move(child_result.op), anon_label_id);
    // NB: build the layout in a local BEFORE moving output_schema into the
    // result. A braced-init-list evaluates left-to-right, so passing
    // makeSlotLayout(output_schema) after std::move(output_schema) would read
    // a moved-from (empty) schema.
    TupleSlotLayout pe_layout = makeSlotLayout(output_schema, ctx);
    return PlanOperatorResult{std::move(op), std::move(output_schema), std::move(output_types), std::move(pe_layout)};
}

// ==================== Path Element Property Read ====================

static PlanOperatorResult wrapPathElementPropertyRead(PlanOperatorResult&& child_result,
                                                      const std::string& path_variable, IAsyncGraphDataStore& store) {
    if (path_variable.empty())
        return std::move(child_result);
    size_t col_idx = SIZE_MAX;
    for (size_t i = child_result.output_schema.size(); i-- > 0;) {
        if (child_result.output_schema[i] == path_variable) {
            col_idx = i;
            break;
        }
    }
    if (col_idx == SIZE_MAX)
        return std::move(child_result);
    // Phase D: upgrade output type from topology (PATH_TOPOLOGY) to semantic (PATH).
    auto ppr_output_types = std::vector<binder::BoundType>(child_result.output_types);
    ppr_output_types[col_idx] = binder::BoundType::Path();
    child_result.output_types[col_idx] = binder::BoundType::Path();
    auto read_op = std::make_unique<PathElementPropertyReadPhysicalOp>(
        path_variable, col_idx, store, Schema(child_result.output_schema), std::move(ppr_output_types),
        std::move(child_result.op));
    return PlanOperatorResult{std::move(read_op), std::move(child_result.output_schema),
                              std::move(child_result.output_types), std::move(child_result.slot_layout)};
}

// ==================== Column Index Remapping for CrossProduct ====================
// When a CrossProduct's right child uses global column indices (assigned by the
// binder starting from 0 for the left child), those indices need to be shifted
// so they're relative to the right child's own output (which starts at column 0).

static void remapExprColumnIndices(binder::BoundExpression& expr, uint32_t offset) {
    std::visit(
        [&offset](auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, binder::BoundColumnRef>) {
                if (val.column_index >= offset)
                    val.column_index -= offset;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryOp>>) {
                remapExprColumnIndices(val->left, offset);
                remapExprColumnIndices(val->right, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnaryOp>>) {
                remapExprColumnIndices(val->operand, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>>) {
                remapExprColumnIndices(val->object, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDynamicPropertyRef>>) {
                remapExprColumnIndices(val->object, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFunctionCall>>) {
                for (auto& arg : val->args)
                    remapExprColumnIndices(arg, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundList>>) {
                for (auto& elem : val->elements)
                    remapExprColumnIndices(elem, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLabelCast>>) {
                remapExprColumnIndices(val->object, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCase>>) {
                if (val->subject.has_value())
                    remapExprColumnIndices(*val->subject, offset);
                for (auto& [w, t] : val->when_thens) {
                    remapExprColumnIndices(w, offset);
                    remapExprColumnIndices(t, offset);
                }
                if (val->else_expr.has_value())
                    remapExprColumnIndices(*val->else_expr, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSubscript>>) {
                remapExprColumnIndices(val->list, offset);
                remapExprColumnIndices(val->index, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSlice>>) {
                remapExprColumnIndices(val->list, offset);
                if (val->from.has_value())
                    remapExprColumnIndices(*val->from, offset);
                if (val->to.has_value())
                    remapExprColumnIndices(*val->to, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMap>>) {
                for (auto& [k, v] : val->entries)
                    remapExprColumnIndices(v, offset);
            }
        },
        expr);
}

/// Add `offset` to every anonymous BoundColumnRef inside expr. Used for
/// cross-product equality predicates: their right refs are anonymous with
/// binder-local column indices, and the physical left child may contain extra
/// ProjectionExtract-appended columns the binder could not count.
static void offsetAnonymousColumnRefs(binder::BoundExpression& expr, uint32_t offset) {
    std::visit(
        [&offset](auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, binder::BoundColumnRef>) {
                if (val.name.empty())
                    val.column_index += offset;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryOp>>) {
                offsetAnonymousColumnRefs(val->left, offset);
                offsetAnonymousColumnRefs(val->right, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnaryOp>>) {
                offsetAnonymousColumnRefs(val->operand, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>>) {
                offsetAnonymousColumnRefs(val->object, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDynamicPropertyRef>>) {
                offsetAnonymousColumnRefs(val->object, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFunctionCall>>) {
                for (auto& arg : val->args)
                    offsetAnonymousColumnRefs(arg, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundList>>) {
                for (auto& elem : val->elements)
                    offsetAnonymousColumnRefs(elem, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLabelCast>>) {
                offsetAnonymousColumnRefs(val->object, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCase>>) {
                if (val->subject.has_value())
                    offsetAnonymousColumnRefs(*val->subject, offset);
                for (auto& [w, t] : val->when_thens) {
                    offsetAnonymousColumnRefs(w, offset);
                    offsetAnonymousColumnRefs(t, offset);
                }
                if (val->else_expr.has_value())
                    offsetAnonymousColumnRefs(*val->else_expr, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSubscript>>) {
                offsetAnonymousColumnRefs(val->list, offset);
                offsetAnonymousColumnRefs(val->index, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSlice>>) {
                offsetAnonymousColumnRefs(val->list, offset);
                if (val->from.has_value())
                    offsetAnonymousColumnRefs(*val->from, offset);
                if (val->to.has_value())
                    offsetAnonymousColumnRefs(*val->to, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMap>>) {
                for (auto& [k, v] : val->entries)
                    offsetAnonymousColumnRefs(v, offset);
            }
        },
        expr);
}

static void remapLogicalOpColumnIndices(binder::BoundLogicalOperator& op, uint32_t offset);

static void remapChildOps(binder::BoundLogicalOperator& op, uint32_t offset) {
    std::visit(
        [&offset](auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, binder::BoundScanOp>) {
                if (val.column_index >= offset)
                    val.column_index -= offset;
            } else if constexpr (std::is_same_v<T, binder::BoundLabelScanOp>) {
                if (val.column_index >= offset)
                    val.column_index -= offset;
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>>) {
                remapExprColumnIndices(val->predicate, offset);
                remapLogicalOpColumnIndices(val->child, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                for (auto& item : val->items)
                    remapExprColumnIndices(item.expr, offset);
                remapLogicalOpColumnIndices(val->child, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                if (val->src_column_index >= offset)
                    val->src_column_index -= offset;
                if (val->edge_column_index >= offset)
                    val->edge_column_index -= offset;
                if (val->dst_column_index >= offset)
                    val->dst_column_index -= offset;
                remapLogicalOpColumnIndices(val->child, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>>) {
                for (auto& item : val->items)
                    remapExprColumnIndices(item.expr, offset);
                remapLogicalOpColumnIndices(val->child, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAggregateOp>>) {
                for (auto& expr : val->group_keys)
                    remapExprColumnIndices(expr, offset);
                for (auto& item : val->aggregates)
                    for (auto& arg : item.arguments)
                        remapExprColumnIndices(arg, offset);
                remapLogicalOpColumnIndices(val->child, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>>) {
                remapLogicalOpColumnIndices(val->left, offset);
                remapLogicalOpColumnIndices(val->right, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLeftJoinOp>>) {
                remapLogicalOpColumnIndices(val->left, offset);
                remapLogicalOpColumnIndices(val->right, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>>) {
                remapLogicalOpColumnIndices(val->left, offset);
                remapLogicalOpColumnIndices(val->right, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPatternComprehensionApplyOp>>) {
                remapLogicalOpColumnIndices(val->left, offset);
                remapLogicalOpColumnIndices(val->right, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDistinctOp>>) {
                remapLogicalOpColumnIndices(val->child, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSkipOp>>) {
                remapLogicalOpColumnIndices(val->child, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLimitOp>>) {
                remapLogicalOpColumnIndices(val->child, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnwindOp>>) {
                remapExprColumnIndices(val->list_expr, offset);
                remapLogicalOpColumnIndices(val->child, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>>) {
                remapLogicalOpColumnIndices(val->child, offset);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                if (val->src_column_index >= offset)
                    val->src_column_index -= offset;
                if (val->edge_column_index >= offset)
                    val->edge_column_index -= offset;
                if (val->dst_column_index >= offset)
                    val->dst_column_index -= offset;
                if (val->path_column_index >= offset)
                    val->path_column_index -= offset;
                remapLogicalOpColumnIndices(val->child, offset);
            }
        },
        op);
}

static void remapLogicalOpColumnIndices(binder::BoundLogicalOperator& op, uint32_t offset) {
    remapChildOps(op, offset);
}

// Convert a Value (runtime literal) to PropertyValue for storage.
static PropertyValue valueToPropertyValue(const Value& v) {
    if (std::holds_alternative<bool>(v))
        return std::get<bool>(v);
    if (std::holds_alternative<int64_t>(v))
        return std::get<int64_t>(v);
    if (std::holds_alternative<double>(v))
        return std::get<double>(v);
    if (std::holds_alternative<std::string>(v))
        return std::get<std::string>(v);
    if (std::holds_alternative<DateTimeValue>(v))
        return std::get<DateTimeValue>(v);
    if (std::holds_alternative<TimeValue>(v))
        return std::get<TimeValue>(v);
    if (std::holds_alternative<DurationValue>(v))
        return std::get<DurationValue>(v);
    return PropertyValue{};
}

/// Infer PropertyType from a BoundExpression's result type.
static PropertyType boundExprToPropertyType(const binder::BoundExpression& expr) {
    const auto& bt = binder::getBoundExprType(expr);
    switch (bt.kind) {
    case binder::BoundTypeKind::BOOL:
        return PropertyType::BOOL;
    case binder::BoundTypeKind::INT64:
        return PropertyType::INT64;
    case binder::BoundTypeKind::DOUBLE:
        return PropertyType::DOUBLE;
    case binder::BoundTypeKind::STRING:
        return PropertyType::STRING;
    case binder::BoundTypeKind::DATETIME:
        return PropertyType::DATETIME;
    case binder::BoundTypeKind::TIME:
        return PropertyType::TIME;
    case binder::BoundTypeKind::DURATION:
        return PropertyType::DURATION;
    default:
        return PropertyType::ANY;
    }
}

// ==================== Read-Only Property Label Pruning ====================

static void addPruneHint(std::unordered_map<std::string, PlanContext::StaticPruneHint>& hints, const std::string& var,
                         std::vector<LabelId> vertex_labels, std::vector<EdgeLabelId> edge_labels) {
    auto& hint = hints[var];
    for (LabelId lid : vertex_labels) {
        if (std::find(hint.vertex_labels.begin(), hint.vertex_labels.end(), lid) == hint.vertex_labels.end())
            hint.vertex_labels.push_back(lid);
    }
    for (EdgeLabelId elid : edge_labels) {
        if (std::find(hint.edge_labels.begin(), hint.edge_labels.end(), elid) == hint.edge_labels.end())
            hint.edge_labels.push_back(elid);
    }
}

static std::vector<LabelId> collectStaticVertexPruneLabels(PlanContext& ctx, const binder::BoundExpression& expr) {
    std::vector<LabelId> labels;
    if (auto* un = std::get_if<std::unique_ptr<binder::BoundUnaryOp>>(&expr)) {
        if (!*un || (*un)->op != cypher::UnaryOperator::IS_NOT_NULL)
            return labels;
        auto* cref = std::get_if<binder::BoundColumnRef>(&(*un)->operand);
        if (!cref)
            return labels;
        for (const auto& [source_slot, plan] : ctx.extraction_info) {
            for (size_t i = 0; i < plan.prop_slot_ids.size() && i < plan.prop_order.size(); ++i) {
                if (plan.prop_slot_ids[i] == cref->slot_id) {
                    LabelId lid = plan.prop_order[i].first;
                    if (std::find(labels.begin(), labels.end(), lid) == labels.end())
                        labels.push_back(lid);
                }
            }
            for (const auto& [prop, co] : plan.coalesce_vertices) {
                if (co.slot_id == cref->slot_id) {
                    for (const auto& [lid, pid] : co.candidates) {
                        if (std::find(labels.begin(), labels.end(), lid) == labels.end())
                            labels.push_back(lid);
                    }
                }
            }
        }
    }
    return labels;
}

static std::vector<EdgeLabelId> collectStaticEdgePruneLabels(PlanContext& ctx, const binder::BoundExpression& expr) {
    std::vector<EdgeLabelId> labels;
    if (auto* un = std::get_if<std::unique_ptr<binder::BoundUnaryOp>>(&expr)) {
        if (!*un || (*un)->op != cypher::UnaryOperator::IS_NOT_NULL)
            return labels;
        auto* cref = std::get_if<binder::BoundColumnRef>(&(*un)->operand);
        if (!cref)
            return labels;
        for (const auto& [source_slot, plan] : ctx.extraction_info) {
            for (size_t i = 0; i < plan.edge_prop_slot_ids.size() && i < plan.edge_prop_order.size(); ++i) {
                if (plan.edge_prop_slot_ids[i] == cref->slot_id) {
                    EdgeLabelId elid = plan.edge_prop_order[i].first;
                    if (std::find(labels.begin(), labels.end(), elid) == labels.end())
                        labels.push_back(elid);
                }
            }
        }
    }
    return labels;
}

static void collectStaticPruneHints(const binder::BoundExpression& expr, PlanContext& ctx,
                                    std::unordered_map<std::string, PlanContext::StaticPruneHint>& hints) {
    if (auto* un = std::get_if<std::unique_ptr<binder::BoundUnaryOp>>(&expr)) {
        if (!*un || (*un)->op != cypher::UnaryOperator::IS_NOT_NULL)
            return;
        if (auto* dr = std::get_if<std::unique_ptr<binder::BoundDynamicPropertyRef>>(&(*un)->operand)) {
            if (!*dr)
                return;
            std::string var;
            const binder::BoundType* type = nullptr;
            if (auto* cref = std::get_if<binder::BoundColumnRef>(&(*dr)->object)) {
                var = cref->name;
                type = &cref->type;
            } else if (auto* vref = std::get_if<binder::BoundVariableRef>(&(*dr)->object)) {
                var = vref->name;
                type = &vref->type;
            }
            if (var.empty() || !type)
                return;
            if (type->kind == binder::BoundTypeKind::VERTEX || type->kind == binder::BoundTypeKind::VERTEX_REF) {
                std::vector<LabelId> labels;
                for (const auto& [lid, ldef] : ctx.label_defs) {
                    for (const auto& pd : ldef.properties) {
                        if (pd.name == (*dr)->property) {
                            labels.push_back(lid);
                            break;
                        }
                    }
                }
                addPruneHint(hints, var, std::move(labels), {});
            } else if (type->kind == binder::BoundTypeKind::EDGE || type->kind == binder::BoundTypeKind::EDGE_KEY) {
                std::vector<EdgeLabelId> labels;
                for (const auto& [elid, eldef] : ctx.edge_label_defs) {
                    for (const auto& pd : eldef.properties) {
                        if (pd.name == (*dr)->property) {
                            labels.push_back(elid);
                            break;
                        }
                    }
                }
                addPruneHint(hints, var, {}, std::move(labels));
            }
            return;
        }
        auto* pr = std::get_if<std::unique_ptr<binder::BoundPropertyRef>>(&(*un)->operand);
        if (!pr || !*pr || (*pr)->candidates.empty())
            return;
        std::string var;
        const binder::BoundType* type = nullptr;
        if (auto* cref = std::get_if<binder::BoundColumnRef>(&(*pr)->object)) {
            var = cref->name;
            type = &cref->type;
        } else if (auto* vref = std::get_if<binder::BoundVariableRef>(&(*pr)->object)) {
            var = vref->name;
            type = &vref->type;
        }
        if (var.empty() || !type)
            return;
        if (type->kind == binder::BoundTypeKind::VERTEX || type->kind == binder::BoundTypeKind::VERTEX_REF) {
            std::vector<LabelId> labels;
            for (const auto& cand : (*pr)->candidates)
                labels.push_back(cand.label_id);
            addPruneHint(hints, var, std::move(labels), {});
        } else if (type->kind == binder::BoundTypeKind::EDGE || type->kind == binder::BoundTypeKind::EDGE_KEY) {
            std::vector<EdgeLabelId> labels;
            for (const auto& cand : (*pr)->candidates)
                labels.push_back(EdgeLabelId{cand.label_id});
            addPruneHint(hints, var, {}, std::move(labels));
        }
        return;
    }
    if (auto* bin = std::get_if<std::unique_ptr<binder::BoundBinaryOp>>(&expr)) {
        if (!*bin || (*bin)->op != cypher::BinaryOperator::AND)
            return;
        collectStaticPruneHints((*bin)->left, ctx, hints);
        collectStaticPruneHints((*bin)->right, ctx, hints);
    }
}

// ==================== Bound Plan Index Scan Optimization ====================

/// Extract AND-chain of BoundBinaryOp conditions from a BoundExpression.
static void collectBoundConditions(const binder::BoundExpression& pred,
                                   std::vector<const binder::BoundBinaryOp*>& conditions) {
    if (auto* bp = std::get_if<std::unique_ptr<binder::BoundBinaryOp>>(&pred)) {
        auto& binop = *bp;
        if (binop->op == cypher::BinaryOperator::AND) {
            collectBoundConditions(binop->left, conditions);
            collectBoundConditions(binop->right, conditions);
        } else {
            conditions.push_back(binop.get());
        }
    }
}

struct BoundIndexableCondition {
    LabelId label_id = INVALID_LABEL_ID;
    uint16_t prop_id = 0;
    std::string property_name;
    cypher::BinaryOperator op;
    PropertyValue value;
};

/// Resolve a BoundColumnRef produced by ProjectionExtract lowering back to
/// the property name / (label_id, prop_id) it was derived from.
static std::optional<std::pair<std::string, std::pair<LabelId, uint16_t>>>
resolveColumnProperty(const PlanContext& ctx, const binder::BoundColumnRef& ref) {
    for (const auto& [canonical_slot, pe] : ctx.extraction_info) {
        (void)canonical_slot;
        for (size_t i = 0; i < pe.prop_slot_ids.size(); ++i) {
            if (pe.prop_slot_ids[i] == ref.slot_id) {
                const auto& [lid, pid] = pe.prop_order[i];
                auto lit = ctx.label_defs.find(lid);
                if (lit == ctx.label_defs.end())
                    return std::nullopt;
                for (const auto& pd : lit->second.properties) {
                    if (pd.id == pid)
                        return std::make_pair(pd.name, std::make_pair(lid, pid));
                }
                return std::nullopt;
            }
        }
        for (const auto& [prop_name, co] : pe.coalesce_vertices) {
            if (co.slot_id == ref.slot_id) {
                LabelId lid = co.candidates.empty() ? INVALID_LABEL_ID : co.candidates[0].first;
                uint16_t pid = co.candidates.empty() ? 0 : co.candidates[0].second;
                return std::make_pair(prop_name, std::make_pair(lid, pid));
            }
        }
    }
    return std::nullopt;
}

/// Try to extract a property=value condition from a BoundBinaryOp.
/// Accepts both unlowered BoundPropertyRef and the BoundColumnRef form that
/// column-rewrite produces for flat property columns.
static std::optional<BoundIndexableCondition> tryExtractBoundCondition(const binder::BoundBinaryOp& binop,
                                                                       const PlanContext& ctx) {
    if (!std::holds_alternative<binder::BoundLiteral>(binop.right))
        return std::nullopt;
    auto& lit = std::get<binder::BoundLiteral>(binop.right);

    auto pv = valueToPropertyValue(lit.value);
    if (std::holds_alternative<std::monostate>(pv))
        return std::nullopt;

    BoundIndexableCondition cond;
    cond.op = binop.op;
    cond.value = std::move(pv);

    if (std::holds_alternative<std::unique_ptr<binder::BoundPropertyRef>>(binop.left)) {
        auto& prop_ref = std::get<std::unique_ptr<binder::BoundPropertyRef>>(binop.left);
        if (prop_ref->candidates.empty())
            return std::nullopt;
        cond.label_id = prop_ref->candidates[0].label_id;
        cond.prop_id = prop_ref->candidates[0].prop_id;
        cond.property_name = prop_ref->property_name;
        return cond;
    }

    if (std::holds_alternative<binder::BoundColumnRef>(binop.left)) {
        auto resolved = resolveColumnProperty(ctx, std::get<binder::BoundColumnRef>(binop.left));
        if (!resolved)
            return std::nullopt;
        cond.property_name = resolved->first;
        cond.label_id = resolved->second.first;
        cond.prop_id = resolved->second.second;
        return cond;
    }

    return std::nullopt;
}

// Detect `Filter(src.prop = v OR dst.prop = v)` above a VarLenExpand and
// attach index-value pruning hints. The VLE then implements the OR by
// checking source/destination membership without a UNION rewrite.
static bool trySetVarlenOrFilters(const binder::BoundFilterOp& filter, binder::BoundVarLenExpandOp& vle,
                                  const PlanContext& ctx) {
    const auto* or_op_ptr = std::get_if<std::unique_ptr<binder::BoundBinaryOp>>(&filter.predicate);
    if (!or_op_ptr || !*or_op_ptr || (*or_op_ptr)->op != cypher::BinaryOperator::OR)
        return false;
    auto& or_op = **or_op_ptr;

    auto extract_side = [&](const binder::BoundExpression& expr, std::string& var,
                            std::optional<BoundIndexableCondition>& cond) {
        const auto* bp = std::get_if<std::unique_ptr<binder::BoundBinaryOp>>(&expr);
        if (!bp || !*bp || (*bp)->op != cypher::BinaryOperator::EQ)
            return false;
        auto extracted = tryExtractBoundCondition(**bp, ctx);
        if (!extracted)
            return false;
        if (auto* cref = std::get_if<binder::BoundColumnRef>(&(*bp)->left)) {
            var = cref->name;
        } else if (const auto* prop = std::get_if<std::unique_ptr<binder::BoundPropertyRef>>(&(*bp)->left)) {
            if (!prop || !*prop)
                return false;
            if (auto* obj = std::get_if<binder::BoundColumnRef>(&(*prop)->object))
                var = obj->name;
        }
        if (var.empty())
            return false;
        cond = std::move(extracted);
        return true;
    };

    std::string side_a_var, side_b_var;
    std::optional<BoundIndexableCondition> cond_a, cond_b;
    if (!extract_side(or_op.left, side_a_var, cond_a) || !extract_side(or_op.right, side_b_var, cond_b))
        return false;
    if (side_a_var == vle.dst_variable && side_b_var == vle.src_variable) {
        std::swap(side_a_var, side_b_var);
        std::swap(cond_a, cond_b);
    }
    if (side_a_var != vle.src_variable || side_b_var != vle.dst_variable)
        return false;

    auto find_index = [&](LabelId label, const std::string& prop_name) -> uint32_t {
        auto it = ctx.label_defs.find(label);
        if (it == ctx.label_defs.end())
            return 0;
        for (const auto& idx : it->second.indexes) {
            if (idx.state != IndexState::PUBLIC || idx.index_id == 0)
                continue;
            for (const auto& acc : idx.accessors)
                if (acc.property_name == prop_name)
                    return idx.index_id;
        }
        return 0;
    };

    LabelId src_label = INVALID_LABEL_ID;
    LabelId dst_label = INVALID_LABEL_ID;
    if (const auto* child_scan = std::get_if<binder::BoundLabelScanOp>(&vle.child)) {
        if (!child_scan->label_ids.empty())
            src_label = child_scan->label_ids[0];
    }
    if (!vle.dst_label_ids.empty())
        dst_label = vle.dst_label_ids[0];
    if (src_label == INVALID_LABEL_ID || dst_label == INVALID_LABEL_ID)
        return false;

    uint32_t src_idx = find_index(src_label, cond_a->property_name);
    uint32_t dst_idx = find_index(dst_label, cond_b->property_name);
    if (src_idx == 0 && dst_idx == 0)
        return false;

    if (src_idx != 0) {
        vle.src_filter_index_id = src_idx;
        vle.src_filter_value = cond_a->value;
    }
    if (dst_idx != 0) {
        vle.dst_filter_index_id = dst_idx;
        vle.dst_filter_value = cond_b->value;
    }
    return true;
}

std::optional<PlanOperatorResult>
PhysicalPlanner::tryBoundIndexScan(const binder::BoundLabelScanOp& scan_op,
                                   const std::vector<const binder::BoundBinaryOp*>& conditions, LabelId label_id,
                                   const LabelDef& label_def, IAsyncGraphDataStore& store, PlanContext& ctx) {
    std::unordered_map<std::string, BoundIndexableCondition> prop_conditions;
    for (auto* cond : conditions) {
        auto extracted = tryExtractBoundCondition(*cond, ctx);
        if (extracted.has_value())
            prop_conditions[extracted->property_name] = std::move(*extracted);
    }

    if (prop_conditions.empty())
        return std::nullopt;

    using ScanMode = IndexScanPhysicalOp::ScanMode;
    auto make_index_scan = [&](const LabelDef::IndexDef& idx, ScanMode mode, std::vector<PropertyValue> eq_values,
                               std::optional<std::vector<PropertyValue>> range_start,
                               std::optional<std::vector<PropertyValue>> range_end,
                               std::vector<binder::BoundType> output_types) -> std::unique_ptr<IndexScanPhysicalOp> {
        if (idx.index_id != 0) {
            return std::make_unique<IndexScanPhysicalOp>(
                scan_op.variable, idx.index_id, idx.prop_ids, mode, std::move(eq_values), std::move(range_start),
                std::move(range_end), std::move(output_types), store, ctx.label_defs);
        }
        return std::make_unique<IndexScanPhysicalOp>(scan_op.variable, label_id, idx.prop_ids, mode,
                                                     std::move(eq_values), std::move(range_start), std::move(range_end),
                                                     std::move(output_types), store, ctx.label_defs);
    };

    for (const auto& idx : label_def.indexes) {
        if (idx.state != IndexState::PUBLIC)
            continue;

        size_t match_count = 0;
        bool last_is_range = false;
        std::vector<PropertyValue> eq_values;
        std::optional<PropertyValue> range_start;
        std::optional<PropertyValue> range_end;

        // Accessor path. Conditions are matched by property name; strong
        // accessors additionally require the source label match.
        for (size_t i = 0; i < idx.accessors.size(); ++i) {
            const auto& acc = idx.accessors[i];
            auto it = prop_conditions.find(acc.property_name);
            if (it == prop_conditions.end())
                break;
            auto& cond = it->second;
            if (acc.is_strong && cond.label_id != acc.source_label_id)
                break;

            if (i < idx.accessors.size() - 1) {
                if (cond.op != cypher::BinaryOperator::EQ)
                    break;
                eq_values.push_back(std::move(cond.value));
                match_count++;
            } else {
                if (cond.op == cypher::BinaryOperator::EQ) {
                    eq_values.push_back(std::move(cond.value));
                    match_count++;
                } else if (cond.op == cypher::BinaryOperator::GT || cond.op == cypher::BinaryOperator::GTE) {
                    range_start = std::move(cond.value);
                    last_is_range = true;
                    match_count++;
                } else if (cond.op == cypher::BinaryOperator::LT || cond.op == cypher::BinaryOperator::LTE) {
                    range_end = std::move(cond.value);
                    last_is_range = true;
                    match_count++;
                } else {
                    break;
                }
            }
        }

        if (match_count == 0 || match_count != idx.accessors.size())
            continue;

        Schema output_schema;
        std::vector<binder::BoundType> output_types;
        if (!scan_op.variable.empty()) {
            output_schema.push_back(scan_op.variable);
            output_types.push_back(binder::BoundType::VertexRef());
        }
        auto result_output_types = output_types;

        std::unique_ptr<IndexScanPhysicalOp> result;
        if (!last_is_range) {
            result = make_index_scan(idx, IndexScanPhysicalOp::ScanMode::EQUALITY, std::move(eq_values), std::nullopt,
                                     std::nullopt, std::move(output_types));
        } else {
            std::optional<std::vector<PropertyValue>> composite_start;
            std::optional<std::vector<PropertyValue>> composite_end;
            if (range_start.has_value()) {
                auto start_vec = eq_values;
                start_vec.push_back(std::move(*range_start));
                composite_start = std::move(start_vec);
            } else if (!eq_values.empty()) {
                composite_start = eq_values;
            }
            if (range_end.has_value()) {
                auto end_vec = eq_values;
                end_vec.push_back(std::move(*range_end));
                composite_end = std::move(end_vec);
            }
            result = make_index_scan(idx, IndexScanPhysicalOp::ScanMode::RANGE, std::vector<PropertyValue>{},
                                     std::move(composite_start), std::move(composite_end), std::move(output_types));
        }

        auto plan_result = PlanOperatorResult{std::move(result), std::move(output_schema),
                                              std::move(result_output_types), TupleSlotLayout{}};
        plan_result = dispatchProjectionExtract(std::move(plan_result), store, ctx);
        return plan_result;
    }
    return std::nullopt;
}

std::optional<PlanOperatorResult> PhysicalPlanner::tryBoundEdgeIndexScan(
    const binder::BoundExpandOp& expand_op, const std::vector<const binder::BoundBinaryOp*>& conditions,
    EdgeLabelId label_id, const EdgeLabelDef& edge_label_def, IAsyncGraphDataStore& store, PlanContext& ctx) {
    std::unordered_map<uint16_t, BoundIndexableCondition> prop_conditions;
    for (auto* cond : conditions) {
        auto extracted = tryExtractBoundCondition(*cond, ctx);
        if (extracted.has_value())
            prop_conditions[extracted->prop_id] = std::move(*extracted);
    }

    if (prop_conditions.empty())
        return std::nullopt;

    for (const auto& idx : edge_label_def.indexes) {
        if (idx.state != IndexState::PUBLIC)
            continue;

        size_t match_count = 0;
        bool last_is_range = false;
        std::vector<PropertyValue> eq_values;
        std::optional<PropertyValue> range_start;
        std::optional<PropertyValue> range_end;

        for (size_t i = 0; i < idx.prop_ids.size(); ++i) {
            uint16_t pid = idx.prop_ids[i];
            auto it = prop_conditions.find(pid);
            if (it == prop_conditions.end())
                break;

            auto& cond = it->second;
            if (i < idx.prop_ids.size() - 1) {
                if (cond.op != cypher::BinaryOperator::EQ)
                    break;
                eq_values.push_back(std::move(cond.value));
                match_count++;
            } else {
                if (cond.op == cypher::BinaryOperator::EQ) {
                    eq_values.push_back(std::move(cond.value));
                    match_count++;
                } else if (cond.op == cypher::BinaryOperator::GT || cond.op == cypher::BinaryOperator::GTE) {
                    range_start = std::move(cond.value);
                    last_is_range = true;
                    match_count++;
                } else if (cond.op == cypher::BinaryOperator::LT || cond.op == cypher::BinaryOperator::LTE) {
                    range_end = std::move(cond.value);
                    last_is_range = true;
                    match_count++;
                } else {
                    break;
                }
            }
        }

        if (match_count == 0)
            continue;

        using ScanMode = EdgeIndexScanPhysicalOp::ScanMode;
        std::unique_ptr<EdgeIndexScanPhysicalOp> result;

        Schema output_schema;
        std::vector<binder::BoundType> output_types;
        if (!expand_op.src_variable.empty()) {
            output_schema.push_back(expand_op.src_variable);
            output_types.push_back(binder::BoundType::VertexRef());
        }
        if (!expand_op.dst_variable.empty()) {
            output_schema.push_back(expand_op.dst_variable);
            output_types.push_back(binder::BoundType::VertexRef());
        }
        if (!expand_op.edge_variable.empty()) {
            output_schema.push_back(expand_op.edge_variable);
            output_types.push_back(binder::BoundType::EdgeKey());
        }

        auto result_output_types = output_types;

        if (!last_is_range) {
            result = std::make_unique<EdgeIndexScanPhysicalOp>(
                expand_op.src_variable, expand_op.dst_variable, expand_op.edge_variable, label_id, idx.prop_ids,
                ScanMode::EQUALITY, std::move(eq_values), std::nullopt, std::nullopt, std::move(output_types), store,
                ctx.edge_label_defs);
        } else {
            std::optional<std::vector<PropertyValue>> composite_start;
            std::optional<std::vector<PropertyValue>> composite_end;
            if (range_start.has_value()) {
                auto start_vec = eq_values;
                start_vec.push_back(std::move(*range_start));
                composite_start = std::move(start_vec);
            } else if (!eq_values.empty()) {
                composite_start = eq_values;
            }
            if (range_end.has_value()) {
                auto end_vec = eq_values;
                end_vec.push_back(std::move(*range_end));
                composite_end = std::move(end_vec);
            }
            result = std::make_unique<EdgeIndexScanPhysicalOp>(
                expand_op.src_variable, expand_op.dst_variable, expand_op.edge_variable, label_id, idx.prop_ids,
                ScanMode::RANGE, std::vector<PropertyValue>{}, std::move(composite_start), std::move(composite_end),
                std::move(output_types), store, ctx.edge_label_defs);
        }

        return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(result_output_types),
                                  TupleSlotLayout{}};
    }
    return std::nullopt;
}

// ==================== Bound Plan Pipeline ====================

namespace {
int findColumn(const Schema& schema, const std::string& name) {
    for (size_t i = 0; i < schema.size(); ++i) {
        if (schema[i] == name)
            return static_cast<int>(i);
    }
    return -1;
}

PlanOperatorResult extractChildResult(std::variant<PlanOperatorResult, std::string>&& child_result) {
    auto cr = std::move(std::get<PlanOperatorResult>(child_result));
    if (cr.op) {
        cr.op->setOutputSchema(Schema(cr.output_schema), std::vector<binder::BoundType>(cr.output_types));
        cr.op->setSlotLayout(TupleSlotLayout(cr.slot_layout));
    }
    return cr;
}

/// Also propagate the slot layout from the result to the physical operator.
static std::unique_ptr<PhysicalOperator> finalizePlanResult(PlanOperatorResult&& result) {
    if (!result.op)
        return nullptr;
    result.op->setOutputSchema(std::move(result.output_schema), std::move(result.output_types));
    result.op->setSlotLayout(std::move(result.slot_layout));
    return std::move(result.op);
}

} // namespace

std::optional<PlanOperatorResult>
PhysicalPlanner::tryPlanListIndexJoin(const binder::BoundFilterOp& filter, binder::BoundBinaryJoinOp& join,
                                      IAsyncGraphDataStore& store, IAsyncGraphMetaStore& meta, PlanContext& ctx,
                                      Schema input_schema, const std::vector<binder::BoundType>& input_types) {
    // Recognize `right_var.prop IN left_list_column`.
    const binder::BoundBinaryOp* in_op = nullptr;
    if (auto* bp = std::get_if<std::unique_ptr<binder::BoundBinaryOp>>(&filter.predicate)) {
        if (*bp && (*bp)->op == cypher::BinaryOperator::IN)
            in_op = bp->get();
    }
    if (!in_op) {
        return std::nullopt;
    }
    if (!std::holds_alternative<binder::BoundColumnRef>(in_op->right)) {
        return std::nullopt;
    }
    const auto& list_ref = std::get<binder::BoundColumnRef>(in_op->right);

    std::string prop_name;
    LabelId prop_label = INVALID_LABEL_ID;
    std::string dst_var;
    if (std::holds_alternative<std::unique_ptr<binder::BoundPropertyRef>>(in_op->left)) {
        auto& prop_ref = std::get<std::unique_ptr<binder::BoundPropertyRef>>(in_op->left);
        if (!prop_ref || prop_ref->candidates.empty() || prop_ref->property_name.empty())
            return std::nullopt;
        prop_name = prop_ref->property_name;
        prop_label = prop_ref->candidates[0].label_id;
        if (auto* obj = std::get_if<binder::BoundColumnRef>(&prop_ref->object))
            dst_var = obj->name;
    } else if (std::holds_alternative<binder::BoundColumnRef>(in_op->left)) {
        auto resolved = resolveColumnProperty(ctx, std::get<binder::BoundColumnRef>(in_op->left));
        if (!resolved)
            return std::nullopt;
        prop_name = resolved->first;
        prop_label = resolved->second.first;
        dst_var = std::get<binder::BoundColumnRef>(in_op->left).name;
    } else {
        return std::nullopt;
    }
    if (prop_name.empty() || prop_label == INVALID_LABEL_ID || dst_var.empty()) {
        return std::nullopt;
    }
    auto label_it = ctx.label_defs.find(prop_label);
    if (label_it == ctx.label_defs.end())
        return std::nullopt;

    uint32_t index_id = 0;
    for (const auto& idx : label_it->second.indexes) {
        if (idx.state != IndexState::PUBLIC || idx.index_id == 0)
            continue;
        for (const auto& acc : idx.accessors) {
            if (acc.property_name == prop_name) {
                index_id = idx.index_id;
                break;
            }
        }
        if (index_id != 0)
            break;
    }
    if (index_id == 0) {
        return std::nullopt;
    }

    // Find the final Expand in the right subtree that produces prop_ref's
    // variable and capture its single edge label.
    EdgeLabelId edge_label = INVALID_EDGE_LABEL_ID;
    LabelId expand_dst_label = INVALID_LABEL_ID;
    bool found_expand = false;
    std::function<void(const binder::BoundLogicalOperator&)> visit = [&](const binder::BoundLogicalOperator& op) {
        std::visit(
            [&](const auto& val) {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                    if (val) {
                        if (val->dst_variable == dst_var &&
                            val->direction == cypher::RelationshipDirection::LEFT_TO_RIGHT &&
                            val->edge_label_ids.size() == 1) {
                            edge_label = val->edge_label_ids[0];
                            if (!val->dst_label_ids.empty())
                                expand_dst_label = val->dst_label_ids[0];
                            found_expand = true;
                        }
                        visit(val->child);
                    }
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>>) {
                    if (val)
                        visit(val->child);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                    if (val)
                        visit(val->child);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAggregateOp>>) {
                    if (val)
                        visit(val->child);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>>) {
                    if (val)
                        visit(val->child);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLimitOp>>) {
                    if (val)
                        visit(val->child);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSkipOp>>) {
                    if (val)
                        visit(val->child);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDistinctOp>>) {
                    if (val)
                        visit(val->child);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>>) {
                    if (val)
                        visit(val->child);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                    if (val)
                        visit(val->child);
                }
            },
            op);
    };
    visit(join.right);
    if (!found_expand || edge_label == INVALID_EDGE_LABEL_ID) {
        return std::nullopt;
    }
    if (expand_dst_label != INVALID_LABEL_ID && ctx.label_defs.count(expand_dst_label)) {
        prop_label = expand_dst_label;
        index_id = 0;
        auto expand_label_it = ctx.label_defs.find(prop_label);
        for (const auto& idx : expand_label_it->second.indexes) {
            if (idx.state != IndexState::PUBLIC || idx.index_id == 0)
                continue;
            for (const auto& acc : idx.accessors) {
                if (acc.property_name == prop_name) {
                    index_id = idx.index_id;
                    break;
                }
            }
            if (index_id != 0)
                break;
        }
        if (index_id == 0)
            return std::nullopt;
    }

    auto left_result = planBoundOperator(join.left, store, meta, ctx, input_schema, input_types);
    if (std::holds_alternative<std::string>(left_result))
        return std::nullopt;
    auto lr = extractChildResult(std::move(left_result));

    // Prefer the name carried by the bound column; fall back to slot layout.
    int list_col = -1;
    for (size_t i = 0; i < lr.output_schema.size(); ++i) {
        if (lr.output_schema[i] == list_ref.name) {
            list_col = static_cast<int>(i);
            break;
        }
    }
    if (list_col < 0)
        list_col = lr.slot_layout.getColumnIndex(list_ref.slot_id);
    if (list_col < 0) {
        return std::nullopt;
    }

    // Logical reorder into a HashJoin(friend), mirroring Neo4j's Q12 plan:
    //
    //   Apply(left: collect(tag.id) AS tags,
    //        HashJoin(left: IndexScanValues(Tag) -> ... -> friend,
    //                 right: Person(id) -> KNOWS -> friend))
    {
        std::vector<const binder::BoundExpandOp*> chain;
        binder::BoundFilterOp* start_filter_op = nullptr;
        LabelId leaf_label = INVALID_LABEL_ID;
        std::optional<binder::BoundLabelScanOp> leaf_scan;
        std::function<void(binder::BoundLogicalOperator&)> collect = [&](binder::BoundLogicalOperator& op) {
            std::visit(
                [&](auto& val) {
                    using T = std::decay_t<decltype(val)>;
                    if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                        if (val) {
                            chain.push_back(val.get());
                            collect(val->child);
                        }
                    } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>>) {
                        if (val) {
                            start_filter_op = val.get();
                            collect(val->child);
                        }
                    } else if constexpr (std::is_same_v<T, binder::BoundLabelScanOp>) {
                        if (val.label_ids.size() == 1)
                            leaf_label = val.label_ids[0];
                        leaf_scan = val;
                    } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                        if (val)
                            collect(val->child);
                    } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>>) {
                        if (val)
                            collect(val->child);
                    } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLimitOp>>) {
                        if (val)
                            collect(val->child);
                    } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSkipOp>>) {
                        if (val)
                            collect(val->child);
                    } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDistinctOp>>) {
                        if (val)
                            collect(val->child);
                    }
                },
                op);
        };
        collect(join.right);

        bool chain_ok = chain.size() >= 2 && start_filter_op && leaf_scan.has_value();
        for (const auto* e : chain)
            if (e->edge_label_ids.empty())
                chain_ok = false;
        if (chain_ok && chain.front()->dst_variable == dst_var) {
            const std::string friend_var = chain[chain.size() - 1]->dst_variable;
            const std::string person_var = chain[chain.size() - 1]->src_variable;

            // Build Tag side: IndexScanValues(Tag) -> reversed expands up to friend.
            binder::BoundLogicalOperator tag_side;
            {
                binder::BoundLabelScanOp leaf;
                leaf.variable = chain.front()->dst_variable;
                leaf.column_index = 0;
                leaf.label_ids = {expand_dst_label != INVALID_LABEL_ID ? expand_dst_label : prop_label};
                leaf.label_prop_ids = chain.front()->dst_label_prop_ids;
                leaf.index_scan_values = true;
                leaf.index_id = index_id;
                tag_side = leaf;
            }
            for (size_t i = 0; i + 1 < chain.size(); ++i) {
                const auto* e = chain[i];
                auto expand = std::make_unique<binder::BoundExpandOp>();
                expand->src_variable = e->dst_variable;
                expand->src_column_index = 0;
                expand->edge_variable = e->edge_variable;
                expand->dst_variable = e->src_variable;
                expand->dst_column_index = 0;
                expand->edge_label_ids = e->edge_label_ids;
                expand->direction = e->direction;
                if (expand->direction == cypher::RelationshipDirection::LEFT_TO_RIGHT)
                    expand->direction = cypher::RelationshipDirection::RIGHT_TO_LEFT;
                else if (expand->direction == cypher::RelationshipDirection::RIGHT_TO_LEFT)
                    expand->direction = cypher::RelationshipDirection::LEFT_TO_RIGHT;
                if (i + 1 < chain.size() - 1 && chain[i + 1]->dst_variable == e->src_variable)
                    expand->dst_label_ids = chain[i + 1]->dst_label_ids;
                else if (i + 1 == chain.size() - 1 && chain[i + 1]->dst_variable == e->src_variable)
                    expand->dst_label_ids = chain[i + 1]->dst_label_ids;
                expand->child = std::move(tag_side);
                tag_side = std::move(expand);
            }

            // Build Person side from the original bottom-most expand + filter + leaf.
            binder::BoundLogicalOperator person_side;
            {
                auto filter = std::make_unique<binder::BoundFilterOp>();
                filter->predicate = std::move(start_filter_op->predicate);
                filter->child = *leaf_scan;
                person_side = std::move(filter);
            }
            {
                const auto* e = chain.back();
                auto expand = std::make_unique<binder::BoundExpandOp>();
                expand->src_variable = e->src_variable;
                expand->src_column_index = 0;
                expand->edge_variable = e->edge_variable;
                expand->dst_variable = e->dst_variable;
                expand->dst_column_index = 0;
                expand->edge_label_ids = e->edge_label_ids;
                expand->direction = e->direction;
                expand->dst_label_ids = e->dst_label_ids;
                expand->dst_label_prop_ids = e->dst_label_prop_ids;
                expand->edge_prop_ids = e->edge_prop_ids;
                expand->child = std::move(person_side);
                person_side = std::move(expand);
            }

            Schema right_input_schema;
            std::vector<binder::BoundType> right_input_types;
            auto tag_result = planBoundOperator(tag_side, store, meta, ctx, right_input_schema, right_input_types);
            if (std::holds_alternative<std::string>(tag_result))
                return std::nullopt;
            auto tr = extractChildResult(std::move(tag_result));

            auto person_result =
                planBoundOperator(person_side, store, meta, ctx, right_input_schema, right_input_types);
            if (std::holds_alternative<std::string>(person_result))
                return std::nullopt;
            auto pr = extractChildResult(std::move(person_result));

            int left_key = findColumn(tr.output_schema, friend_var);
            int right_key = findColumn(pr.output_schema, friend_var);
            if (left_key < 0 || right_key < 0)
                return std::nullopt;

            Schema hj_schema = tr.output_schema;
            hj_schema.insert(hj_schema.end(), pr.output_schema.begin(), pr.output_schema.end());
            auto hj_types = tr.output_types;
            hj_types.insert(hj_types.end(), pr.output_types.begin(), pr.output_types.end());
            TupleSlotLayout hj_layout = tr.slot_layout;
            hj_layout.merge(pr.slot_layout);

            auto hj = std::make_unique<HashJoinPhysicalOp>(std::move(tr.op), std::move(pr.op),
                                                           std::vector<uint32_t>{static_cast<uint32_t>(left_key)},
                                                           std::vector<uint32_t>{static_cast<uint32_t>(right_key)},
                                                           std::vector<binder::BoundType>(hj_types), hj_schema);
            hj->setEvalContext(ctx.eval_ctx);

            std::function<IndexScanValuesPhysicalOp*(PhysicalOperator*)> find_values =
                [&](PhysicalOperator* node) -> IndexScanValuesPhysicalOp* {
                if (auto* iv = dynamic_cast<IndexScanValuesPhysicalOp*>(node))
                    return iv;
                for (auto* child : node->children())
                    if (auto* iv = find_values(const_cast<PhysicalOperator*>(child)))
                        return iv;
                return nullptr;
            };
            IndexScanValuesPhysicalOp* value_sink = find_values(hj.get());
            if (!value_sink)
                return std::nullopt;

            Schema output_schema = lr.output_schema;
            output_schema.insert(output_schema.end(), hj_schema.begin(), hj_schema.end());
            auto output_types = lr.output_types;
            output_types.insert(output_types.end(), hj_types.begin(), hj_types.end());
            TupleSlotLayout layout = lr.slot_layout;
            layout.merge(hj_layout);

            auto apply = std::make_unique<ApplyPhysicalOp>(std::move(lr.op), std::move(hj), nullptr,
                                                           std::vector<uint32_t>{static_cast<uint32_t>(list_col)},
                                                           std::vector<binder::BoundType>(hj_types));
            apply->setValueSink(value_sink);
            apply->setEvalContext(ctx.eval_ctx);
            return PlanOperatorResult{std::move(apply), std::move(output_schema), std::move(output_types),
                                      std::move(layout)};
        }
    }

    auto saved_filter = ctx.expand_allowed_filter;
    ctx.expand_allowed_filter = ExpandAllowedFilterContext{index_id, edge_label, dst_var};
    ctx.filtered_expand = nullptr;

    Schema right_input_schema;
    std::vector<binder::BoundType> right_input_types;
    auto right_result = planBoundOperator(join.right, store, meta, ctx, right_input_schema, right_input_types);
    ctx.expand_allowed_filter = saved_filter;

    if (std::holds_alternative<std::string>(right_result) || !ctx.filtered_expand)
        return std::nullopt;

    auto rr = extractChildResult(std::move(right_result));

    Schema output_schema = lr.output_schema;
    output_schema.insert(output_schema.end(), rr.output_schema.begin(), rr.output_schema.end());
    auto output_types = lr.output_types;
    output_types.insert(output_types.end(), rr.output_types.begin(), rr.output_types.end());

    TupleSlotLayout layout = lr.slot_layout;
    layout.merge(rr.slot_layout);

    auto result = std::make_unique<ListIndexJoinPhysicalOp>(
        std::move(lr.op), std::move(rr.op), ctx.filtered_expand, static_cast<uint32_t>(list_col),
        std::vector<binder::BoundType>(output_types), output_schema);
    result->setEvalContext(ctx.eval_ctx);
    return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types), std::move(layout)};
}

/// Bottom-up (post-order) compilation: visit children first so each
/// operator can use its child's output layout to resolve its own
/// BoundColumnRef slot_ids → column_indices.
static void compileOperatorTree(PhysicalOperator* op, PlanContext& ctx) {
    if (!op)
        return;
    for (auto* child : op->children())
        compileOperatorTree(const_cast<PhysicalOperator*>(child), ctx);
    const auto& children = op->children();
    if (!children.empty() && children[0]) {
        TupleSlotLayout child_layout = children[0]->slotLayout();
        const auto& child_schema = children[0]->outputSchema();
        // Build/repair child_layout so it has one slot per output_schema entry.
        // Some operators (Expand, PathBuild, CreateNode, ...) append columns
        // but the inherited slot_layout doesn't include slots for them —
        // compileExpressions would then fail to resolve BoundColumnRef.slot_id
        // for those columns. Fall back to name-based layout whenever the
        // child's slot count doesn't match its schema size.
        if (child_layout.size() != child_schema.size() && !child_schema.empty())
            child_layout = makeSlotLayout(child_schema, ctx);
        op->compileExpressions(child_layout);
        // Only derive output layout if the planner didn't set one explicitly
        // (e.g. Project / PE / Aggregate set theirs via makeSlotLayout on
        // their own output_schema; overwriting would clobber appended-column
        // slots and misalign downstream column_index resolution).
        if (op->slotLayout().size() == 0)
            op->deriveOutputLayout(child_layout);
    }
}

std::variant<std::unique_ptr<PhysicalOperator>, std::string>
PhysicalPlanner::planBound(binder::BoundLogicalPlan& bound_plan, IAsyncGraphDataStore& store,
                           IAsyncGraphMetaStore& meta, PlanContext& ctx) {
    // Wire up evaluator context so that functions and property evaluators
    // can load vertex/edge properties lazily (e.g. for startNode results).
    ctx.eval_ctx.store = &store;
    ctx.eval_ctx.meta = &meta;
    ctx.eval_ctx.edge_label_defs = &ctx.edge_label_defs;

    // Phase 0: Allocate slot_ids for every variable name (incl. aliases) so
    // subsequent passes can resolve names synchronously. Also build the alias
    // slot map (alias_slot → canonical_slot) by scanning Project items of the
    // form `BoundVariableRef(X) AS Y`.
    optimizer::allocateAllSlots(bound_plan.root, ctx.var_slots, ctx.slot_allocator);
    ctx.alias_map = optimizer::collectAliasSlotMap(bound_plan.root, ctx.var_slots);

    // Collect fresh Expand bindings (dst/edge_slot_id == INVALID) so
    // requirements and source-type collection can add PEPlans for names
    // that canonicalForName would otherwise conflate with an alias chain
    // (§6.2).
    ctx.fresh_expands = optimizer::buildFreshExpandMap(bound_plan.root);
    const auto& fresh_expands = ctx.fresh_expands;

    // Phase 1: Scan the bound plan once to collect per-slot requirements
    // from every downstream consumer (Project / Filter / Sort / Aggregate /
    // Set / Remove / Delete / Merge). Variable references are canonicalized
    // through alias_map before keying the result.
    ctx.requirements =
        optimizer::collectPlanRequirements(bound_plan.root, ctx.resolver(), &fresh_expands, &ctx.label_defs);

    // Phase 2 (Decide): Allocate slot_ids for every appended PE column.
    // source_types tells the Decide phase which source columns are already
    // constructed objects (VERTEX/EDGE) so it can skip allocating a redundant
    // $obj slot — dispatchProjectionExtract then skips the matching Construct.
    auto source_types = optimizer::collectSourceTypes(bound_plan.root, ctx.resolver(), &fresh_expands);
    ctx.extraction_info =
        optimizer::buildExtractionInfo(ctx.requirements, source_types, ctx.resolver(), ctx.slot_allocator);

    // Phase 3 (Alias passthrough): For each Project item of the form
    // `BoundVariableRef(X) AS Y`, propagate X's derived PE columns through Y
    // so downstream consumers of Y can reference them via the alias slot.
    optimizer::lowerAliasPassthrough(bound_plan.root, ctx.extraction_info, ctx.var_slots, ctx.alias_map,
                                     ctx.slot_allocator);

    // Phase 4 (Lower): Rewrite BoundPropertyRef → BoundColumnRef using the
    // slot_ids allocated above. No base_col / ProjectResetMap machinery —
    // slot_ids are stable across schema-changing operators.
    optimizer::rewriteColumnIndices(bound_plan.root, ctx.extraction_info, ctx.resolver());

    Schema empty_schema;
    std::vector<binder::BoundType> empty_types;
    auto result = planBoundOperator(bound_plan.root, store, meta, ctx, empty_schema, empty_types);
    if (std::holds_alternative<std::string>(result))
        return std::get<std::string>(result);
    auto phys_op = finalizePlanResult(std::move(std::get<PlanOperatorResult>(result)));
    compileOperatorTree(phys_op.get(), ctx);
    return phys_op;
}

namespace {

// Recursive helper: walk the materialized BoundLogicalOperator tree and
// merge the Enricher's materialization requirements into the descendant
// scan/expand that produces `var`. This "lowers" the Enricher into the
// RBO-style label_prop_ids / dst_label_prop_ids / edge_prop_ids so
// collectPlanRequirements picks them up and dispatchProjectionExtract
// generates the corresponding ColumnSpec array. A proper runtime Enricher
// operator would make this function obsolete (Phase E).
void applyEnrichInPlace(binder::BoundLogicalOperator& op, const std::string& var,
                        const optimizer::MaterializationReq& req);

// Walk all variables in `enrich` and apply them to the tree rooted at `op`.
void applyMultiEnrich(binder::BoundLogicalOperator& op, const optimizer::VarRequirements& enrich) {
    for (const auto& [var, req] : enrich) {
        if (!req.empty())
            applyEnrichInPlace(op, var, req);
    }
}

// Materialize a ChosenPlan tree into a fresh BoundLogicalOperator tree by
// cloning each node's source op and recursively attaching materialized
// children. Mirrors Memo::copyOut's child attachment (handles both the
// single-child `child` field and binary operators' `left`/`right` pair).
// We rebuild rather than reuse plan.root because the CBO winner chain may
// select a different physical plan than the RBO path through plan.root.
//
// Enricher nodes (VertexEnrich/EdgeEnrich/PathEnrich) carry placeholder
// source ops (default BoundSingletonOp) — we fold their enrich_output
// into the descendant topology source's label_prop_ids / edge_prop_ids
// and skip them, so planBoundOperator's property-read wraps produce the
// correct runtime plan.
binder::BoundLogicalOperator materializeChosen(const optimizer::ChosenPlan& chosen) {
    // Enricher nodes: lower into child tree by merging enrich_output
    // into the descendant scan/expand that produces enrich_variable.
    if (chosen.tag == optimizer::PhysicalOpTag::VertexEnrich || chosen.tag == optimizer::PhysicalOpTag::EdgeEnrich ||
        chosen.tag == optimizer::PhysicalOpTag::PathEnrich) {
        auto child_op = materializeChosen(*chosen.children[0]);
        applyMultiEnrich(child_op, chosen.enrich_output);
        return child_op;
    }

    // PropertyExtract nodes: merge enrich_output into label_prop_ids so the
    // existing wrap pipeline can handle the property loading.
    if (chosen.tag == optimizer::PhysicalOpTag::VertexPropertyExtract ||
        chosen.tag == optimizer::PhysicalOpTag::EdgePropertyExtract ||
        chosen.tag == optimizer::PhysicalOpTag::PathPropertyExtract) {
        auto child_op = materializeChosen(*chosen.children[0]);
        applyMultiEnrich(child_op, chosen.enrich_output);
        return child_op;
    }

    // Non-Enricher nodes: materialize conventionally.
    binder::BoundLogicalOperator result = optimizer::cloneBoundLogicalOperator(chosen.op);

    if (chosen.children.size() == 1) {
        optimizer::setChild(result, materializeChosen(*chosen.children[0]));
    } else if (chosen.children.size() == 2) {
        if (std::holds_alternative<std::unique_ptr<binder::BoundBinaryJoinOp>>(result)) {
            auto& join = std::get<std::unique_ptr<binder::BoundBinaryJoinOp>>(result);
            join->left = materializeChosen(*chosen.children[0]);
            join->right = materializeChosen(*chosen.children[1]);
        } else if (std::holds_alternative<std::unique_ptr<binder::BoundSemiJoinOp>>(result)) {
            auto& sj = std::get<std::unique_ptr<binder::BoundSemiJoinOp>>(result);
            sj->left = materializeChosen(*chosen.children[0]);
            sj->right = materializeChosen(*chosen.children[1]);
        } else if (std::holds_alternative<std::unique_ptr<binder::BoundPatternComprehensionApplyOp>>(result)) {
            auto& pc = std::get<std::unique_ptr<binder::BoundPatternComprehensionApplyOp>>(result);
            pc->left = materializeChosen(*chosen.children[0]);
            pc->right = materializeChosen(*chosen.children[1]);
        } else if (std::holds_alternative<std::unique_ptr<binder::BoundLeftJoinOp>>(result)) {
            auto& lj = std::get<std::unique_ptr<binder::BoundLeftJoinOp>>(result);
            lj->left = materializeChosen(*chosen.children[0]);
            lj->right = materializeChosen(*chosen.children[1]);
        } else if (std::holds_alternative<std::unique_ptr<binder::BoundUnionOp>>(result)) {
            auto& uo = std::get<std::unique_ptr<binder::BoundUnionOp>>(result);
            uo->left = materializeChosen(*chosen.children[0]);
            uo->right = materializeChosen(*chosen.children[1]);
        }
    }
    return result;
}

// --- applyEnrichInPlace — recursive tree walkers ---

// Merge req.need_props into a vertex-level label→prop mapping.
void mergePropsIntoVertex(std::unordered_map<LabelId, std::vector<uint16_t>>& dst,
                          const optimizer::MaterializationReq& req) {
    for (const auto& [lid, props] : req.need_props) {
        auto& vec = dst[lid];
        for (uint16_t p : props) {
            if (std::find(vec.begin(), vec.end(), p) == vec.end())
                vec.push_back(p);
        }
    }
}

// Merge req.need_props into an edge-level prop list (not per-label).
void mergePropsIntoEdge(std::vector<uint16_t>& dst, const optimizer::MaterializationReq& req) {
    for (const auto& [lid, props] : req.need_props) {
        for (uint16_t p : props) {
            if (std::find(dst.begin(), dst.end(), p) == dst.end())
                dst.push_back(p);
        }
    }
}

// Recurse into a unary operator's child (bound form: single `child` field).
void recurseChild(binder::BoundLogicalOperator& op, const std::string& var, const optimizer::MaterializationReq& req) {
    std::visit(
        [&](auto& v) {
            using T = std::decay_t<decltype(v)>;
            // Unary operators: all have a `child` member of type BoundLogicalOperator.
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundAggregateOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundSkipOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundLimitOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundDistinctOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundUnwindOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>> ||
                          std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                if (v)
                    applyEnrichInPlace(v->child, var, req);
                // BoundCreateNodeOp stores child as optional<BoundLogicalOperator>.
                // All other write-op types store child as plain BoundLogicalOperator.
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateNodeOp>>) {
                if (v && v->child.has_value())
                    applyEnrichInPlace(*v->child, var, req);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCreateEdgeOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSetOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundRemoveOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundDeleteOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundMergeOp>>) {
                if (v)
                    applyEnrichInPlace(v->child, var, req);
            }
            // Binary operators: recursively walk left/right.
            else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>>) {
                if (v) {
                    applyEnrichInPlace(v->left, var, req);
                    applyEnrichInPlace(v->right, var, req);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLeftJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>> ||
                                 std::is_same_v<T, std::unique_ptr<binder::BoundPatternComprehensionApplyOp>>) {
                if (v) {
                    applyEnrichInPlace(v->left, var, req);
                    applyEnrichInPlace(v->right, var, req);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnionOp>>) {
                if (v) {
                    applyEnrichInPlace(v->left, var, req);
                    applyEnrichInPlace(v->right, var, req);
                }
            }
            // Leaf types: stop.
        },
        op);
}

void applyEnrichInPlace(binder::BoundLogicalOperator& op, const std::string& var,
                        const optimizer::MaterializationReq& req) {
    std::visit(
        [&](auto& v) {
            using T = std::decay_t<decltype(v)>;
            // Vertex-producing leaf operators.
            if constexpr (std::is_same_v<T, binder::BoundScanOp>) {
                if (v.variable == var)
                    mergePropsIntoVertex(v.label_prop_ids, req);
            } else if constexpr (std::is_same_v<T, binder::BoundLabelScanOp>) {
                if (v.variable == var)
                    mergePropsIntoVertex(v.label_prop_ids, req);
            }
            // Expand: three variables — src (from child), edge, dst.
            else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                if (!v)
                    return;
                if (v->dst_variable == var)
                    mergePropsIntoVertex(v->dst_label_prop_ids, req);
                if (v->edge_variable == var)
                    mergePropsIntoEdge(v->edge_prop_ids, req);
                applyEnrichInPlace(v->child, var, req);
            }
            // VarLenExpand: similar to Expand. Note: VarLenExpand uses
            // edge_prop_filters for filtering (not edge_prop_ids for eager
            // loading), so edge-property Enricher lowering drops the prop IDs
            // here. Edge props from VarLenExpand edges rely on the existing
            // planBoundOperator wrap which applies filters, not eager loads.
            else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                if (!v)
                    return;
                if (v->dst_variable == var)
                    mergePropsIntoVertex(v->dst_label_prop_ids, req);
                applyEnrichInPlace(v->child, var, req);
            }
            // Recurse into internal operators.
            else {
                recurseChild(op, var, req);
            }
        },
        op);
}

} // namespace

std::variant<std::unique_ptr<PhysicalOperator>, std::string>
PhysicalPlanner::planChosen(const optimizer::ChosenPlan& chosen, IAsyncGraphDataStore& store,
                            IAsyncGraphMetaStore& meta, PlanContext& ctx) {
    // CBO path: materializeChosen lowers PropertyExtract/Enricher tags into
    // label_prop_ids on descendant scans/expands. We then reuse the same
    // requirement-collection + column-rewrite + ProjectionExtract pipeline
    // as planBound so both paths converge on the unified extract operator.
    ctx.eval_ctx.store = &store;
    ctx.eval_ctx.meta = &meta;
    ctx.eval_ctx.edge_label_defs = &ctx.edge_label_defs;
    binder::BoundLogicalOperator materialized = materializeChosen(chosen);
    optimizer::allocateAllSlots(materialized, ctx.var_slots, ctx.slot_allocator);
    ctx.alias_map = optimizer::collectAliasSlotMap(materialized, ctx.var_slots);
    ctx.requirements = optimizer::collectPlanRequirements(materialized, ctx.resolver(), nullptr, &ctx.label_defs);
    auto source_types = optimizer::collectSourceTypes(materialized, ctx.resolver());
    ctx.extraction_info =
        optimizer::buildExtractionInfo(ctx.requirements, source_types, ctx.resolver(), ctx.slot_allocator);
    optimizer::lowerAliasPassthrough(materialized, ctx.extraction_info, ctx.var_slots, ctx.alias_map,
                                     ctx.slot_allocator);
    optimizer::rewriteColumnIndices(materialized, ctx.extraction_info, ctx.resolver());
    Schema empty_schema;
    std::vector<binder::BoundType> empty_types;
    auto result = planBoundOperator(materialized, store, meta, ctx, empty_schema, empty_types);
    if (std::holds_alternative<std::string>(result))
        return std::get<std::string>(result);
    auto phys_op = finalizePlanResult(std::move(std::get<PlanOperatorResult>(result)));
    compileOperatorTree(phys_op.get(), ctx);
    return phys_op;
}

std::variant<PlanOperatorResult, std::string>
PhysicalPlanner::planBoundOperator(binder::BoundLogicalOperator& op, IAsyncGraphDataStore& store,
                                   IAsyncGraphMetaStore& meta, PlanContext& ctx, Schema input_schema,
                                   const std::vector<binder::BoundType>& input_types) {
    return std::visit(
        [this, &store, &meta, &ctx, &input_schema, &input_types,
         &op](auto& val) -> std::variant<PlanOperatorResult, std::string> {
            using T = std::decay_t<decltype(val)>;

            // Resolve __anon__ label id once (used by scan operators for labels filtering)
            LabelId anon_id = INVALID_LABEL_ID;
            for (const auto& [lid, ldef] : ctx.label_defs) {
                if (ldef.name == kAnonLabelName) {
                    anon_id = lid;
                    break;
                }
            }

            if constexpr (std::is_same_v<T, binder::BoundSingletonOp>) {
                Schema output_schema;
                std::vector<binder::BoundType> output_types;
                auto result = std::make_unique<SingletonPhysicalOp>(std::vector<binder::BoundType>(output_types));
                TupleSlotLayout layout = makeSlotLayout(output_schema, ctx);
                return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types),
                                          std::move(layout)};
            } else if constexpr (std::is_same_v<T, binder::BoundCorrelatedSourceOp>) {
                Schema output_schema = val.variables;
                std::vector<binder::BoundType> output_types = val.types;
                auto result = std::make_unique<CorrelatedSourcePhysicalOp>(std::vector<std::string>(val.variables),
                                                                           std::vector<binder::BoundType>(val.types));
                // Use binder-assigned slot_ids when available so downstream
                // BoundColumnRefs (which carry bind-time slot_ids) resolve to
                // the correct columns in this operator's output layout.
                // Binder-assigned slots are set only by bindExistsSubPlan.
                // Correlated OPTIONAL MATCH sources still rely on column-index
                // correlation (bindOptionalMatch deliberately does not set
                // slot_ids); wrapping those in ProjectionExtract changes the
                // right sub-plan's schema and breaks LeftJoin matching.
                TupleSlotLayout layout;
                if (val.slot_ids.size() == output_schema.size() && !val.slot_ids.empty()) {
                    for (size_t i = 0; i < val.slot_ids.size(); ++i)
                        layout.append(val.slot_ids[i]);
                    auto plan_result = PlanOperatorResult{std::move(result), std::move(output_schema),
                                                          std::move(output_types), std::move(layout)};
                    plan_result = dispatchProjectionExtract(std::move(plan_result), store, ctx);
                    return plan_result;
                }
                layout = makeSlotLayout(output_schema, ctx);
                return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types),
                                          std::move(layout)};
            } else if constexpr (std::is_same_v<T, binder::BoundScanOp>) {
                Schema output_schema;
                std::vector<binder::BoundType> output_types;
                if (!val.variable.empty()) {
                    output_schema.push_back(val.variable);
                    output_types.push_back(binder::BoundType::VertexRef());
                }
                std::vector<LabelId> prune_labels;
                if (ctx.eval_ctx.allow_static_schema_pruning) {
                    auto hint = ctx.static_prune_hints.find(val.variable);
                    if (hint != ctx.static_prune_hints.end())
                        prune_labels = hint->second.vertex_labels;
                }
                auto result = std::make_unique<AllNodeScanPhysicalOp>(
                    val.variable, std::vector<binder::BoundType>(output_types), store, ctx.label_name_to_id,
                    ctx.label_defs, anon_id, std::unordered_map<LabelId, std::vector<uint16_t>>{},
                    std::move(prune_labels));
                TupleSlotLayout scan_layout = makeSlotLayout(output_schema, ctx);
                auto plan_result = PlanOperatorResult{std::move(result), std::move(output_schema),
                                                      std::move(output_types), std::move(scan_layout)};
                plan_result = dispatchProjectionExtract(std::move(plan_result), store, ctx);
                return plan_result;
            } else if constexpr (std::is_same_v<T, binder::BoundLabelScanOp>) {
                Schema output_schema;
                std::vector<binder::BoundType> output_types;
                if (!val.variable.empty()) {
                    output_schema.push_back(val.variable);
                    output_types.push_back(binder::BoundType::VertexRef());
                }
                std::unique_ptr<PhysicalOperator> result;
                if (val.index_scan_values && val.index_id != 0) {
                    result = std::make_unique<IndexScanValuesPhysicalOp>(val.variable, val.index_id, store,
                                                                         output_types, output_schema);
                } else {
                    result = std::make_unique<LabelScanPhysicalOp>(
                        val.variable, val.label_ids, std::vector<binder::BoundType>(output_types), store,
                        ctx.label_defs, anon_id, std::unordered_map<LabelId, std::vector<uint16_t>>{});
                }
                TupleSlotLayout scan_layout = makeSlotLayout(output_schema, ctx);
                auto plan_result = PlanOperatorResult{std::move(result), std::move(output_schema),
                                                      std::move(output_types), std::move(scan_layout)};
                plan_result = dispatchProjectionExtract(std::move(plan_result), store, ctx);
                return plan_result;
            } else {
                using Elem = typename T::element_type;
                if (!val)
                    return std::string("internal error: null operator in plan");
                auto& v = *val;

                if constexpr (std::is_same_v<Elem, binder::BoundExpandOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    auto child_op = std::move(cr.op);
                    auto child_schema = std::move(cr.output_schema);
                    auto output_types = std::move(cr.output_types);

                    std::optional<std::vector<EdgeLabelId>> label_filters = v.edge_label_ids;

                    int edge_existing = v.edge_variable.empty() ? -1 : findColumn(child_schema, v.edge_variable);
                    int dst_existing = v.dst_variable.empty() ? -1 : findColumn(child_schema, v.dst_variable);
                    bool edge_bound = edge_existing >= 0;
                    bool dst_bound = dst_existing >= 0;

                    Schema output_schema = child_schema;
                    if (!v.edge_variable.empty() && !edge_bound) {
                        output_schema.push_back(v.edge_variable);
                        output_types.push_back(binder::BoundType::EdgeKey());
                    }
                    if (!v.dst_variable.empty() && !dst_bound) {
                        output_schema.push_back(v.dst_variable);
                        output_types.push_back(binder::BoundType::VertexRef());
                    }

                    const bool anon_src = v.src_variable.empty() || v.src_variable.starts_with("__anon_");
                    const bool child_is_all_node_scan = std::holds_alternative<binder::BoundScanOp>(v.child);
                    const bool directed_single_label =
                        v.direction == cypher::RelationshipDirection::LEFT_TO_RIGHT && !v.edge_label_ids.empty();
                    bool full_edge_scan =
                        (v.direction == cypher::RelationshipDirection::UNDIRECTED || directed_single_label) &&
                        !edge_bound && !dst_bound && child_schema.size() == 1 && v.dst_label_ids.empty() && anon_src &&
                        child_is_all_node_scan;
                    std::vector<EdgeLabelId> full_scan_labels;
                    if (full_edge_scan) {
                        if (v.edge_label_ids.empty()) {
                            for (const auto& [lid, def] : ctx.edge_label_defs)
                                full_scan_labels.push_back(lid);
                        } else {
                            full_scan_labels = v.edge_label_ids;
                        }
                    }

                    auto result = std::make_unique<ExpandPhysicalOp>(
                        v.src_variable, v.dst_variable, v.edge_variable, std::move(label_filters), v.direction, store,
                        std::move(child_schema), std::vector<binder::BoundType>(output_types), std::move(child_op),
                        std::unordered_map<LabelId, std::vector<uint16_t>>{}, std::vector<uint16_t>{}, v.dst_label_ids,
                        dst_bound, edge_bound, dst_existing, edge_existing, std::move(full_scan_labels),
                        full_edge_scan);
                    if (ctx.expand_allowed_filter && ctx.expand_allowed_filter->dst_var == v.dst_variable &&
                        v.direction == cypher::RelationshipDirection::LEFT_TO_RIGHT) {
                        result->setAllowedDstFilter(ctx.expand_allowed_filter->index_id,
                                                    ctx.expand_allowed_filter->edge_label);
                        ctx.filtered_expand = result.get();
                    }
                    auto plan_result = PlanOperatorResult{std::move(result), std::move(output_schema),
                                                          std::move(output_types), TupleSlotLayout{}};
                    plan_result = dispatchProjectionExtract(std::move(plan_result), store, ctx);
                    return plan_result;
                } else if constexpr (std::is_same_v<Elem, binder::BoundVarLenExpandOp>) {
                    // Reverse a zero-or-more OUT varlen that starts from a
                    // LabelScan: start from the (usually much smaller)
                    // destination label instead, mirroring Neo4j's join order.
                    if (v.direction == cypher::RelationshipDirection::LEFT_TO_RIGHT && v.min_hops == 0 &&
                        v.max_hops == -1 && v.path_variable.empty() && v.edge_variable.empty() && !v.bound_edge_list &&
                        v.edge_prop_filters.empty() && std::holds_alternative<binder::BoundLabelScanOp>(v.child)) {
                        auto& src_scan = std::get<binder::BoundLabelScanOp>(v.child);
                        if (src_scan.label_ids.size() == 1 && !v.dst_label_ids.empty()) {
                            const auto old_src = v.src_variable;
                            const auto old_dst = v.dst_variable;
                            const auto old_src_labels = src_scan.label_ids;
                            const auto old_src_props = src_scan.label_prop_ids;
                            const auto old_dst_labels = v.dst_label_ids;
                            const auto old_dst_props = v.dst_label_prop_ids;

                            binder::BoundLabelScanOp new_scan;
                            new_scan.variable = old_dst;
                            new_scan.column_index = 0;
                            new_scan.label_ids = old_dst_labels;
                            new_scan.label_prop_ids = old_dst_props;

                            v.src_variable = old_dst;
                            v.dst_variable = old_src;
                            v.src_column_index = 0;
                            v.dst_column_index = 0;
                            v.dst_label_ids = old_src_labels;
                            v.dst_label_prop_ids = old_src_props;
                            v.direction = cypher::RelationshipDirection::RIGHT_TO_LEFT;
                            std::swap(v.src_filter_index_id, v.dst_filter_index_id);
                            std::swap(v.src_filter_value, v.dst_filter_value);
                            if (auto slot_it = ctx.var_slots.find(old_src); slot_it != ctx.var_slots.end()) {
                                v.dst_slot_id = slot_it->second;
                                v.planner_dst_slot_id = slot_it->second;
                            }
                            v.child = new_scan;
                        }
                    }
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    auto child_op = std::move(cr.op);
                    auto child_schema = std::move(cr.output_schema);
                    auto output_types = std::move(cr.output_types);

                    std::optional<std::vector<EdgeLabelId>> label_filters = v.edge_label_ids;

                    int dst_existing = v.dst_variable.empty() ? -1 : findColumn(child_schema, v.dst_variable);
                    bool dst_bound = dst_existing >= 0;

                    int edge_list_existing = -1;
                    if (v.bound_edge_list) {
                        edge_list_existing = findColumn(child_schema, v.edge_variable);
                        if (edge_list_existing < 0) {
                            return std::string("VarLenExpand: bound edge list '" + v.edge_variable +
                                               "' not found in child schema");
                        }
                    }

                    Schema output_schema = child_schema;
                    if (!dst_bound) {
                        output_schema.push_back(v.dst_variable);
                        output_types.push_back(binder::BoundType::VertexRef());
                    }

                    // P1: add PATH column if path variable is set
                    if (!v.path_variable.empty()) {
                        output_schema.push_back(v.path_variable);
                        output_types.push_back(binder::BoundType::Path());
                    }
                    // P2: add LIST<EDGE> column unless the edge variable is a
                    // bound input list (already present in child schema).
                    if (!v.edge_variable.empty() && !v.bound_edge_list) {
                        output_schema.push_back(v.edge_variable);
                        output_types.push_back(binder::BoundType::List(binder::BoundType::Edge()));
                    }

                    int prev_edge_existing = -1;
                    if (!v.prev_edge_var.empty()) {
                        prev_edge_existing = findColumn(child_schema, v.prev_edge_var);
                        if (prev_edge_existing < 0)
                            return std::string("VarLenExpand: previous edge '" + v.prev_edge_var +
                                               "' not found in child schema");
                    }

                    auto result = std::make_unique<VarLenExpandPhysicalOp>(
                        v.src_variable, v.dst_variable, std::move(label_filters), v.direction, v.min_hops, v.max_hops,
                        store, std::move(child_schema), std::vector<binder::BoundType>(output_types),
                        std::move(child_op), std::unordered_map<LabelId, std::vector<uint16_t>>{}, v.path_variable,
                        v.edge_variable, v.edge_prop_filters, v.dst_label_ids, dst_bound, dst_existing,
                        v.bound_edge_list, edge_list_existing, v.prev_edge_var, prev_edge_existing, v.dst_label_missing,
                        v.src_filter_index_id, v.src_filter_value, v.dst_filter_index_id, v.dst_filter_value);
                    auto plan_result = PlanOperatorResult{std::move(result), std::move(output_schema),
                                                          std::move(output_types), TupleSlotLayout{}};
                    // Phase D: VarLenExpand outputs VertexRef for dst; ProjectionExtract
                    // upgrades it (or appends property columns) per downstream requirements.
                    plan_result = dispatchProjectionExtract(std::move(plan_result), store, ctx);
                    return plan_result;
                } else if constexpr (std::is_same_v<Elem, binder::BoundFilterOp>) {
                    // Attach OR index-value pruning hints to a child VarLenExpand.
                    if (auto* vle_ptr = std::get_if<std::unique_ptr<binder::BoundVarLenExpandOp>>(&v.child)) {
                        if (vle_ptr && *vle_ptr)
                            trySetVarlenOrFilters(v, **vle_ptr, ctx);
                    }

                    // ── Index scan optimization: Filter(LabelScan) → IndexScan ──
                    if (std::holds_alternative<binder::BoundLabelScanOp>(v.child)) {
                        auto& scan_op = std::get<binder::BoundLabelScanOp>(v.child);
                        // Index scan only when single label (multi-label requires runtime intersection)
                        if (scan_op.label_ids.size() == 1) {
                            auto def_it = ctx.label_defs.find(scan_op.label_ids[0]);
                            if (def_it != ctx.label_defs.end()) {
                                std::vector<const binder::BoundBinaryOp*> conditions;
                                collectBoundConditions(v.predicate, conditions);
                                if (!conditions.empty()) {
                                    auto idx_result = tryBoundIndexScan(scan_op, conditions, scan_op.label_ids[0],
                                                                        def_it->second, store, ctx);
                                    if (idx_result.has_value())
                                        return std::move(idx_result.value());
                                }
                            }
                        }
                    }

                    // ── List index join: Filter(CrossProduct) with x.prop IN left.list ──
                    if (auto* join_op = std::get_if<std::unique_ptr<binder::BoundBinaryJoinOp>>(&v.child)) {
                        if (join_op && *join_op) {
                            auto list_join =
                                tryPlanListIndexJoin(v, **join_op, store, meta, ctx, input_schema, input_types);
                            if (list_join.has_value())
                                return std::move(list_join.value());
                        }
                    }

                    // ── Edge index scan optimization: Filter(Expand) → EdgeIndexScan ──
                    if (std::holds_alternative<std::unique_ptr<binder::BoundExpandOp>>(v.child)) {
                        auto& expand_ptr = std::get<std::unique_ptr<binder::BoundExpandOp>>(v.child);
                        if (expand_ptr->edge_label_ids.size() == 1) {
                            EdgeLabelId edge_label_id = expand_ptr->edge_label_ids[0];
                            auto def_it = ctx.edge_label_defs.find(edge_label_id);
                            if (def_it != ctx.edge_label_defs.end()) {
                                std::vector<const binder::BoundBinaryOp*> conditions;
                                collectBoundConditions(v.predicate, conditions);
                                if (!conditions.empty()) {
                                    auto idx_result = tryBoundEdgeIndexScan(*expand_ptr, conditions, edge_label_id,
                                                                            def_it->second, store, ctx);
                                    if (idx_result.has_value())
                                        return std::move(idx_result.value());
                                }
                            }
                        }
                    }

                    // Read-only IS NOT NULL property filters can restrict the
                    // scans below the filter to labels that define the property.
                    auto saved_prune_hints = ctx.static_prune_hints;
                    if (ctx.eval_ctx.allow_static_schema_pruning)
                        collectStaticPruneHints(v.predicate, ctx, ctx.static_prune_hints);
                    if (auto* scan_ptr = std::get_if<binder::BoundScanOp>(&v.child)) {
                        if (!scan_ptr->variable.empty()) {
                            auto labels = collectStaticVertexPruneLabels(ctx, v.predicate);
                            if (!labels.empty())
                                ctx.static_prune_hints[scan_ptr->variable].vertex_labels = std::move(labels);
                        }
                    }

                    if (auto* expand_ptr = std::get_if<std::unique_ptr<binder::BoundExpandOp>>(&v.child)) {
                        if (expand_ptr && *expand_ptr && !(*expand_ptr)->edge_variable.empty()) {
                            auto hint = ctx.static_prune_hints.find((*expand_ptr)->edge_variable);
                            if (hint != ctx.static_prune_hints.end() && !hint->second.edge_labels.empty()) {
                                (*expand_ptr)->edge_label_ids = hint->second.edge_labels;
                            } else {
                                auto edge_labels = collectStaticEdgePruneLabels(ctx, v.predicate);
                                if (!edge_labels.empty())
                                    (*expand_ptr)->edge_label_ids = std::move(edge_labels);
                            }
                        }
                    }

                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    ctx.static_prune_hints = std::move(saved_prune_hints);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));

                    // A filter directly above a CrossProduct may be a
                    // cross-scope equality filter whose anonymous right refs
                    // carry binder-local column indices. Offset them by the
                    // physical left output width (PE may have appended object
                    // columns after binding).
                    if (auto* cp = dynamic_cast<const CrossProductPhysicalOp*>(cr.op.get()))
                        offsetAnonymousColumnRefs(v.predicate, static_cast<uint32_t>(cp->leftColumnCount()));

                    auto result =
                        std::make_unique<FilterPhysicalOp>(std::move(v.predicate), cr.output_schema, std::move(cr.op));
                    result->setEvalContext(ctx.eval_ctx);
                    return PlanOperatorResult{std::move(result), std::move(cr.output_schema),
                                              std::move(cr.output_types), std::move(cr.slot_layout)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundProjectOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    auto child_op = std::move(cr.op);

                    std::vector<ProjectPhysicalOp::ProjectItem> items;
                    Schema output_schema;
                    std::vector<binder::BoundType> output_types;
                    for (auto& item : v.items) {
                        ProjectPhysicalOp::ProjectItem pi;
                        pi.expr = std::move(item.expr);
                        pi.name = std::move(item.alias);
                        items.push_back(std::move(pi));
                        output_schema.push_back(items.back().name);
                        output_types.push_back(std::move(item.result_type));
                    }

                    // Build the output layout from the pass-computed
                    // items[i].output_slot. This keeps the physical layout
                    // consistent with the data each rewritten item emits:
                    // a forwarded graph variable is promoted to its object
                    // slot, so the output column must be labelled with that
                    // slot for downstream covering references to resolve.
                    // Per-item fallback: items the pass didn't touch
                    // (output_slot == INVALID_SLOT_ID) fall back to name-based
                    // assignment, while items with a valid output_slot keep
                    // it. Mixing the two in one Project is legal — the pass
                    // may leave a scalar alias untouched while promoting a
                    // sibling graph variable.
                    TupleSlotLayout proj_layout;
                    for (size_t i = 0; i < output_schema.size(); ++i) {
                        binder::SlotId sid = v.items[i].output_slot;
                        if (sid == binder::INVALID_SLOT_ID) {
                            auto it = ctx.var_slots.find(output_schema[i]);
                            sid = it != ctx.var_slots.end() ? it->second : ctx.slot_allocator.next();
                            ctx.var_slots[output_schema[i]] = sid;
                        }
                        proj_layout.append(sid);
                    }

                    auto result = std::make_unique<ProjectPhysicalOp>(std::move(items), std::move(cr.output_schema),
                                                                      std::move(child_op));
                    result->setEvalContext(ctx.eval_ctx);
                    auto plan_result = PlanOperatorResult{std::move(result), std::move(output_schema),
                                                          std::move(output_types), std::move(proj_layout)};
                    // Note: we deliberately do NOT run dispatchProjectionExtract
                    // after Project. Pre-Project PE already appended Construct /
                    // property columns against the topology source; Project's
                    // items reference those object slots directly, so the alias
                    // output column carries the constructed VertexValue / EdgeValue.
                    // Re-running PE here would re-emit Construct for the alias
                    // and produce duplicate output columns.
                    return plan_result;
                } else if constexpr (std::is_same_v<Elem, binder::BoundAggregateOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    auto child_op = std::move(cr.op);

                    std::vector<AggregatePhysicalOp::GroupKey> group_keys;
                    for (size_t i = 0; i < v.group_keys.size() && i < v.output_names.size(); ++i) {
                        AggregatePhysicalOp::GroupKey gk;
                        gk.expr = std::move(v.group_keys[i]);
                        gk.name = v.output_names[i];
                        group_keys.push_back(std::move(gk));
                    }

                    std::vector<AggregatePhysicalOp::AggregateExpr> aggregates;
                    for (size_t i = 0; i < v.aggregates.size(); ++i) {
                        auto& ai = v.aggregates[i];
                        AggregatePhysicalOp::AggregateExpr ae;
                        ae.func_def = ai.func_def;
                        ae.arguments = std::move(ai.arguments);
                        ae.distinct = ai.distinct;
                        ae.is_internal = ai.is_internal;
                        ae.keeps_nulls = ai.keeps_nulls;
                        if (v.group_keys.size() + i < v.output_names.size())
                            ae.name = v.output_names[v.group_keys.size() + i];
                        aggregates.push_back(std::move(ae));
                    }

                    Schema output_schema(v.output_names.begin(), v.output_names.end());
                    std::vector<binder::BoundType> output_types;
                    std::vector<binder::BoundTypeKind> output_type_kinds;
                    for (size_t i = 0; i < group_keys.size(); ++i) {
                        auto gk_type = getBoundExprType(group_keys[i].expr);
                        output_types.push_back(gk_type);
                        output_type_kinds.push_back(gk_type.kind);
                    }
                    for (const auto& ae : aggregates) {
                        if (ae.is_internal)
                            continue;
                        if (ae.func_def) {
                            output_types.push_back(ae.func_def->return_type);
                            output_type_kinds.push_back(ae.func_def->return_type.kind);
                        } else {
                            output_types.push_back(binder::BoundType::Any());
                            output_type_kinds.push_back(binder::BoundTypeKind::ANY);
                        }
                    }
                    // Pattern comprehension's collect() is_internal aggregate
                    // emits a column consumed by the Apply op directly. Keep
                    // output_types aligned with output_schema so downstream
                    // passes (PE dispatch) can index by column position.
                    while (output_types.size() < output_schema.size())
                        output_types.push_back(binder::BoundType::Any());

                    auto result =
                        std::make_unique<AggregatePhysicalOp>(std::move(group_keys), std::move(aggregates),
                                                              std::move(child_op), std::move(output_type_kinds));
                    result->setEvalContext(ctx.eval_ctx);
                    TupleSlotLayout agg_layout = makeSlotLayout(output_schema, ctx);
                    auto plan_result = PlanOperatorResult{std::move(result), std::move(output_schema),
                                                          std::move(output_types), std::move(agg_layout)};
                    // PE after Aggregate: group-key variables that downstream
                    // inspects (e.g. RETURN x after WITH ... GROUP BY) need
                    // PE columns built against Aggregate's output schema.
                    plan_result = dispatchProjectionExtract(std::move(plan_result), store, ctx);
                    return plan_result;
                } else if constexpr (std::is_same_v<Elem, binder::BoundSortOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));

                    std::vector<SortPhysicalOp::SortItem> sort_items;
                    for (auto& si : v.items) {
                        SortPhysicalOp::SortItem item;
                        item.expr = std::move(si.expr);
                        item.ascending = (si.direction == cypher::OrderBy::Direction::ASC);
                        sort_items.push_back(std::move(item));
                    }

                    auto result = std::make_unique<SortPhysicalOp>(std::move(sort_items), std::move(cr.op));
                    result->setEvalContext(ctx.eval_ctx);
                    return PlanOperatorResult{std::move(result), std::move(cr.output_schema),
                                              std::move(cr.output_types), std::move(cr.slot_layout)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundSkipOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    auto result = std::make_unique<SkipPhysicalOp>(v.constant, std::move(v.expr), std::move(cr.op));
                    return PlanOperatorResult{std::move(result), std::move(cr.output_schema),
                                              std::move(cr.output_types), std::move(cr.slot_layout)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundLimitOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    auto result = std::make_unique<LimitPhysicalOp>(v.constant, std::move(v.expr), std::move(cr.op));
                    return PlanOperatorResult{std::move(result), std::move(cr.output_schema),
                                              std::move(cr.output_types), std::move(cr.slot_layout)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundDistinctOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    auto result = std::make_unique<DistinctPhysicalOp>(std::move(cr.op));
                    return PlanOperatorResult{std::move(result), std::move(cr.output_schema),
                                              std::move(cr.output_types), std::move(cr.slot_layout)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundPathBuildOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    auto child_op = std::move(cr.op);
                    auto child_schema = std::move(cr.output_schema);
                    auto output_types = std::move(cr.output_types);

                    Schema output_schema = child_schema;
                    output_schema.push_back(v.path_variable);
                    output_types.push_back(binder::BoundType::PathTopology());

                    auto result = std::make_unique<PathBuildPhysicalOp>(
                        v.path_variable, v.element_variables, std::move(child_schema),
                        std::vector<binder::BoundType>(output_types), std::move(child_op));
                    TupleSlotLayout path_layout = makeSlotLayout(output_schema, ctx);
                    auto plan_result = PlanOperatorResult{std::move(result), std::move(output_schema),
                                                          std::move(output_types), std::move(path_layout)};
                    // Phase D: PathBuild produces PathTopology; upgrade to PathValue for RETURN output.
                    plan_result = wrapPathElementPropertyRead(std::move(plan_result), v.path_variable, store);
                    return plan_result;
                } else if constexpr (std::is_same_v<Elem, binder::BoundCreateNodeOp>) {
                    spdlog::info("[Planner] BoundCreateNodeOp: var='{}', label_ids.size()={}, "
                                 "label_names.size()={}, pending_props.size()={}",
                                 v.variable, v.label_ids.size(), v.label_names.size(), v.pending_props.size());
                    std::vector<LabelId> label_ids = v.label_ids;
                    // Pass BoundExpressions to the operator for runtime evaluation.
                    std::vector<std::pair<LabelId, std::vector<std::pair<uint16_t, binder::BoundExpression>>>>
                        label_prop_exprs;
                    for (auto& [lid, props_vec] : v.label_properties) {
                        std::vector<std::pair<uint16_t, binder::BoundExpression>> exprs;
                        for (auto& [pid, expr] : props_vec) {
                            exprs.emplace_back(pid, std::move(expr));
                        }
                        label_prop_exprs.emplace_back(lid, std::move(exprs));
                    }

                    std::unique_ptr<PhysicalOperator> child;
                    Schema child_schema;
                    std::vector<binder::BoundType> child_types;
                    TupleSlotLayout child_layout;
                    if (v.child) {
                        auto child_result = planBoundOperator(*v.child, store, meta, ctx, input_schema, input_types);
                        if (std::holds_alternative<std::string>(child_result))
                            return std::get<std::string>(child_result);
                        auto cr = extractChildResult(std::move(child_result));
                        child = std::move(cr.op);
                        child_schema = std::move(cr.output_schema);
                        child_types = std::move(cr.output_types);
                        child_layout = std::move(cr.slot_layout);
                    }

                    // No AlterVertexLabelPhysicalOp — pending_props go to __anon__ via CreateNodePhysicalOp

                    auto result = std::make_unique<CreateNodePhysicalOp>(
                        v.variable, std::move(label_ids), std::move(label_prop_exprs), store, meta, std::move(child),
                        ctx.label_defs, std::move(v.pending_props), std::move(v.label_names));
                    result->setEvalContext(ctx.eval_ctx);
                    // Output schema: child columns + vertex column
                    Schema node_schema = child_schema;
                    std::vector<binder::BoundType> output_types = child_types;
                    if (!v.variable.empty()) {
                        node_schema.push_back(v.variable);
                        output_types.push_back(binder::BoundType::Vertex());
                    }
                    // Inherit child slot layout so upstream Project's slot remapping
                    // (e.g. alias → object_slot_id) stays consistent. Only append the
                    // new vertex column's slot. Using makeSlotLayout here would re-resolve
                    // names via var_slots and lose any alias/internal-slot remapping.
                    TupleSlotLayout node_layout = std::move(child_layout);
                    if (!v.variable.empty()) {
                        auto it = ctx.var_slots.find(v.variable);
                        binder::SlotId sid = it != ctx.var_slots.end() ? it->second : ctx.slot_allocator.next();
                        if (it == ctx.var_slots.end())
                            ctx.var_slots[v.variable] = sid;
                        node_layout.append(sid);
                    }
                    return PlanOperatorResult{std::move(result), std::move(node_schema), std::move(output_types),
                                              std::move(node_layout)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundCreateEdgeOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto child_cr = extractChildResult(std::move(child_result));
                    auto child_op = std::move(child_cr.op);
                    auto child_schema = std::move(child_cr.output_schema);
                    auto child_types = std::move(child_cr.output_types);

                    // Extract property (name, type) from pending_props for schema registration
                    std::vector<std::pair<std::string, PropertyType>> pending_prop_defs;
                    for (const auto& [name, expr] : v.pending_props) {
                        pending_prop_defs.emplace_back(name, boundExprToPropertyType(expr));
                    }

                    if (v.label_name.has_value()) {
                        child_op = std::make_unique<CreateEdgeLabelPhysicalOp>(
                            *v.label_name, std::move(pending_prop_defs), meta, store, ctx.edge_label_name_to_id,
                            ctx.edge_label_defs, std::move(child_op));
                    } else if (v.label_id.has_value() && !v.pending_props.empty()) {
                        auto def_it = ctx.edge_label_defs.find(*v.label_id);
                        if (def_it != ctx.edge_label_defs.end()) {
                            child_op = std::make_unique<AlterEdgeLabelPhysicalOp>(
                                def_it->second.name, std::move(pending_prop_defs), meta, ctx.edge_label_defs,
                                std::move(child_op));
                        }
                    }

                    // Find src/dst column indices from child schema
                    size_t src_col = SIZE_MAX, dst_col = SIZE_MAX;
                    for (size_t i = 0; i < child_schema.size(); ++i) {
                        if (child_schema[i] == v.src_variable)
                            src_col = i;
                        if (child_schema[i] == v.dst_variable)
                            dst_col = i;
                    }

                    // Pass BoundExpressions for runtime evaluation.
                    std::vector<std::pair<uint16_t, binder::BoundExpression>> prop_exprs;
                    for (auto& [pid, expr] : v.properties) {
                        prop_exprs.emplace_back(pid, std::move(expr));
                    }

                    auto result = std::make_unique<CreateEdgePhysicalOp>(
                        v.variable, src_col, dst_col, v.label_id, std::move(prop_exprs), store, meta,
                        ctx.edge_label_defs, v.label_name, ctx.edge_label_name_to_id, std::move(v.pending_props),
                        std::move(child_op));
                    result->setEvalContext(ctx.eval_ctx);
                    // Output schema: child columns + edge column
                    Schema edge_schema = child_schema;
                    std::vector<binder::BoundType> edge_types = child_types;
                    if (!v.variable.empty()) {
                        edge_schema.push_back(v.variable);
                        edge_types.push_back(binder::BoundType::Edge());
                    }
                    // Inherit child slot layout (see CreateNode branch for rationale).
                    TupleSlotLayout edge_layout = std::move(child_cr.slot_layout);
                    if (!v.variable.empty()) {
                        auto it = ctx.var_slots.find(v.variable);
                        binder::SlotId sid = it != ctx.var_slots.end() ? it->second : ctx.slot_allocator.next();
                        if (it == ctx.var_slots.end())
                            ctx.var_slots[v.variable] = sid;
                        edge_layout.append(sid);
                    }
                    return PlanOperatorResult{std::move(result), std::move(edge_schema), std::move(edge_types),
                                              std::move(edge_layout)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundSetOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    cr = extractChildResult(dispatchProjectionExtract(
                        PlanOperatorResult{std::move(cr.op), std::move(cr.output_schema), std::move(cr.output_types),
                                           std::move(cr.slot_layout)},
                        store, ctx));

                    std::vector<SetPhysicalOp::BoundSetItem> items;
                    for (auto& si : v.items) {
                        SetPhysicalOp::BoundSetItem bsi;
                        switch (si.kind) {
                        case binder::BoundSetOp::ItemKind::SET_PROPERTY:
                            bsi.kind = cypher::SetItemKind::SET_PROPERTY;
                            break;
                        case binder::BoundSetOp::ItemKind::SET_PROPERTIES:
                            bsi.kind = cypher::SetItemKind::SET_PROPERTIES;
                            break;
                        case binder::BoundSetOp::ItemKind::SET_LABELS:
                            bsi.kind = cypher::SetItemKind::SET_LABELS;
                            break;
                        }
                        bsi.var_name = si.target_variable;
                        bsi.object_col = resolveMutationColumn(ctx, cr.slot_layout, si.target_variable);
                        bsi.prop_name = si.prop_name;
                        bsi.strong_mode = si.strong_mode;
                        bsi.is_add_assign = si.is_add_assign;
                        bsi.resolved_label_id = si.label_id;
                        bsi.resolved_prop_id = si.prop_id;
                        bsi.label = si.label;
                        if (bsi.label.empty() && si.label_id) {
                            for (auto& [name, id] : ctx.label_name_to_id) {
                                if (id == *si.label_id) {
                                    bsi.label = name;
                                    break;
                                }
                            }
                        }
                        if (si.value_expr)
                            bsi.value = std::move(si.value_expr);
                        items.push_back(std::move(bsi));
                    }

                    auto result = std::make_unique<SetPhysicalOp>(std::move(items), cr.output_schema, store, meta,
                                                                  ctx.label_defs, ctx.edge_label_defs,
                                                                  ctx.label_name_to_id, anon_id, std::move(cr.op));
                    result->setEvalContext(ctx.eval_ctx);
                    return PlanOperatorResult{std::move(result), std::move(cr.output_schema),
                                              std::move(cr.output_types), std::move(cr.slot_layout)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundRemoveOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    cr = extractChildResult(dispatchProjectionExtract(
                        PlanOperatorResult{std::move(cr.op), std::move(cr.output_schema), std::move(cr.output_types),
                                           std::move(cr.slot_layout)},
                        store, ctx));

                    std::vector<RemovePhysicalOp::BoundRemoveItem> items;
                    for (auto& ri : v.items) {
                        RemovePhysicalOp::BoundRemoveItem bri;
                        bri.kind = (ri.kind == binder::BoundRemoveOp::ItemKind::REMOVE_LABEL)
                                       ? RemovePhysicalOp::BoundRemoveItem::Kind::LABEL
                                       : RemovePhysicalOp::BoundRemoveItem::Kind::PROPERTY;
                        bri.var_name = ri.target_variable;
                        bri.object_col = resolveMutationColumn(ctx, cr.slot_layout, ri.target_variable);
                        bri.name = ri.prop_name;
                        bri.strong_mode = ri.strong_mode;
                        bri.resolved_label_id = ri.label_id;
                        bri.resolved_prop_id = ri.prop_id;
                        items.push_back(std::move(bri));
                    }

                    auto result = std::make_unique<RemovePhysicalOp>(std::move(items), cr.output_schema, store, meta,
                                                                     ctx.label_defs, ctx.label_name_to_id, anon_id,
                                                                     ctx.edge_label_defs, std::move(cr.op));
                    return PlanOperatorResult{std::move(result), std::move(cr.output_schema),
                                              std::move(cr.output_types), std::move(cr.slot_layout)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundDeleteOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    cr = extractChildResult(dispatchProjectionExtract(
                        PlanOperatorResult{std::move(cr.op), std::move(cr.output_schema), std::move(cr.output_types),
                                           std::move(cr.slot_layout)},
                        store, ctx));

                    std::vector<DeletePhysicalOp::DeleteTarget> targets;
                    for (auto& dt : v.targets) {
                        DeletePhysicalOp::DeleteTarget target;
                        if (dt.kind) {
                            target.kind = (*dt.kind == binder::BoundDeleteOp::TargetKind::VERTEX)
                                              ? DeletePhysicalOp::TargetKind::VERTEX
                                              : DeletePhysicalOp::TargetKind::EDGE;
                            target.var_name = dt.variable_name;
                            target.object_col = resolveMutationColumn(ctx, cr.slot_layout, dt.variable_name);
                        } else {
                            target.expr = std::move(dt.expr);
                        }
                        targets.push_back(std::move(target));
                    }

                    LabelId anon_label_id = INVALID_LABEL_ID;
                    for (const auto& [lid, ldef] : ctx.label_defs) {
                        if (ldef.name == kAnonLabelName) {
                            anon_label_id = lid;
                            break;
                        }
                    }
                    auto result = std::make_unique<DeletePhysicalOp>(std::move(targets), v.detach, cr.output_schema,
                                                                     store, ctx.label_defs, ctx.edge_label_defs,
                                                                     anon_label_id, std::move(cr.op));
                    return PlanOperatorResult{std::move(result), std::move(cr.output_schema),
                                              std::move(cr.output_types), std::move(cr.slot_layout)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundUnwindOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    auto child_op = std::move(cr.op);
                    auto child_schema = std::move(cr.output_schema);
                    auto output_types = std::move(cr.output_types);

                    // The unwind variable is appended AFTER all child columns
                    // (which may include PE-appended columns not present at
                    // bind time). The binder's variable_column_index is stale
                    // in the presence of PE — use the actual child column count
                    // as the new column's physical index.
                    uint32_t unwind_col_index = static_cast<uint32_t>(child_schema.size());

                    Schema output_schema = child_schema;
                    output_schema.push_back(v.variable);
                    output_types.push_back(binder::BoundType::Any());

                    auto result = std::make_unique<UnwindPhysicalOp>(
                        std::move(v.list_expr), unwind_col_index, binder::BoundType::Any(), std::move(child_schema),
                        std::vector<binder::BoundType>(output_types), std::move(child_op));
                    result->setEvalContext(ctx.eval_ctx);
                    TupleSlotLayout unwind_layout = makeSlotLayout(output_schema, ctx);
                    return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types),
                                              std::move(unwind_layout)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundUnionOp>) {
                    auto left_result = planBoundOperator(v.left, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(left_result))
                        return std::get<std::string>(left_result);
                    auto lr = extractChildResult(std::move(left_result));

                    Schema right_input_schema;
                    std::vector<binder::BoundType> right_input_types;
                    auto right_result =
                        planBoundOperator(v.right, store, meta, ctx, right_input_schema, right_input_types);
                    if (std::holds_alternative<std::string>(right_result))
                        return std::get<std::string>(right_result);
                    auto rr = extractChildResult(std::move(right_result));

                    // Both sides must have the same number of columns
                    if (lr.output_schema.size() != rr.output_schema.size())
                        return std::string("UNION requires both sides to have the same number of columns");

                    auto result = std::make_unique<UnionPhysicalOp>(v.all, std::move(lr.op), std::move(rr.op));
                    auto output_types = lr.output_types;

                    if (!v.all) {
                        // UNION (without ALL) requires deduplication
                        auto distinct = std::make_unique<DistinctPhysicalOp>(std::move(result));
                        return PlanOperatorResult{std::move(distinct), std::move(lr.output_schema),
                                                  std::move(output_types), std::move(lr.slot_layout)};
                    }

                    return PlanOperatorResult{std::move(result), std::move(lr.output_schema), std::move(output_types),
                                              std::move(lr.slot_layout)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundBinaryJoinOp>) {
                    auto left_result = planBoundOperator(v.left, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(left_result))
                        return std::get<std::string>(left_result);
                    auto lr = extractChildResult(std::move(left_result));

                    // Remap right child's column indices: the binder assigns
                    // global indices (0 for left, N for right), but the right
                    // child produces columns starting from 0 locally.
                    auto right_col_offset = static_cast<uint32_t>(lr.output_schema.size());
                    remapLogicalOpColumnIndices(v.right, right_col_offset);

                    Schema right_input_schema;
                    std::vector<binder::BoundType> right_input_types;
                    auto right_result =
                        planBoundOperator(v.right, store, meta, ctx, right_input_schema, right_input_types);
                    if (std::holds_alternative<std::string>(right_result))
                        return std::get<std::string>(right_result);
                    auto rr = extractChildResult(std::move(right_result));

                    Schema output_schema = lr.output_schema;
                    output_schema.insert(output_schema.end(), rr.output_schema.begin(), rr.output_schema.end());
                    auto output_types = lr.output_types;
                    output_types.insert(output_types.end(), rr.output_types.begin(), rr.output_types.end());

                    // Output layout = concat of children's slot_layouts. We
                    // must inherit the children's actual output slots — Project
                    // rewrites raise `WITH a` from source slot to object slot
                    // (covering), so rebuilding from var_slots here would
                    // produce a stale slot for "a" and downstream refs (which
                    // point at the object slot) would fail column resolution.
                    TupleSlotLayout xprod_layout;
                    xprod_layout.merge(lr.slot_layout);
                    xprod_layout.merge(rr.slot_layout);

                    if (v.join_type == binder::JoinType::Hash) {
                        auto hash_join = std::make_unique<HashJoinPhysicalOp>(
                            std::move(lr.op), std::move(rr.op), std::vector<uint32_t>(v.left_keys),
                            std::vector<uint32_t>(v.right_keys), std::vector<binder::BoundType>(output_types),
                            output_schema);
                        hash_join->setEvalContext(ctx.eval_ctx);
                        return PlanOperatorResult{std::move(hash_join), std::move(output_schema),
                                                  std::move(output_types), std::move(xprod_layout)};
                    }

                    if (v.correlated) {
                        std::function<CorrelatedSourcePhysicalOp*(PhysicalOperator*)> find_source =
                            [&](PhysicalOperator* node) -> CorrelatedSourcePhysicalOp* {
                            if (auto* cs = dynamic_cast<CorrelatedSourcePhysicalOp*>(node))
                                return cs;
                            for (auto* child : node->children()) {
                                if (auto* cs = find_source(const_cast<PhysicalOperator*>(child)))
                                    return cs;
                            }
                            return nullptr;
                        };
                        CorrelatedSourcePhysicalOp* source = find_source(rr.op.get());
                        if (!source)
                            return std::string("Apply: CorrelatedSourcePhysicalOp not found in right sub-plan");
                        std::vector<uint32_t> left_corr_cols;
                        left_corr_cols.reserve(v.correlation.size());
                        for (const auto& [left_slot, right_slot] : v.correlation) {
                            (void)right_slot;
                            int col = lr.slot_layout.getColumnIndex(left_slot);
                            if (col < 0)
                                return std::string("Apply: left correlation slot has no physical column");
                            left_corr_cols.push_back(static_cast<uint32_t>(col));
                        }
                        auto apply = std::make_unique<ApplyPhysicalOp>(std::move(lr.op), std::move(rr.op), source,
                                                                       std::move(left_corr_cols),
                                                                       std::vector<binder::BoundType>(rr.output_types));
                        apply->setEvalContext(ctx.eval_ctx);
                        return PlanOperatorResult{std::move(apply), std::move(output_schema), std::move(output_types),
                                                  std::move(xprod_layout)};
                    }

                    auto result = std::make_unique<CrossProductPhysicalOp>(
                        std::move(lr.op), std::move(rr.op), std::move(lr.output_schema), std::move(rr.output_schema),
                        std::vector<binder::BoundType>(output_types));
                    result->setEvalContext(ctx.eval_ctx);
                    return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types),
                                              std::move(xprod_layout)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundSemiJoinOp>) {
                    auto left_result = planBoundOperator(v.left, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(left_result))
                        return std::get<std::string>(left_result);
                    auto lr = extractChildResult(std::move(left_result));

                    Schema right_input_schema;
                    std::vector<binder::BoundType> right_input_types;
                    auto right_result =
                        planBoundOperator(v.right, store, meta, ctx, right_input_schema, right_input_types);
                    if (std::holds_alternative<std::string>(right_result))
                        return std::get<std::string>(right_result);
                    auto rr = extractChildResult(std::move(right_result));

                    // Non-correlated EXISTS: the right sub-plan has no correlation
                    // to the left. Use a cross-product with right limited to 1
                    // row: if right produces any row, left rows pass through;
                    // if right produces no rows, the cross product is empty.
                    if (v.correlation.empty()) {
                        auto limit = std::make_unique<LimitPhysicalOp>(std::optional<int64_t>(1), std::nullopt,
                                                                       std::move(rr.op));
                        limit->setEvalContext(ctx.eval_ctx);

                        TupleSlotLayout layout = lr.slot_layout;
                        layout.merge(rr.slot_layout);

                        auto cross = std::make_unique<CrossProductPhysicalOp>(
                            std::move(lr.op), std::move(limit), std::move(lr.output_schema),
                            std::move(rr.output_schema), std::move(lr.output_types));
                        cross->setEvalContext(ctx.eval_ctx);

                        return PlanOperatorResult{std::move(cross), std::move(lr.output_schema),
                                                  std::move(lr.output_types), std::move(layout)};
                    }

                    // Correlated EXISTS: find CorrelatedSourcePhysicalOp in right sub-plan.
                    std::function<CorrelatedSourcePhysicalOp*(PhysicalOperator*)> findCorrelatedSource =
                        [&](PhysicalOperator* node) -> CorrelatedSourcePhysicalOp* {
                        if (auto* cs = dynamic_cast<CorrelatedSourcePhysicalOp*>(node))
                            return cs;
                        for (auto* child : node->children()) {
                            if (auto* cs = findCorrelatedSource(const_cast<PhysicalOperator*>(child)))
                                return cs;
                        }
                        return nullptr;
                    };

                    CorrelatedSourcePhysicalOp* correlated = findCorrelatedSource(rr.op.get());
                    if (!correlated)
                        return std::string("SemiJoin: CorrelatedSourcePhysicalOp not found in right sub-plan");

                    // Resolve each left slot_id to its physical column position
                    // in the left's TupleSlotLayout. SlotIds are used (not
                    // column_index) because ProjectionExtract may have appended
                    // columns, breaking the column_index == physical position
                    // assumption.
                    std::vector<uint32_t> left_corr_cols;
                    left_corr_cols.reserve(v.correlation.size());
                    for (const auto& [left_slot, _] : v.correlation) {
                        int pos = lr.slot_layout.getColumnIndex(left_slot);
                        if (pos < 0) {
                            return std::string("SemiJoin: left slot " + std::to_string(left_slot) +
                                               " not found in left slot layout (size " +
                                               std::to_string(lr.slot_layout.size()) + ")");
                        }
                        left_corr_cols.push_back(static_cast<uint32_t>(pos));
                    }

                    auto result = std::make_unique<SemiJoinPhysicalOp>(std::move(lr.op), std::move(rr.op), correlated,
                                                                       std::move(left_corr_cols), v.anti);
                    return PlanOperatorResult{std::move(result), std::move(lr.output_schema),
                                              std::move(lr.output_types), std::move(lr.slot_layout)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundLeftJoinOp>) {
                    auto left_result = planBoundOperator(v.left, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(left_result))
                        return std::get<std::string>(left_result);
                    auto lr = extractChildResult(std::move(left_result));

                    Schema right_input_schema;
                    std::vector<binder::BoundType> right_input_types;
                    auto right_result =
                        planBoundOperator(v.right, store, meta, ctx, right_input_schema, right_input_types);
                    if (std::holds_alternative<std::string>(right_result))
                        return std::get<std::string>(right_result);
                    auto rr = extractChildResult(std::move(right_result));

                    Schema output_schema = lr.output_schema;
                    output_schema.insert(output_schema.end(), rr.output_schema.begin(), rr.output_schema.end());
                    auto output_types = lr.output_types;
                    output_types.insert(output_types.end(), rr.output_types.begin(), rr.output_types.end());

                    // Find CorrelatedSourcePhysicalOp in right sub-plan (if correlated)
                    CorrelatedSourcePhysicalOp* correlated = nullptr;
                    if (!v.correlation.empty()) {
                        std::function<CorrelatedSourcePhysicalOp*(PhysicalOperator*)> findCorrelatedSource =
                            [&](PhysicalOperator* node) -> CorrelatedSourcePhysicalOp* {
                            if (auto* cs = dynamic_cast<CorrelatedSourcePhysicalOp*>(node))
                                return cs;
                            for (auto* child : node->children()) {
                                if (auto* cs = findCorrelatedSource(const_cast<PhysicalOperator*>(child)))
                                    return cs;
                            }
                            return nullptr;
                        };
                        correlated = findCorrelatedSource(rr.op.get());
                        if (!correlated)
                            return std::string("LeftJoin: CorrelatedSourcePhysicalOp not found in right sub-plan");
                    }

                    std::vector<uint32_t> left_corr_cols;
                    left_corr_cols.reserve(v.correlation.size());
                    for (const auto& corr : v.correlation) {
                        // Prefer the binder column when it still points at the
                        // same variable. PE may append an object column and a
                        // name-based slot can resolve to that object instead
                        // of the raw topology column CorrelatedSource needs.
                        int pos = static_cast<int>(corr.left_column);
                        if (pos < 0 || static_cast<size_t>(pos) >= lr.output_schema.size() ||
                            lr.output_schema[pos] != corr.left_var)
                            pos = lr.slot_layout.getColumnIndex(corr.left_slot);
                        if (pos < 0 || static_cast<size_t>(pos) >= lr.output_schema.size())
                            pos = findColumn(lr.output_schema, corr.left_var);
                        if (pos < 0) {
                            return std::string("LeftJoin: correlation for '" + corr.left_var +
                                               "' not found in left output (slot " + std::to_string(corr.left_slot) +
                                               ", col " + std::to_string(corr.left_column) + ")");
                        }
                        left_corr_cols.push_back(static_cast<uint32_t>(pos));
                    }

                    // Output layout = concat of children's slot_layouts (see
                    // BinaryJoin note on why we must inherit, not rebuild).
                    TupleSlotLayout ljoin_layout;
                    ljoin_layout.merge(lr.slot_layout);
                    ljoin_layout.merge(rr.slot_layout);

                    auto result =
                        std::make_unique<LeftJoinPhysicalOp>(std::move(lr.op), std::move(rr.op), correlated,
                                                             std::move(left_corr_cols), std::move(rr.output_types));
                    return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types),
                                              std::move(ljoin_layout)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundPatternComprehensionApplyOp>) {
                    auto left_result = planBoundOperator(v.left, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(left_result))
                        return std::get<std::string>(left_result);
                    auto lr = extractChildResult(std::move(left_result));

                    Schema right_input_schema;
                    std::vector<binder::BoundType> right_input_types;
                    auto right_result =
                        planBoundOperator(v.right, store, meta, ctx, right_input_schema, right_input_types);
                    if (std::holds_alternative<std::string>(right_result))
                        return std::get<std::string>(right_result);
                    auto rr = extractChildResult(std::move(right_result));

                    if (v.correlation.empty())
                        return std::string("PatternComprehensionApply: empty correlation is unsupported (requires at "
                                           "least one outer variable)");

                    // Locate the CorrelatedSourcePhysicalOp leaf in the right sub-plan.
                    std::function<CorrelatedSourcePhysicalOp*(PhysicalOperator*)> findCorrelatedSource =
                        [&](PhysicalOperator* node) -> CorrelatedSourcePhysicalOp* {
                        if (auto* cs = dynamic_cast<CorrelatedSourcePhysicalOp*>(node))
                            return cs;
                        for (auto* child : node->children()) {
                            if (auto* cs = findCorrelatedSource(const_cast<PhysicalOperator*>(child)))
                                return cs;
                        }
                        return nullptr;
                    };
                    CorrelatedSourcePhysicalOp* correlated = findCorrelatedSource(rr.op.get());
                    if (!correlated)
                        return std::string(
                            "PatternComprehensionApply: CorrelatedSourcePhysicalOp not found in right sub-plan");

                    // Resolve left side correlation slots to physical column positions.
                    std::vector<uint32_t> left_corr_cols;
                    left_corr_cols.reserve(v.correlation.size());
                    for (size_t ci = 0; ci < v.correlation.size(); ++ci) {
                        const auto& corr = v.correlation[ci];
                        int pos = static_cast<int>(corr.left_column);
                        if (pos < 0 || static_cast<size_t>(pos) >= lr.output_schema.size() ||
                            lr.output_schema[pos] != corr.left_var)
                            pos = lr.slot_layout.getColumnIndex(corr.left_slot);
                        if (pos < 0 && ci < lr.output_schema.size())
                            pos = static_cast<int>(ci);
                        if (pos < 0 || static_cast<size_t>(pos) >= lr.output_schema.size()) {
                            return std::string(
                                "PatternComprehensionApply: left slot " + std::to_string(corr.left_slot) +
                                " not found in left slot layout (size " + std::to_string(lr.slot_layout.size()) + ")");
                        }
                        left_corr_cols.push_back(static_cast<uint32_t>(pos));
                    }

                    // Output schema: left columns + one LIST column per output.
                    std::vector<binder::BoundType> list_elem_types;
                    list_elem_types.reserve(v.outputs.size());
                    Schema output_schema = lr.output_schema;
                    std::vector<binder::BoundType> output_types = lr.output_types;
                    TupleSlotLayout out_layout = lr.slot_layout;
                    for (auto& out : v.outputs) {
                        binder::BoundType list_t = binder::BoundType::List(out.element_type);
                        output_schema.push_back(out.name);
                        output_types.push_back(list_t);
                        list_elem_types.push_back(out.element_type);
                        out_layout.append(out.slot_id);
                    }

                    auto result = std::make_unique<PatternComprehensionApplyPhysicalOp>(
                        std::move(lr.op), std::move(rr.op), correlated, std::move(left_corr_cols),
                        std::move(list_elem_types));
                    result->setEvalContext(ctx.eval_ctx);
                    return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types),
                                              std::move(out_layout)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundMergeOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    // Insert PE so mutation ops receive ConstructVertex/Edge
                    // columns for graph variables upstream.
                    cr = extractChildResult(dispatchProjectionExtract(
                        PlanOperatorResult{std::move(cr.op), std::move(cr.output_schema), std::move(cr.output_types),
                                           std::move(cr.slot_layout)},
                        store, ctx));
                    auto child_op = std::move(cr.op);
                    auto child_schema = std::move(cr.output_schema);
                    auto output_types = std::move(cr.output_types);

                    auto convertItems = [&](std::vector<binder::BoundSetOp::SetItem>& src) {
                        std::vector<SetPhysicalOp::BoundSetItem> result;
                        for (auto& si : src) {
                            SetPhysicalOp::BoundSetItem bsi;
                            switch (si.kind) {
                            case binder::BoundSetOp::ItemKind::SET_PROPERTY:
                                bsi.kind = cypher::SetItemKind::SET_PROPERTY;
                                break;
                            case binder::BoundSetOp::ItemKind::SET_PROPERTIES:
                                bsi.kind = cypher::SetItemKind::SET_PROPERTIES;
                                break;
                            case binder::BoundSetOp::ItemKind::SET_LABELS:
                                bsi.kind = cypher::SetItemKind::SET_LABELS;
                                break;
                            }
                            bsi.var_name = si.target_variable;
                            bsi.object_col = resolveMutationColumn(ctx, cr.slot_layout, si.target_variable);
                            bsi.prop_name = si.prop_name;
                            bsi.strong_mode = si.strong_mode;
                            bsi.is_add_assign = si.is_add_assign;
                            bsi.resolved_label_id = si.label_id;
                            bsi.resolved_prop_id = si.prop_id;
                            bsi.label = si.label;
                            if (bsi.label.empty() && si.label_id) {
                                for (auto& [name, id] : ctx.label_name_to_id) {
                                    if (id == *si.label_id) {
                                        bsi.label = name;
                                        break;
                                    }
                                }
                            }
                            if (si.value_expr)
                                bsi.value = std::move(si.value_expr);
                            result.push_back(std::move(bsi));
                        }
                        return result;
                    };

                    auto on_create = convertItems(v.on_create_items);
                    auto on_match = convertItems(v.on_match_items);

                    Schema output_schema = child_schema;

                    if (!v.start_pre_bound) {
                        output_schema.push_back(v.start_var);
                        output_types.push_back(binder::BoundType::Vertex());
                    }
                    if (v.has_relationship) {
                        if (!v.end_pre_bound) {
                            output_schema.push_back(v.end_var);
                            output_types.push_back(binder::BoundType::Vertex());
                        }
                        if (!v.edge_var.empty()) {
                            output_schema.push_back(v.edge_var);
                            output_types.push_back(binder::BoundType::Edge());
                        }
                    }
                    if (v.path_variable) {
                        output_schema.push_back(*v.path_variable);
                        output_types.push_back(binder::BoundType::Path());
                    }

                    auto result = std::make_unique<MergePhysicalOp>(
                        v.start_var, v.start_pre_bound, std::move(v.start_labels), std::move(v.start_prop_filters),
                        std::move(v.start_pending_props), v.has_relationship, v.edge_var, v.edge_label_id,
                        v.edge_label_name, v.direction, std::move(v.edge_prop_filters), std::move(v.edge_pending_props),
                        v.end_var, v.end_pre_bound, std::move(v.end_labels), std::move(v.end_prop_filters),
                        std::move(v.end_pending_props), v.path_variable, std::move(on_create), std::move(on_match),
                        store, meta, ctx.label_defs, ctx.label_name_to_id, ctx.edge_label_defs,
                        ctx.edge_label_name_to_id, std::move(child_op));
                    result->setEvalContext(ctx.eval_ctx);
                    TupleSlotLayout merge_layout = makeSlotLayout(output_schema, ctx);
                    return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types),
                                              std::move(merge_layout)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundCallOp>) {
                    Schema output_schema = v.output_names;
                    std::vector<binder::BoundType> output_types = v.output_types;
                    auto result = std::make_unique<CallPhysicalOp>(v.procedure_name, std::move(v.output_names),
                                                                   std::vector<binder::BoundType>(v.output_types),
                                                                   &store, &meta, ctx.func_registry);
                    TupleSlotLayout layout = makeSlotLayout(output_schema, ctx);
                    return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types),
                                              std::move(layout)};
                } else {
                    return std::string("Unknown bound logical operator type");
                }
            }
        },
        op);
}

} // namespace compute
} // namespace eugraph
