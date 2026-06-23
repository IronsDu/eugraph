#include "query/physical_plan/physical_planner.hpp"
#include "common/types/graph_types.hpp"
#include "query/optimizer/chosen_plan.hpp"
#include "query/optimizer/column_rewrite.hpp"
#include "query/optimizer/memo.hpp"
#include "query/physical_plan/operator/correlated_source_physical_op.hpp"
#include "query/physical_plan/operator/cross_product_physical_op.hpp"
#include "query/physical_plan/operator/delete_physical_op.hpp"
#include "query/physical_plan/operator/distinct_physical_op.hpp"
#include "query/physical_plan/operator/edge_property_extract_physical_op.hpp"
#include "query/physical_plan/operator/left_join_physical_op.hpp"
#include "query/physical_plan/operator/merge_physical_op.hpp"
#include "query/physical_plan/operator/path_element_property_read_physical_op.hpp"
#include "query/physical_plan/operator/semi_join_physical_op.hpp"
#include "query/physical_plan/operator/singleton_physical_op.hpp"
#include "query/physical_plan/operator/union_physical_op.hpp"
#include "query/physical_plan/operator/unwind_physical_op.hpp"
#include "query/physical_plan/operator/varlen_expand_physical_op.hpp"
#include "query/physical_plan/operator/vertex_property_extract_physical_op.hpp"
#include "query/planner/bound_logical_plan.hpp"

#include <functional>
#include <stdexcept>

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

static PlanOperatorResult
dispatchVertexPropertyExtract(PlanOperatorResult&& child_result, const std::string& variable,
                              const std::unordered_map<LabelId, std::vector<uint16_t>>& label_prop_ids,
                              bool include_labels, IAsyncGraphDataStore& store,
                              const std::unordered_map<LabelId, LabelDef>& label_defs) {
    size_t col_idx = SIZE_MAX;
    for (size_t i = child_result.output_schema.size(); i-- > 0;)
        if (child_result.output_schema[i] == variable) {
            col_idx = i;
            break;
        }
    if (col_idx == SIZE_MAX)
        return std::move(child_result);
    if (!include_labels && label_prop_ids.empty())
        return std::move(child_result);

    std::vector<VertexPropertyExtractPhysicalOp::LabelRequest> label_requests;
    std::vector<VertexPropertyExtractPhysicalOp::PropRequest> prop_requests;
    Schema output_schema = child_result.output_schema;
    auto output_types = child_result.output_types;

    std::unordered_map<LabelId, std::string> label_id_to_name;
    if (include_labels) {
        label_requests.push_back({col_idx});
        output_schema.push_back(variable + ".labels");
        output_types.push_back(binder::BoundType::List(binder::BoundType::String()));
        for (const auto& [lid, ldef] : label_defs)
            if (ldef.name != kAnonLabelName)
                label_id_to_name[lid] = ldef.name;
    }

    for (const auto& [lid, pids] : label_prop_ids) {
        auto def_it = label_defs.find(lid);
        for (uint16_t pid : pids) {
            std::string pn = std::to_string(pid);
            binder::BoundType pt = binder::BoundType::Any();
            if (def_it != label_defs.end())
                for (const auto& pd : def_it->second.properties)
                    if (pd.id == pid) {
                        pn = pd.name;
                        pt = propertyTypeToBoundType(pd.type);
                        break;
                    }
            output_schema.push_back(variable + "." + pn);
            output_types.push_back(pt);
            prop_requests.push_back({col_idx, lid, pid});
        }
    }

    auto extract_op = std::make_unique<VertexPropertyExtractPhysicalOp>(
        variable, std::move(label_requests), std::move(prop_requests),
        std::vector<VertexPropertyExtractPhysicalOp::VertexRequest>{}, store, std::move(label_id_to_name),
        Schema(child_result.output_schema), std::vector<binder::BoundType>(output_types),
        std::move(child_result.op));
    return PlanOperatorResult{std::move(extract_op), std::move(output_schema), std::move(output_types)};
}

/// Create an EdgePropertyExtractPhysicalOp that appends flat edge property columns.
static PlanOperatorResult
dispatchEdgePropertyExtract(PlanOperatorResult&& child_result, const std::string& variable,
                            const std::vector<uint16_t>& edge_prop_ids, EdgeLabelId edge_label_id,
                            IAsyncGraphDataStore& store,
                            const std::unordered_map<EdgeLabelId, EdgeLabelDef>& edge_label_defs) {
    if (variable.empty() || edge_prop_ids.empty())
        return std::move(child_result);
    size_t col_idx = SIZE_MAX;
    for (size_t i = child_result.output_schema.size(); i-- > 0;)
        if (child_result.output_schema[i] == variable) {
            col_idx = i;
            break;
        }
    if (col_idx == SIZE_MAX)
        return std::move(child_result);

    Schema output_schema = child_result.output_schema;
    auto output_types = child_result.output_types;
    auto def_it = edge_label_defs.find(edge_label_id);
    for (uint16_t pid : edge_prop_ids) {
        std::string pn = std::to_string(pid);
        binder::BoundType pt = binder::BoundType::Any();
        if (def_it != edge_label_defs.end()) {
            for (const auto& pd : def_it->second.properties) {
                if (pd.id == pid) {
                    pn = pd.name;
                    pt = propertyTypeToBoundType(pd.type);
                    break;
                }
            }
        }
        output_schema.push_back(variable + "." + pn);
        output_types.push_back(pt);
    }

    auto extract_op = std::make_unique<EdgePropertyExtractPhysicalOp>(
        variable, col_idx, edge_prop_ids, store, Schema(child_result.output_schema),
        std::vector<binder::BoundType>(output_types), std::move(child_result.op));
    return PlanOperatorResult{std::move(extract_op), std::move(output_schema), std::move(output_types)};
}

// Returns true if the plan is safe for standalone extract.
// - Single-scan only (no WITH/multi-MATCH) — extract appends columns
// - Only labeled scans (no unlabeled AllNodeScan)
// - No BoundDynamicPropertyRef
// - At least one known (non-__anon__) property
static bool canUsePropertyExtract(const binder::BoundLogicalOperator& op, PlanContext& ctx) {
    std::function<bool(const binder::BoundExpression&)> exprHasDPR;
    exprHasDPR = [&](const binder::BoundExpression& expr) -> bool {
        return std::visit(
            [&](const auto& ptr) -> bool {
                using T = std::decay_t<decltype(ptr)>;
                if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDynamicPropertyRef>>)
                    return true;
                else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryOp>>)
                    return exprHasDPR(ptr->left) || exprHasDPR(ptr->right);
                else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnaryOp>>)
                    return exprHasDPR(ptr->operand);
                else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFunctionCall>>) {
                    for (const auto& a : ptr->args)
                        if (exprHasDPR(a))
                            return true;
                    return false;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>>)
                    return exprHasDPR(ptr->object);
                else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundList>>) {
                    for (const auto& e : ptr->elements)
                        if (exprHasDPR(e))
                            return true;
                    return false;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCase>>) {
                    if (ptr->subject && exprHasDPR(*ptr->subject))
                        return true;
                    for (const auto& [w, t] : ptr->when_thens)
                        if (exprHasDPR(w) || exprHasDPR(t))
                            return true;
                    if (ptr->else_expr && exprHasDPR(*ptr->else_expr))
                        return true;
                    return false;
                }
                return false;
            },
            expr);
    };
    std::function<bool(const binder::BoundLogicalOperator&)> opHasDPR;
    opHasDPR = [&](const binder::BoundLogicalOperator& o) -> bool {
        return std::visit(
            [&](const auto& v) -> bool {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>>) {
                    if (v) {
                        if (exprHasDPR(v->predicate))
                            return true;
                        return opHasDPR(v->child);
                    }
                    return false;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                    if (v) {
                        for (auto& i : v->items)
                            if (exprHasDPR(i.expr))
                                return true;
                        return opHasDPR(v->child);
                    }
                    return false;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>>) {
                    if (v) {
                        for (auto& i : v->items)
                            if (exprHasDPR(i.expr))
                                return true;
                        return opHasDPR(v->child);
                    }
                    return false;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAggregateOp>>) {
                    if (v) {
                        for (auto& e : v->group_keys)
                            if (exprHasDPR(e))
                                return true;
                        for (auto& a : v->aggregates)
                            if (exprHasDPR(a.argument))
                                return true;
                        return opHasDPR(v->child);
                    }
                    return false;
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>> ||
                                     std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>> ||
                                     std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>> ||
                                     std::is_same_v<T, std::unique_ptr<binder::BoundUnwindOp>>)
                    return v && opHasDPR(v->child);
                else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSkipOp>> ||
                                   std::is_same_v<T, std::unique_ptr<binder::BoundLimitOp>> ||
                                   std::is_same_v<T, std::unique_ptr<binder::BoundDistinctOp>>)
                    return v && opHasDPR(v->child);
                else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryJoinOp>>)
                    return v && (opHasDPR(v->left) || opHasDPR(v->right));
                else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundLeftJoinOp>> ||
                                   std::is_same_v<T, std::unique_ptr<binder::BoundSemiJoinOp>> ||
                                   std::is_same_v<T, std::unique_ptr<binder::BoundUnionOp>>)
                    return v && (opHasDPR(v->left) || opHasDPR(v->right));
                return false;
            },
            o);
    };
    if (opHasDPR(op))
        return false;

    // Count scans — only single-scan queries are safe (no WITH/multi-MATCH)
    int scan_count = 0;
    bool has_unlabeled = false;
    auto walk = [&](auto& self, const binder::BoundLogicalOperator& o) -> void {
        std::visit(
            [&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, binder::BoundScanOp>) {
                    ++scan_count;
                    if (!v.label_prop_ids.empty())
                        has_unlabeled = true;
                } else if constexpr (std::is_same_v<T, binder::BoundLabelScanOp>) {
                    ++scan_count;
                } else if constexpr (std::is_same_v<T, binder::BoundBinaryJoinOp> ||
                                     std::is_same_v<T, binder::BoundLeftJoinOp> ||
                                     std::is_same_v<T, binder::BoundSemiJoinOp> ||
                                     std::is_same_v<T, binder::BoundUnionOp>) {
                    if (v) {
                        self(self, v->left);
                        self(self, v->right);
                    }
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                    if (v)
                        self(self, v->child);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                    if (v)
                        self(self, v->child);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>>) {
                    if (v)
                        self(self, v->child);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                    if (v)
                        self(self, v->child);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>>) {
                    if (v)
                        self(self, v->child);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>>) {
                    if (v)
                        self(self, v->child);
                }
            },
            o);
    };
    // Also count ProjectOps — WITH creates extra ProjectOps that reference
    // columns by index, which would be broken by extract column shifts.
    int proj_count = 0;
    auto countProj = [&](auto& self, const binder::BoundLogicalOperator& o) -> void {
        std::visit(
            [&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>)
                    ++proj_count;
                if constexpr (std::is_same_v<T, binder::BoundBinaryJoinOp> ||
                              std::is_same_v<T, binder::BoundLeftJoinOp> ||
                              std::is_same_v<T, binder::BoundSemiJoinOp> || std::is_same_v<T, binder::BoundUnionOp>) {
                    if (v) {
                        self(self, v->left);
                        self(self, v->right);
                    }
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundExpandOp>>) {
                    if (v)
                        self(self, v->child);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundVarLenExpandOp>>) {
                    if (v)
                        self(self, v->child);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>>) {
                    if (v)
                        self(self, v->child);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                    if (v)
                        self(self, v->child);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>>) {
                    if (v)
                        self(self, v->child);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPathBuildOp>>) {
                    if (v)
                        self(self, v->child);
                }
            },
            o);
    };
    countProj(countProj, op);

    walk(walk, op);
    if (scan_count != 1)
        return false;
    if (has_unlabeled)
        return false;
    // Only single-Project queries (no WITH): extra ProjectOps reference
    // columns by index which would be shifted by extract.
    if (proj_count > 1)
        return false;

    auto info = optimizer::collectExtractionFromPlan(op, 0);
    if (info.empty())
        return false;
    for (const auto& [var, ext] : info) {
        for (const auto& [lid, pid] : ext.prop_order) {
            auto def_it = ctx.label_defs.find(lid);
            if (def_it != ctx.label_defs.end() && def_it->second.name != kAnonLabelName)
                return true;
        }
    }
    return false;
}

// ==================== Deferred Property Loading Wrappers ====================

/// Inline label loader for non-standalone path. Upgrades VertexRef→VertexValue
/// in-place (preserves old wrap semantics for BoundDynamicPropertyRef / RETURN n).
static PlanOperatorResult
loadVertexLabelsInPlace(PlanOperatorResult&& child_result, const std::string& variable,
                        IAsyncGraphDataStore& store) {
    if (variable.empty())
        return std::move(child_result);
    size_t col_idx = SIZE_MAX;
    for (size_t i = child_result.output_schema.size(); i-- > 0;)
        if (child_result.output_schema[i] == variable) { col_idx = i; break; }
    if (col_idx == SIZE_MAX)
        return std::move(child_result);

    auto output_types = std::vector<binder::BoundType>(child_result.output_types);
    output_types[col_idx] = binder::BoundType::Vertex();

    struct LabelReadOp : PhysicalOperator {
        size_t ci; IAsyncGraphDataStore& s; std::vector<binder::BoundType> ot;
        std::unique_ptr<PhysicalOperator> ch;
        LabelReadOp(size_t c, IAsyncGraphDataStore& st, std::vector<binder::BoundType> o,
                    std::unique_ptr<PhysicalOperator> chld)
            : ci(c), s(st), ot(std::move(o)), ch(std::move(chld)) {}
        folly::coro::AsyncGenerator<RowBatch> execute() override { return executeViaChunk(); }
        folly::coro::AsyncGenerator<DataChunk> executeChunk() override {
            auto g = ch->executeChunk();
            while (auto ck = co_await g.next()) {
                size_t n = ck->numRows();
                auto en = Column::flat(binder::BoundTypeKind::VERTEX, n);
                for (size_t r = 0; r < n; ++r) {
                    auto v = ck->columns[ci].getValue(r);
                    VertexValue vv;
                    if (std::holds_alternative<VertexRef>(v)) vv.id = std::get<VertexRef>(v).id;
                    else if (std::holds_alternative<VertexValue>(v)) vv = std::get<VertexValue>(v);
                    else continue;
                    auto lbs = co_await s.getVertexLabels(vv.id);
                    lbs.erase(INVALID_LABEL_ID);
                    vv.labels = std::move(lbs);
                    en.setValue(r, Value(std::move(vv)));
                }
                DataChunk out;
                out.columns = std::move(ck->columns);
                out.columns[ci] = std::move(en);
                out.count = n;
                co_yield std::move(out);
            }
        }
        std::string toString() const override { return "LabelRead"; }
        std::vector<const PhysicalOperator*> children() const override { return {ch.get()}; }
    };
    auto ot_copy = std::vector<binder::BoundType>(output_types);
    auto op = std::make_unique<LabelReadOp>(col_idx, store, std::move(output_types), std::move(child_result.op));
    return PlanOperatorResult{std::move(op), std::move(child_result.output_schema), std::move(ot_copy)};
}

/// Inline property loader for non-standalone path. Loads properties into
/// VertexValue in-place (preserves old wrap semantics for BoundPropertyRef).
static PlanOperatorResult
loadVertexPropertiesInPlace(PlanOperatorResult&& child_result, const std::string& variable,
                            const std::unordered_map<LabelId, std::vector<uint16_t>>& label_prop_ids,
                            IAsyncGraphDataStore& store) {
    if (variable.empty() || label_prop_ids.empty())
        return std::move(child_result);
    size_t col_idx = SIZE_MAX;
    for (size_t i = child_result.output_schema.size(); i-- > 0;)
        if (child_result.output_schema[i] == variable) { col_idx = i; break; }
    if (col_idx == SIZE_MAX)
        return std::move(child_result);

    auto ot = std::vector<binder::BoundType>(child_result.output_types);
    ot[col_idx] = binder::BoundType::Vertex();

    struct PropReadOp : PhysicalOperator {
        size_t ci; std::unordered_map<LabelId, std::vector<uint16_t>> lp;
        IAsyncGraphDataStore& s; std::vector<binder::BoundType> ot;
        std::unique_ptr<PhysicalOperator> ch;
        PropReadOp(size_t c, std::unordered_map<LabelId, std::vector<uint16_t>> l,
                   IAsyncGraphDataStore& st, std::vector<binder::BoundType> o,
                   std::unique_ptr<PhysicalOperator> chld)
            : ci(c), lp(std::move(l)), s(st), ot(std::move(o)), ch(std::move(chld)) {}
        folly::coro::AsyncGenerator<RowBatch> execute() override { return executeViaChunk(); }
        folly::coro::AsyncGenerator<DataChunk> executeChunk() override {
            auto g = ch->executeChunk();
            while (auto ck = co_await g.next()) {
                size_t n = ck->numRows();
                auto en = Column::flat(binder::BoundTypeKind::VERTEX, n);
                for (size_t r = 0; r < n; ++r) {
                    auto v = ck->columns[ci].getValue(r);
                    VertexValue vv;
                    if (std::holds_alternative<VertexRef>(v)) vv.id = std::get<VertexRef>(v).id;
                    else if (std::holds_alternative<VertexValue>(v)) vv = std::get<VertexValue>(v);
                    else continue;
                    for (const auto& [lid, pids] : lp) {
                        auto ps = co_await s.getVertexProperties(vv.id, lid, pids);
                        if (ps) vv.properties[lid] = std::move(*ps);
                    }
                    en.setValue(r, Value(std::move(vv)));
                }
                DataChunk out; out.columns = std::move(ck->columns);
                out.columns[ci] = std::move(en); out.count = n;
                co_yield std::move(out);
            }
        }
        std::string toString() const override { return "PropertyRead"; }
        std::vector<const PhysicalOperator*> children() const override { return {ch.get()}; }
    };
    auto ot_copy = std::vector<binder::BoundType>(ot);
    auto op = std::make_unique<PropReadOp>(col_idx, label_prop_ids, store, std::move(ot),
                                            std::move(child_result.op));
    return PlanOperatorResult{std::move(op), std::move(child_result.output_schema), std::move(ot_copy)};
}

static PlanOperatorResult wrapEdgePropertyRead(PlanOperatorResult&& child_result, const std::string& variable,
                                               const std::vector<uint16_t>& edge_prop_ids,
                                               IAsyncGraphDataStore& store) {
    // For the EdgeKey→EdgeValue topology upgrade, always wrap even when no
    // properties were requested.  Operators like DELETE/REMOVE still need a
    // semantic EdgeValue to extract the edge id / label.  The underlying
    // EdgePropertyReadPhysicalOp handles empty edge_prop_ids gracefully.
    if (variable.empty())
        return std::move(child_result);
    size_t col_idx = SIZE_MAX;
    for (size_t i = child_result.output_schema.size(); i-- > 0;) {
        if (child_result.output_schema[i] == variable) {
            col_idx = i;
            break;
        }
    }
    if (col_idx == SIZE_MAX)
        return std::move(child_result);
    // Phase D: upgrade output type from topology (EDGE_KEY) to semantic (EDGE).
    auto epr_output_types = std::vector<binder::BoundType>(child_result.output_types);
    epr_output_types[col_idx] = binder::BoundType::Edge();
    child_result.output_types[col_idx] = binder::BoundType::Edge();
    auto read_op = std::make_unique<EdgePropertyReadPhysicalOp>(
        variable, col_idx, edge_prop_ids, store, Schema(child_result.output_schema), std::move(epr_output_types),
        std::move(child_result.op));
    return PlanOperatorResult{std::move(read_op), std::move(child_result.output_schema),
                              std::move(child_result.output_types)};
}

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
                              std::move(child_result.output_types)};
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

        auto plan_result =
            PlanOperatorResult{std::move(result), std::move(output_schema), std::move(result_output_types)};
        plan_result = loadVertexLabelsInPlace(std::move(plan_result), scan_op.variable, store);
        plan_result = loadVertexPropertiesInPlace(std::move(plan_result), scan_op.variable, scan_op.label_prop_ids, store);
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

        return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(result_output_types)};
    }
    return std::nullopt;
}

// ==================== Bound Plan Pipeline ====================

namespace {
std::unique_ptr<PhysicalOperator> finalizePlanResult(PlanOperatorResult&& result) {
    if (!result.op)
        return nullptr;
    result.op->setOutputSchema(std::move(result.output_schema), std::move(result.output_types));
    return std::move(result.op);
}

PlanOperatorResult extractChildResult(std::variant<PlanOperatorResult, std::string>&& child_result) {
    auto cr = std::move(std::get<PlanOperatorResult>(child_result));
    if (cr.op) {
        cr.op->setOutputSchema(Schema(cr.output_schema), std::vector<binder::BoundType>(cr.output_types));
    }
    return cr;
}

} // namespace

std::variant<std::unique_ptr<PhysicalOperator>, std::string>
PhysicalPlanner::planBound(binder::BoundLogicalPlan& bound_plan, IAsyncGraphDataStore& store,
                           IAsyncGraphMetaStore& meta, PlanContext& ctx) {
    ctx.use_property_extract = canUsePropertyExtract(bound_plan.root, ctx);
    Schema empty_schema;
    std::vector<binder::BoundType> empty_types;
    auto result = planBoundOperator(bound_plan.root, store, meta, ctx, empty_schema, empty_types);
    ctx.use_property_extract = false;
    if (std::holds_alternative<std::string>(result))
        return std::get<std::string>(result);
    return finalizePlanResult(std::move(std::get<PlanOperatorResult>(result)));
}

namespace {

// Recursive helper: walk the materialized BoundLogicalOperator tree and
// merge the Enricher's materialization requirements into the descendant
// scan/expand that produces `var`. This "lowers" the Enricher into the
// RBO-style label_prop_ids / dst_label_prop_ids / edge_prop_ids so the
// existing wrapVertexPropertyRead / wrapEdgePropertyRead in planBoundOperator
// can do the heavy lifting. A proper runtime Enricher operator would make
// this function obsolete (Phase E).
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
    // PropertyExtract/Enricher tags are lowered to label_prop_ids by
    // materializeChosen. The old wrap pipeline (wrapVertexPropertyRead etc.)
    // handles the actual property loading in-place, preserving column layout.
    // Standalone PropertyExtractPhysicalOp operators (flat columnar) will be
    // enabled once per-operator column index mapping is implemented.
    binder::BoundLogicalOperator materialized = materializeChosen(chosen);
    ctx.use_property_extract = canUsePropertyExtract(materialized, ctx);
    Schema empty_schema;
    std::vector<binder::BoundType> empty_types;
    auto result = planBoundOperator(materialized, store, meta, ctx, empty_schema, empty_types);
    ctx.use_property_extract = false;
    if (std::holds_alternative<std::string>(result))
        return std::get<std::string>(result);
    return finalizePlanResult(std::move(std::get<PlanOperatorResult>(result)));
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
                return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types)};
            } else if constexpr (std::is_same_v<T, binder::BoundCorrelatedSourceOp>) {
                Schema output_schema = val.variables;
                std::vector<binder::BoundType> output_types = val.types;
                auto result = std::make_unique<CorrelatedSourcePhysicalOp>(std::vector<std::string>(val.variables),
                                                                           std::vector<binder::BoundType>(val.types));
                return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types)};
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
                auto plan_result =
                    PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types)};
                plan_result = loadVertexLabelsInPlace(std::move(plan_result), val.variable, store);
                plan_result = loadVertexPropertiesInPlace(std::move(plan_result), val.variable, val.label_prop_ids, store);
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
                auto plan_result =
                    PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types)};
                if (ctx.use_property_extract) {
                    // Extract adds decorative label+property columns;
                    // in-place loading preserves old wrap semantics for RETURN n etc.
                    plan_result = loadVertexLabelsInPlace(std::move(plan_result), val.variable, store);
                    plan_result =
                        loadVertexPropertiesInPlace(std::move(plan_result), val.variable, val.label_prop_ids, store);
                    plan_result = dispatchVertexPropertyExtract(std::move(plan_result), val.variable,
                                                                val.label_prop_ids, /*include_labels=*/true, store,
                                                                ctx.label_defs);
                } else {
                    plan_result = loadVertexLabelsInPlace(std::move(plan_result), val.variable, store);
                    plan_result =
                        loadVertexPropertiesInPlace(std::move(plan_result), val.variable, val.label_prop_ids, store);
                }
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
                        std::unordered_map<LabelId, std::vector<uint16_t>>{}, std::vector<uint16_t>{});
                    auto plan_result =
                        PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types)};
                    plan_result = wrapEdgePropertyRead(std::move(plan_result), v.edge_variable, v.edge_prop_ids, store);
                    if (ctx.use_property_extract && !v.edge_prop_ids.empty() && !v.edge_label_ids.empty())
                        plan_result =
                            dispatchEdgePropertyExtract(std::move(plan_result), v.edge_variable, v.edge_prop_ids,
                                                        v.edge_label_ids[0], store, ctx.edge_label_defs);
                    plan_result = loadVertexLabelsInPlace(std::move(plan_result), v.dst_variable, store);
                    plan_result =
                        loadVertexPropertiesInPlace(std::move(plan_result), v.dst_variable, v.dst_label_prop_ids, store);
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
                    auto plan_result =
                        PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types)};
                    // Phase D: VarLenExpand now outputs VertexRef for dst; load labels here.
                    plan_result = loadVertexLabelsInPlace(std::move(plan_result), v.dst_variable, store);
                    plan_result =
                        loadVertexPropertiesInPlace(std::move(plan_result), v.dst_variable, v.dst_label_prop_ids, store);
                    plan_result = wrapPathElementPropertyRead(std::move(plan_result), v.path_variable, store);
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
                                              std::move(cr.output_types)};
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

                    auto result = std::make_unique<ProjectPhysicalOp>(std::move(items), std::move(cr.output_schema),
                                                                      std::move(child_op));
                    result->setEvalContext(ctx.eval_ctx);
                    return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types)};
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
                    return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types)};
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
                                              std::move(cr.output_types)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundSkipOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    auto result = std::make_unique<SkipPhysicalOp>(v.count, std::move(cr.op));
                    return PlanOperatorResult{std::move(result), std::move(cr.output_schema),
                                              std::move(cr.output_types)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundLimitOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    auto result = std::make_unique<LimitPhysicalOp>(v.count, std::move(cr.op));
                    return PlanOperatorResult{std::move(result), std::move(cr.output_schema),
                                              std::move(cr.output_types)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundDistinctOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    auto result = std::make_unique<DistinctPhysicalOp>(std::move(cr.op));
                    return PlanOperatorResult{std::move(result), std::move(cr.output_schema),
                                              std::move(cr.output_types)};
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
                    auto plan_result =
                        PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types)};
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
                    if (v.child) {
                        auto child_result = planBoundOperator(*v.child, store, meta, ctx, input_schema, input_types);
                        if (std::holds_alternative<std::string>(child_result))
                            return std::get<std::string>(child_result);
                        auto cr = extractChildResult(std::move(child_result));
                        child = std::move(cr.op);
                        child_schema = std::move(cr.output_schema);
                        child_types = std::move(cr.output_types);
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
                    return PlanOperatorResult{std::move(result), std::move(node_schema), std::move(output_types)};
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
                    return PlanOperatorResult{std::move(result), std::move(edge_schema), std::move(edge_types)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundSetOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));

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
                                              std::move(cr.output_types)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundRemoveOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));

                    std::vector<RemovePhysicalOp::BoundRemoveItem> items;
                    for (auto& ri : v.items) {
                        RemovePhysicalOp::BoundRemoveItem bri;
                        bri.kind = (ri.kind == binder::BoundRemoveOp::ItemKind::REMOVE_LABEL)
                                       ? RemovePhysicalOp::BoundRemoveItem::Kind::LABEL
                                       : RemovePhysicalOp::BoundRemoveItem::Kind::PROPERTY;
                        bri.var_name = ri.target_variable;
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
                                              std::move(cr.output_types)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundDeleteOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));

                    std::vector<DeletePhysicalOp::DeleteTarget> targets;
                    for (auto& dt : v.targets) {
                        DeletePhysicalOp::DeleteTarget target;
                        target.kind = (dt.kind == binder::BoundDeleteOp::TargetKind::VERTEX)
                                          ? DeletePhysicalOp::TargetKind::VERTEX
                                          : DeletePhysicalOp::TargetKind::EDGE;
                        target.var_name = dt.variable_name;
                        targets.push_back(std::move(target));
                    }

                    auto result = std::make_unique<DeletePhysicalOp>(std::move(targets), v.detach, cr.output_schema,
                                                                     store, std::move(cr.op));
                    return PlanOperatorResult{std::move(result), std::move(cr.output_schema),
                                              std::move(cr.output_types)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundUnwindOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
                    auto child_op = std::move(cr.op);
                    auto child_schema = std::move(cr.output_schema);
                    auto output_types = std::move(cr.output_types);

                    Schema output_schema = child_schema;
                    output_schema.push_back(v.variable);
                    output_types.push_back(binder::BoundType::Any());

                    auto result = std::make_unique<UnwindPhysicalOp>(
                        std::move(v.list_expr), v.variable_column_index, binder::BoundType::Any(),
                        std::move(child_schema), std::vector<binder::BoundType>(output_types), std::move(child_op));
                    result->setEvalContext(ctx.eval_ctx);
                    return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types)};
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
                                                  std::move(output_types)};
                    }

                    return PlanOperatorResult{std::move(result), std::move(lr.output_schema), std::move(output_types)};
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

                    auto result = std::make_unique<CrossProductPhysicalOp>(
                        std::move(lr.op), std::move(rr.op), std::move(lr.output_schema), std::move(rr.output_schema),
                        std::vector<binder::BoundType>(output_types));
                    result->setEvalContext(ctx.eval_ctx);
                    return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types)};
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
                                              std::move(lr.output_types)};
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

                    auto result =
                        std::make_unique<LeftJoinPhysicalOp>(std::move(lr.op), std::move(rr.op), correlated,
                                                             std::move(left_corr_cols), std::move(rr.output_types));
                    return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types)};
                } else if constexpr (std::is_same_v<Elem, binder::BoundMergeOp>) {
                    auto child_result = planBoundOperator(v.child, store, meta, ctx, input_schema, input_types);
                    if (std::holds_alternative<std::string>(child_result))
                        return std::get<std::string>(child_result);
                    auto cr = extractChildResult(std::move(child_result));
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
                        if (v.path_variable) {
                            output_schema.push_back(*v.path_variable);
                            output_types.push_back(binder::BoundType::Path());
                        }
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
                    return PlanOperatorResult{std::move(result), std::move(output_schema), std::move(output_types)};
                } else {
                    return std::string("Unknown bound logical operator type");
                }
            }
        },
        op);
}

} // namespace compute
} // namespace eugraph
