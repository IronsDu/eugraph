#include "query/physical_plan/physical_planner.hpp"
#include "common/types/graph_types.hpp"
#include "query/optimizer/chosen_plan.hpp"
#include "query/optimizer/column_rewrite.hpp"
#include "query/optimizer/memo.hpp"
#include "query/physical_plan/operator/correlated_source_physical_op.hpp"
#include "query/physical_plan/operator/cross_product_physical_op.hpp"
#include "query/physical_plan/operator/delete_physical_op.hpp"
#include "query/physical_plan/operator/distinct_physical_op.hpp"
#include "query/physical_plan/operator/left_join_physical_op.hpp"
#include "query/physical_plan/operator/merge_physical_op.hpp"
#include "query/physical_plan/operator/path_element_property_read_physical_op.hpp"
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
    for (const auto& [lid, ldef] : ctx.label_defs)
        if (ldef.name != kAnonLabelName)
            vertex_label_names[lid] = ldef.name;
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
        std::move(child_result.op));
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
                    remapExprColumnIndices(item.argument, offset);
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
    uint16_t prop_id;
    cypher::BinaryOperator op;
    PropertyValue value;
};

/// Try to extract a property=value condition from a BoundBinaryOp.
static std::optional<BoundIndexableCondition> tryExtractBoundCondition(const binder::BoundBinaryOp& binop) {
    if (!std::holds_alternative<std::unique_ptr<binder::BoundPropertyRef>>(binop.left))
        return std::nullopt;
    auto& prop_ref = std::get<std::unique_ptr<binder::BoundPropertyRef>>(binop.left);
    if (prop_ref->candidates.empty())
        return std::nullopt;

    if (!std::holds_alternative<binder::BoundLiteral>(binop.right))
        return std::nullopt;
    auto& lit = std::get<binder::BoundLiteral>(binop.right);

    auto pv = valueToPropertyValue(lit.value);
    if (std::holds_alternative<std::monostate>(pv))
        return std::nullopt;

    return BoundIndexableCondition{prop_ref->candidates[0].prop_id, binop.op, std::move(pv)};
}

std::optional<PlanOperatorResult>
PhysicalPlanner::tryBoundIndexScan(const binder::BoundLabelScanOp& scan_op,
                                   const std::vector<const binder::BoundBinaryOp*>& conditions, LabelId label_id,
                                   const LabelDef& label_def, IAsyncGraphDataStore& store, PlanContext& ctx) {
    std::unordered_map<uint16_t, BoundIndexableCondition> prop_conditions;
    for (auto* cond : conditions) {
        auto extracted = tryExtractBoundCondition(*cond);
        if (extracted.has_value())
            prop_conditions[extracted->prop_id] = std::move(*extracted);
    }

    if (prop_conditions.empty())
        return std::nullopt;

    for (const auto& idx : label_def.indexes) {
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

        using ScanMode = IndexScanPhysicalOp::ScanMode;
        std::unique_ptr<IndexScanPhysicalOp> result;

        Schema output_schema;
        std::vector<binder::BoundType> output_types;
        if (!scan_op.variable.empty()) {
            output_schema.push_back(scan_op.variable);
            output_types.push_back(binder::BoundType::VertexRef());
        }

        auto result_output_types = output_types;

        if (!last_is_range) {
            result = std::make_unique<IndexScanPhysicalOp>(scan_op.variable, label_id, idx.prop_ids, ScanMode::EQUALITY,
                                                           std::move(eq_values), std::nullopt, std::nullopt,
                                                           std::move(output_types), store, ctx.label_defs);
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
            result = std::make_unique<IndexScanPhysicalOp>(
                scan_op.variable, label_id, idx.prop_ids, ScanMode::RANGE, std::vector<PropertyValue>{},
                std::move(composite_start), std::move(composite_end), std::move(output_types), store, ctx.label_defs);
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
        auto extracted = tryExtractBoundCondition(*cond);
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
    ctx.requirements = optimizer::collectPlanRequirements(bound_plan.root, ctx.resolver(), &fresh_expands);

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
                                 std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>>) {
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
    ctx.requirements = optimizer::collectPlanRequirements(materialized, ctx.resolver());
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
        [this, &store, &meta, &ctx, &input_schema,
         &input_types](auto& val) -> std::variant<PlanOperatorResult, std::string> {
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
                TupleSlotLayout layout = makeSlotLayout(output_schema, ctx);
                return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types),
                                          std::move(layout)};
            } else if constexpr (std::is_same_v<T, binder::BoundScanOp>) {
                Schema output_schema;
                std::vector<binder::BoundType> output_types;
                if (!val.variable.empty()) {
                    output_schema.push_back(val.variable);
                    output_types.push_back(binder::BoundType::VertexRef());
                }
                auto result = std::make_unique<AllNodeScanPhysicalOp>(
                    val.variable, std::vector<binder::BoundType>(output_types), store, ctx.label_name_to_id,
                    ctx.label_defs, anon_id, std::unordered_map<LabelId, std::vector<uint16_t>>{});
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
                auto result = std::make_unique<LabelScanPhysicalOp>(
                    val.variable, val.label_ids, std::vector<binder::BoundType>(output_types), store, ctx.label_defs,
                    anon_id, std::unordered_map<LabelId, std::vector<uint16_t>>{});
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

                    Schema output_schema = child_schema;
                    if (!v.edge_variable.empty()) {
                        output_schema.push_back(v.edge_variable);
                        output_types.push_back(binder::BoundType::EdgeKey());
                    }
                    if (!v.dst_variable.empty()) {
                        output_schema.push_back(v.dst_variable);
                        output_types.push_back(binder::BoundType::VertexRef());
                    }

                    auto result = std::make_unique<ExpandPhysicalOp>(
                        v.src_variable, v.dst_variable, v.edge_variable, std::move(label_filters), v.direction, store,
                        std::move(child_schema), std::vector<binder::BoundType>(output_types), std::move(child_op),
                        std::unordered_map<LabelId, std::vector<uint16_t>>{}, std::vector<uint16_t>{}, v.dst_label_ids);
                    auto plan_result = PlanOperatorResult{std::move(result), std::move(output_schema),
                                                          std::move(output_types), TupleSlotLayout{}};
                    plan_result = dispatchProjectionExtract(std::move(plan_result), store, ctx);
                    return plan_result;
                } else if constexpr (std::is_same_v<Elem, binder::BoundVarLenExpandOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    auto child_op = std::move(cr.op);
                    auto child_schema = std::move(cr.output_schema);
                    auto output_types = std::move(cr.output_types);

                    std::optional<std::vector<EdgeLabelId>> label_filters = v.edge_label_ids;

                    Schema output_schema = child_schema;
                    output_schema.push_back(v.dst_variable);
                    output_types.push_back(binder::BoundType::VertexRef());

                    // P1: add PATH column if path variable is set
                    if (!v.path_variable.empty()) {
                        output_schema.push_back(v.path_variable);
                        output_types.push_back(binder::BoundType::Path());
                    }
                    // P2: add LIST<EDGE> column if edge variable is set
                    if (!v.edge_variable.empty()) {
                        output_schema.push_back(v.edge_variable);
                        output_types.push_back(binder::BoundType::List(binder::BoundType::Edge()));
                    }

                    auto result = std::make_unique<VarLenExpandPhysicalOp>(
                        v.src_variable, v.dst_variable, std::move(label_filters), v.direction, v.min_hops, v.max_hops,
                        store, std::move(child_schema), std::vector<binder::BoundType>(output_types),
                        std::move(child_op), std::unordered_map<LabelId, std::vector<uint16_t>>{}, v.path_variable,
                        v.edge_variable, v.edge_prop_filters);
                    auto plan_result = PlanOperatorResult{std::move(result), std::move(output_schema),
                                                          std::move(output_types), TupleSlotLayout{}};
                    // Phase D: VarLenExpand outputs VertexRef for dst; ProjectionExtract
                    // upgrades it (or appends property columns) per downstream requirements.
                    plan_result = dispatchProjectionExtract(std::move(plan_result), store, ctx);
                    return plan_result;
                } else if constexpr (std::is_same_v<Elem, binder::BoundFilterOp>) {
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

                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));

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
                        ae.arg = std::move(ai.argument);
                        ae.distinct = ai.distinct;
                        ae.is_internal = ai.is_internal;
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
                    auto result = std::make_unique<SkipPhysicalOp>(v.count, std::move(cr.op));
                    return PlanOperatorResult{std::move(result), std::move(cr.output_schema),
                                              std::move(cr.output_types), std::move(cr.slot_layout)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundLimitOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    auto result = std::make_unique<LimitPhysicalOp>(v.count, std::move(cr.op));
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
                        ctx.label_defs, std::move(v.pending_props));
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
                        if (si.label_id) {
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

                    auto result =
                        std::make_unique<SetPhysicalOp>(std::move(items), cr.output_schema, store, meta, ctx.label_defs,
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

                    auto result = std::make_unique<RemovePhysicalOp>(std::move(items), cr.output_schema, store,
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
                        target.kind = (dt.kind == binder::BoundDeleteOp::TargetKind::VERTEX)
                                          ? DeletePhysicalOp::TargetKind::VERTEX
                                          : DeletePhysicalOp::TargetKind::EDGE;
                        target.var_name = dt.variable_name;
                        target.object_col = resolveMutationColumn(ctx, cr.slot_layout, dt.variable_name);
                        targets.push_back(std::move(target));
                    }

                    auto result = std::make_unique<DeletePhysicalOp>(std::move(targets), v.detach, cr.output_schema,
                                                                     store, std::move(cr.op));
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

                    // Find the CorrelatedSourcePhysicalOp leaf in the right sub-plan.
                    // It must be the unique leaf node of the EXISTS sub-plan.
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

                    // Build left correlation column indices from the mapping
                    std::vector<uint32_t> left_corr_cols;
                    left_corr_cols.reserve(v.correlation.size());
                    for (const auto& [left_col, _] : v.correlation) {
                        left_corr_cols.push_back(left_col);
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
                    for (const auto& [left_col, _] : v.correlation) {
                        left_corr_cols.push_back(left_col);
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
                            if (si.label_id) {
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
                } else {
                    return std::string("Unknown bound logical operator type");
                }
            }
        },
        op);
}

} // namespace compute
} // namespace eugraph
