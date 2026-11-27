#include "query/optimizer/rules/logical/generate_index_join.hpp"

#include "query/optimizer/framework/memo.hpp"
#include "query/planner/bound_logical_plan.hpp"

#include <algorithm>
#include <functional>
#include <unordered_set>

namespace eugraph {
namespace optimizer {

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
    // This rule is the *dynamic-key* reverse-chain candidate only. Static
    // destination probes (single or multi step) are not priced reliably without
    // index distribution statistics: the reverse path scans a global index range
    // once, the forward path expands per anchor row, and the two estimates are
    // dominated by missing stats. Without a principled comparison the generic
    // forward plan is the safe choice (and the single-step shape is owned by the
    // IndexProbe family). Dynamic keys are different: the path is driven by the
    // source rows themselves (IndexScanValues per key), which is exactly what no
    // other rule can express. See physical-planner-residue-audit J8.
    if (!out->dynamic_key)
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
    out->index_id = idx;
    // Build the (small) anchor side: the reverse index stream can be large
    // (e.g. a date range) and must not become the hash-build input.
    out->build_left = true;
    std::vector<std::unique_ptr<GroupExpr>> result;
    result.push_back(
        std::make_unique<GroupExpr>(INVALID_EXPR_ID, expr.group_id, std::move(out), std::vector<GroupId>{source}));
    return result;
}

} // namespace optimizer
} // namespace eugraph
