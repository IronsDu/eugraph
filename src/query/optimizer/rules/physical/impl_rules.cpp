#include "query/optimizer/rules/physical/impl_rules.hpp"

#include "query/optimizer/analysis/index_access_path.hpp"
#include "query/optimizer/framework/memo.hpp"
#include "query/optimizer/framework/physical_expr.hpp"
#include "query/planner/logical_plan/operator/bound_index_join_op.hpp"

#include <unordered_set>

namespace eugraph {
namespace optimizer {

// ============================================================
// Helper: create a physical GroupExpr from a logical source.
//
// Phase C: each non-leaf physical operator declares its per-input
// required materializations (required_input_mat[i]) from the source
// group's own requirements — i.e. the property accesses in THIS
// operator's expressions (Filter predicate, Project items, etc.).
// This is what feeds the Enricher enforcer mechanism: when a parent
// demands materialization that a child's topology-form winner cannot
// provide, O_INPUTS inserts an Enricher to bridge the gap.
// ============================================================
static std::vector<std::unique_ptr<GroupExpr>> makePhysicalExpr(GroupExpr& expr, Memo& memo, PhysicalOpTag tag) {
    auto phys = std::make_unique<PhysicalExpr>();
    phys->tag = tag;
    phys->source = cloneBoundLogicalOperator(expr.op);

    // Each input must provide what this operator's own expressions need.
    // Leaf operators (arity 0) get no entries.
    Group& group = memo.getGroup(expr.group_id);
    if (group.requirementsValid()) {
        const VarRequirements& reqs = group.requirements();
        for (size_t i = 0; i < expr.child_groups.size(); ++i) {
            phys->required_input_mat.push_back(reqs);
        }
    }

    auto ge = std::make_unique<GroupExpr>(INVALID_EXPR_ID, expr.group_id, std::move(phys), expr.child_groups);
    std::vector<std::unique_ptr<GroupExpr>> result;
    result.push_back(std::move(ge));
    return result;
}

// ============================================================
// Leaf sources
// ============================================================

PatternNode GenerateIndexScanRule::pattern() const { return {OptNodeType::Filter, {{}}}; }

std::vector<std::unique_ptr<GroupExpr>> GenerateIndexScanRule::substitute(GroupExpr& expr, Memo& memo) const {
    auto* fp = std::get_if<std::unique_ptr<binder::BoundFilterOp>>(&expr.op);
    if (!fp || !*fp || expr.child_groups.empty() || memo.getCatalog() == nullptr)
        return {};
    Group& child = memo.getGroup(expr.child_groups[0]);
    if (child.logical_exprs.size() != 1)
        return {};
    const auto* scan = std::get_if<binder::BoundLabelScanOp>(&memo.getExpr(child.logical_exprs[0]).op);
    if (!scan || scan->label_ids.size() != 1 || scan->index_scan || scan->index_scan_values)
        return {};

    // Reuse the normalization pass on a childless Filter(LabelScan) clone; it
    // returns a LabelScan with the descriptor when every predicate is consumed.
    binder::BoundFilterOp temp;
    temp.predicate = cloneBoundExpression((*fp)->predicate);
    temp.uniqueness_scope = (*fp)->uniqueness_scope;
    temp.child = *scan;
    binder::BoundLogicalOperator temp_op = std::make_unique<binder::BoundFilterOp>(std::move(temp));
    optimizer::applyIndexAccessPaths(temp_op, *memo.getCatalog(), memo.getSlotSemantics());
    auto* chosen = std::get_if<binder::BoundLabelScanOp>(&temp_op);
    if (!chosen || !chosen->index_scan)
        return {};

    std::vector<std::unique_ptr<GroupExpr>> result;
    result.push_back(std::make_unique<GroupExpr>(INVALID_EXPR_ID, expr.group_id, std::move(*chosen),
                                                 std::vector<GroupId>{}));
    return result;
}

PatternNode ImplIndexScanRule::pattern() const { return {OptNodeType::LabelScan, {}}; }
bool ImplIndexScanRule::condition(GroupExpr& expr, Memo&) const {
    const auto* scan = std::get_if<binder::BoundLabelScanOp>(&expr.op);
    return scan && scan->index_scan && scan->index_id != 0;
}
std::vector<std::unique_ptr<GroupExpr>> ImplIndexScanRule::substitute(GroupExpr& expr, Memo& memo) const {
    return makePhysicalExpr(expr, memo, PhysicalOpTag::IndexScan);
}

PatternNode ImplSingletonRule::pattern() const {
    return {OptNodeType::Singleton, {}};
}
std::vector<std::unique_ptr<GroupExpr>> ImplSingletonRule::substitute(GroupExpr& expr, Memo& memo) const {
    return makePhysicalExpr(expr, memo, PhysicalOpTag::Singleton);
}

PatternNode ImplCorrelatedSourceRule::pattern() const {
    return {OptNodeType::CorrelatedSource, {}};
}
std::vector<std::unique_ptr<GroupExpr>> ImplCorrelatedSourceRule::substitute(GroupExpr& expr, Memo& memo) const {
    return makePhysicalExpr(expr, memo, PhysicalOpTag::CorrelatedSource);
}

PatternNode ImplScanRule::pattern() const {
    return {OptNodeType::Scan, {}};
}
std::vector<std::unique_ptr<GroupExpr>> ImplScanRule::substitute(GroupExpr& expr, Memo& memo) const {
    return makePhysicalExpr(expr, memo, PhysicalOpTag::NodeScan);
}

PatternNode ImplLabelScanRule::pattern() const {
    return {OptNodeType::LabelScan, {}};
}
std::vector<std::unique_ptr<GroupExpr>> ImplLabelScanRule::substitute(GroupExpr& expr, Memo& memo) const {
    return makePhysicalExpr(expr, memo, PhysicalOpTag::LabelScan);
}

// ============================================================
// Unary operators (pattern: one leaf child)
// ============================================================

static bool expressionHasIndexedProperty(const binder::BoundExpression& expr, const catalog::Catalog* catalog) {
    if (catalog == nullptr)
        return false;
    if (auto* prop = std::get_if<std::unique_ptr<binder::BoundPropertyRef>>(&expr); prop && *prop) {
        // The property ref may be unlabeled/coalesced; trust any index on this
        // property name. The physical lowering still verifies the exact target
        // label/index and falls back when the chain cannot use it.
        for (const auto& [lid, label_def] : catalog->allLabels()) {
            (void)lid;
            for (const auto& idx : label_def.indexes) {
                if (idx.index_id == 0)
                    continue;
                for (const auto& accessor : idx.accessors) {
                    if (accessor.property_name == (*prop)->property_name)
                        return true;
                }
            }
        }
        return expressionHasIndexedProperty((*prop)->object, catalog);
    }
    if (auto* binop = std::get_if<std::unique_ptr<binder::BoundBinaryOp>>(&expr); binop && *binop)
        return expressionHasIndexedProperty((*binop)->left, catalog) ||
               expressionHasIndexedProperty((*binop)->right, catalog);
    return false;
}

static bool groupContainsExpand(Memo& memo, GroupId gid, std::unordered_set<GroupId>& seen) {
    if (!seen.insert(gid).second)
        return false;
    const auto& group = memo.getGroup(gid);
    for (ExprId eid : group.logical_exprs) {
        const auto& ge = memo.getExpr(eid);
        if (std::holds_alternative<std::unique_ptr<binder::BoundExpandOp>>(ge.op))
            return true;
        for (GroupId child : ge.child_groups) {
            if (groupContainsExpand(memo, child, seen))
                return true;
        }
    }
    return false;
}

static bool collectAndDbg(const binder::BoundExpression& e, std::vector<const binder::BoundExpression*>& out) {
    if (auto* b = std::get_if<std::unique_ptr<binder::BoundBinaryOp>>(&e);
        b && *b && (*b)->op == cypher::BinaryOperator::AND) {
        collectAndDbg((*b)->left, out);
        collectAndDbg((*b)->right, out);
        return true;
    }
    out.push_back(&e);
    return true;
}
static bool toPV(const Value& v, PropertyValue& out) {
    if (auto* i = std::get_if<int64_t>(&v)) {
        out = *i;
        return true;
    }
    if (auto* s = std::get_if<std::string>(&v)) {
        out = *s;
        return true;
    }
    if (auto* d = std::get_if<double>(&v)) {
        out = *d;
        return true;
    }
    return false;
}
static bool chainDbg(Memo& memo, GroupId gid, std::vector<binder::BoundIndexJoinOp::EdgeStep>& chain, GroupId& source,
                     std::unordered_set<GroupId>& seen) {
    if (!seen.insert(gid).second)
        return false;
    Group& g = memo.getGroup(gid);
    for (ExprId eid : g.logical_exprs) {
        GroupExpr& ge = memo.getExpr(eid);
        if (auto* e = std::get_if<std::unique_ptr<binder::BoundExpandOp>>(&ge.op); e && *e) {
            binder::BoundIndexJoinOp::EdgeStep st;
            st.src_variable = (*e)->src_variable;
            st.dst_variable = (*e)->dst_variable;
            st.edge_variable = (*e)->edge_variable;
            st.edge_label_ids = (*e)->edge_label_ids;
            st.direction = (*e)->direction;
            st.dst_label_ids = (*e)->dst_label_ids;
            chain.push_back(std::move(st));
            if (!ge.child_groups.empty() && chainDbg(memo, ge.child_groups[0], chain, source, seen))
                return true;
            chain.pop_back();
        } else if (std::holds_alternative<std::unique_ptr<binder::BoundProjectOp>>(ge.op) ||
                   std::holds_alternative<std::unique_ptr<binder::BoundFilterOp>>(ge.op) ||
                   std::holds_alternative<std::unique_ptr<binder::BoundDistinctOp>>(ge.op)) {
            if (!ge.child_groups.empty() && chainDbg(memo, ge.child_groups[0], chain, source, seen))
                return true;
        }
    }
    if (!chain.empty()) {
        source = gid;
        return true;
    }
    return false;
}
PatternNode GenerateIndexJoinRule::pattern() const {
    return {OptNodeType::Filter, {{}}};
}
std::vector<std::unique_ptr<GroupExpr>> GenerateIndexJoinRule::substitute(GroupExpr& expr, Memo& memo) const {
    auto* fp = std::get_if<std::unique_ptr<binder::BoundFilterOp>>(&expr.op);
    if (!fp || !*fp || expr.child_groups.empty() || memo.getCatalog() == nullptr)
        return {};
    std::vector<binder::BoundIndexJoinOp::EdgeStep> chain;
    GroupId source = INVALID_GROUP_ID;
    std::unordered_set<GroupId> seen;
    if (!chainDbg(memo, expr.child_groups[0], chain, source, seen) || chain.empty()) {
        return {};
    }
    const auto& outer = chain.front();
    std::vector<const binder::BoundExpression*> conjs;
    collectAndDbg((*fp)->predicate, conjs);
    if (conjs.size() != 1) {
        return {};
    }
    auto* bp = std::get_if<std::unique_ptr<binder::BoundBinaryOp>>(conjs[0]);
    if (!bp || !*bp)
        return {};
    auto& b = **bp;
    std::string prop;
    LabelId label = INVALID_LABEL_ID;
    uint16_t pid = 0;
    bool left = false, right = false;
    auto inspect = [&](const binder::BoundExpression& e) -> bool {
        auto* p = std::get_if<std::unique_ptr<binder::BoundPropertyRef>>(&e);
        if (!p || !*p)
            return false;
        std::string var;
        if (auto* c = std::get_if<binder::BoundColumnRef>(&(*p)->object))
            var = c->name;
        else if (auto* v = std::get_if<binder::BoundVariableRef>(&(*p)->object))
            var = v->name;
        if (var != outer.dst_variable)
            return false;
        prop = (*p)->property_name;
        if ((*p)->candidates.empty())
            return false;
        label = (*p)->candidates.front().label_id;
        pid = (*p)->candidates.front().prop_id;
        for (const auto& c : (*p)->candidates) {
            if (std::find(outer.dst_label_ids.begin(), outer.dst_label_ids.end(), c.label_id) !=
                outer.dst_label_ids.end()) {
                label = c.label_id;
                pid = c.prop_id;
                break;
            }
        }
        return true;
    };
    auto inspect_col = [&](const binder::BoundExpression& e) -> bool {
        auto* c = std::get_if<binder::BoundColumnRef>(&e);
        if (!c || memo.getSlotSemantics() == nullptr)
            return false;
        auto it = memo.getSlotSemantics()->find(c->slot_id);
        if (it == memo.getSlotSemantics()->end() || it->second.kind != binder::SlotSemanticKind::Property)
            return false;
        prop = it->second.property_name;
        label = it->second.property_labels.empty() ? INVALID_LABEL_ID : it->second.property_labels.front();
        pid = it->second.property_ids.empty() ? 0 : it->second.property_ids.front();
        return true;
    };
    left = inspect(b.left) || inspect_col(b.left);
    if (!left)
        right = inspect(b.right) || inspect_col(b.right);
    if (!left && !right) {
        return {};
    }
    auto op = b.op;
    const binder::BoundExpression* other = left ? &b.right : &b.left;
    if (right) {
        switch (op) {
        case cypher::BinaryOperator::LT:
            op = cypher::BinaryOperator::GT;
            break;
        case cypher::BinaryOperator::LTE:
            op = cypher::BinaryOperator::GTE;
            break;
        case cypher::BinaryOperator::GT:
            op = cypher::BinaryOperator::LT;
            break;
        case cypher::BinaryOperator::GTE:
            op = cypher::BinaryOperator::LTE;
            break;
        default:
            break;
        }
    }
    auto out = std::make_unique<binder::BoundIndexJoinOp>();
    out->source_variable = chain.back().src_variable;
    out->target_variable = outer.dst_variable;
    out->target_label = label;
    out->index_property_name = prop;
    out->index_prop = pid;
    out->chain = chain;
    if (auto* lit = std::get_if<binder::BoundLiteral>(other); lit) {
        PropertyValue pv;
        if (!toPV(lit->value, pv))
            return {};
        switch (op) {
        case cypher::BinaryOperator::EQ:
            out->equality = pv;
            break;
        case cypher::BinaryOperator::GT:
            out->lower = pv;
            out->lower_inclusive = false;
            break;
        case cypher::BinaryOperator::GTE:
            out->lower = pv;
            out->lower_inclusive = true;
            break;
        case cypher::BinaryOperator::LT:
            out->upper = pv;
            out->upper_inclusive = false;
            break;
        case cypher::BinaryOperator::LTE:
            out->upper = pv;
            out->upper_inclusive = true;
            break;
        default:
            return {};
        }
    } else if (auto* col = std::get_if<binder::BoundColumnRef>(other); col && op == cypher::BinaryOperator::EQ) {
        out->dynamic_key = true;
        out->dynamic_key_variable = col->name;
    } else
        return {};
    uint32_t idx = 0;
    auto find_idx = [&](LabelId lid) {
        auto it = memo.getCatalog()->allLabels().find(lid);
        if (it == memo.getCatalog()->allLabels().end())
            return;
        for (const auto& i : it->second.indexes)
            for (const auto& a : i.accessors)
                if (a.property_name == prop && i.index_id) {
                    idx = i.index_id;
                    label = lid;
                    return;
                }
    };
    find_idx(label);
    if (!idx)
        for (const auto& [lid, _] : memo.getCatalog()->allLabels()) {
            find_idx(lid);
            if (idx)
                break;
        }
    if (!idx)
        return {};
    // A filtered source is a point/range probe already: re-executing it per
    // reverse-index batch is strictly worse than the forward path. Bare
    // scans remain candidates (tests cover the reverse range path).
    // A bare label scan with a large source cardinality is not an anchor; the
    // reverse index would be re-probed once per source row. Keep the candidate
    // for genuinely small anchors (tests cover that path).
    bool source_is_scan = false;
    for (ExprId eid : memo.getGroup(source).logical_exprs) {
        const auto& o = memo.getExpr(eid).op;
        if (std::holds_alternative<binder::BoundScanOp>(o) || std::holds_alternative<binder::BoundLabelScanOp>(o)) {
            source_is_scan = true;
            break;
        }
    }
    double source_card = memo.getGroup(source).getLogProp(memo, memo.getCatalog()).cardinality;
    if (!out->dynamic_key && source_is_scan && source_card > 1000.0)
        return {};
    out->index_id = idx;
    // Build the (small) anchor side: the reverse index stream can be large
    // (e.g. a date range) and must not become the hash-build input.
    out->build_left = true;
    std::vector<std::unique_ptr<GroupExpr>> result;
    result.push_back(
        std::make_unique<GroupExpr>(INVALID_EXPR_ID, expr.group_id, std::move(out), std::vector<GroupId>{source}));
    return result;
}
PatternNode ImplPatternComprehensionApplyRule::pattern() const {
    return {OptNodeType::PatternComprehensionApply, {{}, {}}};
}
std::vector<std::unique_ptr<GroupExpr>> ImplPatternComprehensionApplyRule::substitute(GroupExpr& expr,
                                                                                      Memo& memo) const {
    return makePhysicalExpr(expr, memo, PhysicalOpTag::PatternComprehensionApply);
}

PatternNode ImplIndexJoinRule::pattern() const {
    return {OptNodeType::IndexJoin, {{}}};
}

bool ImplIndexJoinRule::condition(GroupExpr& /*expr*/, Memo& /*memo*/) const {
    return true;
}

std::vector<std::unique_ptr<GroupExpr>> ImplIndexJoinRule::substitute(GroupExpr& expr, Memo& memo) const {
    return makePhysicalExpr(expr, memo, PhysicalOpTag::IndexJoin);
}

PatternNode ImplFilterRule::pattern() const {
    return {OptNodeType::Filter, {{}}};
}
std::vector<std::unique_ptr<GroupExpr>> ImplFilterRule::substitute(GroupExpr& expr, Memo& memo) const {
    return makePhysicalExpr(expr, memo, PhysicalOpTag::Filter);
}

PatternNode ImplProjectRule::pattern() const {
    return {OptNodeType::Project, {{}}};
}
std::vector<std::unique_ptr<GroupExpr>> ImplProjectRule::substitute(GroupExpr& expr, Memo& memo) const {
    return makePhysicalExpr(expr, memo, PhysicalOpTag::Project);
}

PatternNode ImplExpandRule::pattern() const {
    return {OptNodeType::Expand, {{}}};
}
std::vector<std::unique_ptr<GroupExpr>> ImplExpandRule::substitute(GroupExpr& expr, Memo& memo) const {
    return makePhysicalExpr(expr, memo, PhysicalOpTag::Expand);
}

PatternNode ImplVarLenExpandRule::pattern() const {
    return {OptNodeType::VarLenExpand, {{}}};
}
std::vector<std::unique_ptr<GroupExpr>> ImplVarLenExpandRule::substitute(GroupExpr& expr, Memo& memo) const {
    return makePhysicalExpr(expr, memo, PhysicalOpTag::VarLenExpand);
}

PatternNode ImplSortRule::pattern() const {
    return {OptNodeType::Sort, {{}}};
}
std::vector<std::unique_ptr<GroupExpr>> ImplSortRule::substitute(GroupExpr& expr, Memo& memo) const {
    return makePhysicalExpr(expr, memo, PhysicalOpTag::Sort);
}

PatternNode ImplLimitRule::pattern() const {
    return {OptNodeType::Limit, {{}}};
}
std::vector<std::unique_ptr<GroupExpr>> ImplLimitRule::substitute(GroupExpr& expr, Memo& memo) const {
    return makePhysicalExpr(expr, memo, PhysicalOpTag::Limit);
}

PatternNode ImplSkipRule::pattern() const {
    return {OptNodeType::Skip, {{}}};
}
std::vector<std::unique_ptr<GroupExpr>> ImplSkipRule::substitute(GroupExpr& expr, Memo& memo) const {
    return makePhysicalExpr(expr, memo, PhysicalOpTag::Skip);
}

PatternNode ImplDistinctRule::pattern() const {
    return {OptNodeType::Distinct, {{}}};
}
std::vector<std::unique_ptr<GroupExpr>> ImplDistinctRule::substitute(GroupExpr& expr, Memo& memo) const {
    return makePhysicalExpr(expr, memo, PhysicalOpTag::Distinct);
}

PatternNode ImplAggregateRule::pattern() const {
    return {OptNodeType::Aggregate, {{}}};
}
std::vector<std::unique_ptr<GroupExpr>> ImplAggregateRule::substitute(GroupExpr& expr, Memo& memo) const {
    return makePhysicalExpr(expr, memo, PhysicalOpTag::Aggregate);
}

PatternNode ImplPathCountRule::pattern() const {
    return {OptNodeType::PathCount, {{}}};
}
std::vector<std::unique_ptr<GroupExpr>> ImplPathCountRule::substitute(GroupExpr& expr, Memo& memo) const {
    return makePhysicalExpr(expr, memo, PhysicalOpTag::PathCount);
}

PatternNode ImplPathBuildRule::pattern() const {
    return {OptNodeType::PathBuild, {{}}};
}
std::vector<std::unique_ptr<GroupExpr>> ImplPathBuildRule::substitute(GroupExpr& expr, Memo& memo) const {
    return makePhysicalExpr(expr, memo, PhysicalOpTag::PathBuild);
}

PatternNode ImplUnwindRule::pattern() const {
    return {OptNodeType::Unwind, {{}}};
}
std::vector<std::unique_ptr<GroupExpr>> ImplUnwindRule::substitute(GroupExpr& expr, Memo& memo) const {
    return makePhysicalExpr(expr, memo, PhysicalOpTag::Unwind);
}

// ============================================================
// Binary operators (pattern: two leaf children)
// ============================================================

PatternNode ImplBinaryJoinRule::pattern() const {
    return {OptNodeType::BinaryJoin, {{}, {}}};
}
std::vector<std::unique_ptr<GroupExpr>> ImplBinaryJoinRule::substitute(GroupExpr& expr, Memo& memo) const {
    return makePhysicalExpr(expr, memo, PhysicalOpTag::CrossProduct);
}

PatternNode ImplHashJoinRule::pattern() const {
    return {OptNodeType::BinaryJoin, {{}, {}}};
}

std::vector<std::unique_ptr<GroupExpr>> ImplHashJoinRule::substitute(GroupExpr& expr, Memo& memo) const {
    auto* join = std::get_if<std::unique_ptr<binder::BoundBinaryJoinOp>>(&expr.op);
    if (!join || !*join || (*join)->join_conditions.empty())
        return {};
    return makePhysicalExpr(expr, memo, PhysicalOpTag::HashJoin);
}

PatternNode ImplLeftJoinRule::pattern() const {
    return {OptNodeType::LeftJoin, {{}, {}}};
}
std::vector<std::unique_ptr<GroupExpr>> ImplLeftJoinRule::substitute(GroupExpr& expr, Memo& memo) const {
    return makePhysicalExpr(expr, memo, PhysicalOpTag::LeftJoin);
}

PatternNode ImplSemiJoinRule::pattern() const {
    return {OptNodeType::SemiJoin, {{}, {}}};
}
std::vector<std::unique_ptr<GroupExpr>> ImplSemiJoinRule::substitute(GroupExpr& expr, Memo& memo) const {
    return makePhysicalExpr(expr, memo, PhysicalOpTag::SemiJoin);
}

PatternNode ImplUnionRule::pattern() const {
    return {OptNodeType::Union, {{}, {}}};
}
std::vector<std::unique_ptr<GroupExpr>> ImplUnionRule::substitute(GroupExpr& expr, Memo& memo) const {
    return makePhysicalExpr(expr, memo, PhysicalOpTag::Union);
}

} // namespace optimizer
} // namespace eugraph
