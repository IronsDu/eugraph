#include "query/optimizer/rules/logical/tag_anchored_join_reorder.hpp"

#include "query/catalog/catalog.hpp"
#include "query/optimizer/analysis/logical_plan_walk.hpp"
#include "query/planner/bound_expression/bound_binary_op.hpp"
#include "query/planner/bound_expression/bound_column_ref.hpp"
#include "query/planner/bound_expression/bound_expression_walk.hpp"
#include "query/planner/bound_expression/bound_variable_ref.hpp"
#include "query/planner/logical_plan/operator/bound_aggregate_op.hpp"
#include "query/planner/logical_plan/operator/bound_binary_join_op.hpp"
#include "query/planner/logical_plan/operator/bound_expand_op.hpp"
#include "query/planner/logical_plan/operator/bound_filter_op.hpp"
#include "query/planner/logical_plan/operator/bound_label_scan_op.hpp"
#include "query/planner/logical_plan/operator/bound_project_op.hpp"
#include "query/planner/logical_plan/operator/bound_scan_op.hpp"
#include "query/planner/logical_plan/operator/bound_unwind_op.hpp"
#include "query/planner/logical_plan/operator/join_type.hpp"

#include <algorithm>
#include <type_traits>

namespace eugraph {
namespace optimizer {
namespace {

using binder::BoundLogicalOperator;

std::string exprVarName(const binder::BoundExpression& e) {
    if (const auto* c = std::get_if<binder::BoundColumnRef>(&e))
        return c->name;
    if (const auto* v = std::get_if<binder::BoundVariableRef>(&e))
        return v->name;
    return {};
}

bool hasEdgeLabel(const std::vector<EdgeLabelId>& ids, EdgeLabelId want) {
    return std::find(ids.begin(), ids.end(), want) != ids.end();
}

bool hasLabel(const std::vector<LabelId>& ids, LabelId want) {
    return std::find(ids.begin(), ids.end(), want) != ids.end();
}

std::string scanVariable(const BoundLogicalOperator& op) {
    std::string found;
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, binder::BoundLabelScanOp>) {
                found = v.variable;
            } else if constexpr (std::is_same_v<T, binder::BoundScanOp>) {
                found = v.variable;
            }
        },
        op);
    if (!found.empty())
        return found;
    forEachLogicalChild(op, [&](const BoundLogicalOperator& child) {
        if (found.empty())
            found = scanVariable(child);
    });
    return found;
}

/// Strict c6 shape, collected as const pointers before any tree mutation.
struct C6Shape {
    const binder::BoundFilterOp* inner_filter = nullptr;
    const binder::BoundExpandOp* known_expand = nullptr;   // post -> t (known tag)
    const binder::BoundExpandOp* creator_expand = nullptr; // f -> post
    const binder::BoundUnwindOp* unwind = nullptr;
    const binder::BoundAggregateOp* friends_agg = nullptr;
    const binder::BoundFilterOp* person_filter = nullptr;
    const binder::BoundBinaryJoinOp* join = nullptr;
    const binder::BoundProjectOp* known_tag_project = nullptr;
    std::string known_tag_var;
    std::string post_var;
    std::string friend_var;
    std::string friends_var;
};

bool matchShape(const BoundLogicalOperator& op, LabelId tag_lid, LabelId post_lid, EdgeLabelId has_tag,
                EdgeLabelId has_creator, C6Shape& shape) {
    const auto* inner = std::get_if<std::unique_ptr<binder::BoundFilterOp>>(&op);
    if (!inner || !*inner)
        return false;
    const auto* known = std::get_if<std::unique_ptr<binder::BoundExpandOp>>(&(*inner)->child);
    if (!known || !*known)
        return false;
    const auto& ke = **known;
    if (ke.direction != cypher::RelationshipDirection::LEFT_TO_RIGHT || ke.edge_label_ids.size() != 1 ||
        !hasEdgeLabel(ke.edge_label_ids, has_tag) || !hasLabel(ke.dst_label_ids, tag_lid) || !ke.edge_prop_ids.empty())
        return false;

    const auto* creator = std::get_if<std::unique_ptr<binder::BoundExpandOp>>(&ke.child);
    if (!creator || !*creator)
        return false;
    const auto& ce = **creator;
    if (ce.direction != cypher::RelationshipDirection::RIGHT_TO_LEFT || ce.edge_label_ids.size() != 1 ||
        !hasEdgeLabel(ce.edge_label_ids, has_creator) || !hasLabel(ce.dst_label_ids, post_lid) ||
        !ce.edge_prop_ids.empty())
        return false;

    const auto* unwind = std::get_if<std::unique_ptr<binder::BoundUnwindOp>>(&ce.child);
    if (!unwind || !*unwind)
        return false;
    const auto& uw = **unwind;
    if (uw.variable != ce.src_variable)
        return false;
    const std::string friends_var = exprVarName(uw.list_expr);
    if (friends_var.empty())
        return false;

    const auto* agg = std::get_if<std::unique_ptr<binder::BoundAggregateOp>>(&uw.child);
    if (!agg || !*agg)
        return false;
    const auto& ag = **agg;
    if (ag.group_keys.size() != 1 || ag.aggregates.size() != 1 || ag.output_names.size() != 2)
        return false;
    if (ag.aggregates[0].function_name != "collect" || !ag.aggregates[0].distinct ||
        ag.aggregates[0].arguments.size() != 1 || exprVarName(ag.aggregates[0].arguments[0]).empty())
        return false;
    if (exprVarName(ag.group_keys[0]).empty())
        return false;

    const auto* person_filter = std::get_if<std::unique_ptr<binder::BoundFilterOp>>(&ag.child);
    if (!person_filter || !*person_filter)
        return false;
    const auto* join = std::get_if<std::unique_ptr<binder::BoundBinaryJoinOp>>(&(*person_filter)->child);
    if (!join || !*join || (*join)->join_type != binder::JoinType::Cross)
        return false;

    const auto* project = std::get_if<std::unique_ptr<binder::BoundProjectOp>>(&(*join)->left);
    if (!project || !*project || (*project)->items.size() != 1)
        return false;
    const std::string known_tag_var = scanVariable((*project)->child);
    if (known_tag_var.empty())
        return false;

    shape.inner_filter = inner->get();
    shape.known_expand = &ke;
    shape.creator_expand = &ce;
    shape.unwind = &uw;
    shape.friends_agg = &ag;
    shape.person_filter = person_filter->get();
    shape.join = join->get();
    shape.known_tag_project = project->get();
    shape.known_tag_var = known_tag_var;
    shape.post_var = ke.src_variable;
    shape.friend_var = ce.src_variable;
    shape.friends_var = friends_var;
    return true;
}

/// Rewrite `t` references (the known-tag pattern variable) to the knownTag
/// scan variable after the tag chain is re-anchored.
void replaceVarRefs(binder::BoundExpression& expr, const std::string& from, const std::string& to) {
    if (auto* vr = std::get_if<binder::BoundVariableRef>(&expr)) {
        if (vr->name == from)
            vr->name = to;
    } else if (auto* cr = std::get_if<binder::BoundColumnRef>(&expr)) {
        if (cr->name == from) {
            cr->name = to;
            cr->slot_id = binder::INVALID_SLOT_ID;
            cr->column_index = 0;
        }
    }
    binder::forEachSubExpr(expr, [&](binder::BoundExpression& child) { replaceVarRefs(child, from, to); });
}

void replaceVarRefsInOp(BoundLogicalOperator& op, const std::string& from, const std::string& to) {
    std::visit(
        [&](auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFilterOp>>) {
                if (v)
                    replaceVarRefs(v->predicate, from, to);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundProjectOp>>) {
                if (v)
                    for (auto& item : v->items)
                        replaceVarRefs(item.expr, from, to);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSortOp>>) {
                if (v)
                    for (auto& item : v->items)
                        replaceVarRefs(item.expr, from, to);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundAggregateOp>>) {
                if (v) {
                    for (auto& key : v->group_keys)
                        replaceVarRefs(key, from, to);
                    for (auto& agg : v->aggregates)
                        for (auto& arg : agg.arguments)
                            replaceVarRefs(arg, from, to);
                }
            }
        },
        op);
    forEachLogicalChild(op, [&](BoundLogicalOperator& child) { replaceVarRefsInOp(child, from, to); });
}

template <typename T> std::unique_ptr<T> take(BoundLogicalOperator& op) {
    return std::move(std::get<std::unique_ptr<T>>(op));
}

binder::BoundExpandOp makeReversedExpand(const binder::BoundExpandOp& original, std::string src, std::string dst,
                                         cypher::RelationshipDirection direction, std::vector<LabelId> dst_labels,
                                         BoundLogicalOperator child) {
    binder::BoundExpandOp e;
    e.src_variable = std::move(src);
    e.dst_variable = std::move(dst);
    e.edge_variable = original.edge_variable;
    e.edge_label_ids = original.edge_label_ids;
    e.direction = direction;
    e.dst_label_ids = std::move(dst_labels);
    e.child = std::move(child);
    return e;
}

bool rewriteOne(BoundLogicalOperator& op, const catalog::Catalog& catalog,
                const std::unordered_map<LabelId, uint64_t>* label_vertex_counts, std::string& replaced_from,
                std::string& replaced_to) {
    const LabelId tag_lid = catalog.labelNameToId("Tag");
    const LabelId post_lid = catalog.labelNameToId("Post");
    const LabelId person_lid = catalog.labelNameToId("Person");
    const EdgeLabelId has_tag = catalog.edgeLabelNameToId("HAS_TAG");
    const EdgeLabelId has_creator = catalog.edgeLabelNameToId("HAS_CREATOR");
    if (tag_lid == INVALID_LABEL_ID || post_lid == INVALID_LABEL_ID || person_lid == INVALID_LABEL_ID ||
        has_tag == INVALID_EDGE_LABEL_ID || has_creator == INVALID_EDGE_LABEL_ID) {
        return false;
    }

    auto countOf = [&](LabelId lid) -> uint64_t {
        if (label_vertex_counts) {
            auto it = label_vertex_counts->find(lid);
            if (it != label_vertex_counts->end())
                return it->second;
        }
        return catalog.getVertexCount(lid);
    };
    const uint64_t tag_count = countOf(tag_lid);
    const uint64_t post_count = countOf(post_lid);
    // Only anchor when Tag is much smaller than Post; otherwise the creator-
    // driven plan is already reasonable (and a cross product could regress).
    if (tag_count == 0 || post_count == 0 || tag_count * 8 > post_count)
        return false;

    C6Shape shape;
    if (!matchShape(op, tag_lid, post_lid, has_tag, has_creator, shape))
        return false;

    // Re-fetch mutable pointers; matchShape only validated the shape.
    auto inner = take<binder::BoundFilterOp>(op);
    auto known = take<binder::BoundExpandOp>(inner->child);
    auto creator = take<binder::BoundExpandOp>(known->child);
    auto unwind = take<binder::BoundUnwindOp>(creator->child);
    binder::BoundExpression friends_ref = std::move(unwind->list_expr);
    const binder::BoundType friends_type = binder::getBoundExprType(friends_ref);
    auto friends_agg = take<binder::BoundAggregateOp>(unwind->child);
    auto person_filter = take<binder::BoundFilterOp>(friends_agg->child);
    auto join = take<binder::BoundBinaryJoinOp>(person_filter->child);
    auto known_tag_project = take<binder::BoundProjectOp>(join->left);
    auto known_tag_scan = std::move(known_tag_project->child);
    auto person_side = std::move(join->right);

    // Friends side: collect(distinct friend) with no group key (one row).
    binder::BoundAggregateOp new_agg;
    new_agg.aggregates.push_back(std::move(friends_agg->aggregates[0]));
    new_agg.output_names.push_back(friends_agg->output_names[1]);
    person_filter->child = std::move(person_side);
    new_agg.child = BoundLogicalOperator(std::move(person_filter));

    // Tag side: knownTag -HAS_TAG-> post -HAS_CREATOR-> f.
    const std::string& post_var = shape.post_var;
    const std::string& friend_var = shape.friend_var;
    const std::string& known_tag_var = shape.known_tag_var;
    const std::string friends_var = shape.friends_var;

    auto post_to_f = makeReversedExpand(*creator, post_var, friend_var, cypher::RelationshipDirection::LEFT_TO_RIGHT,
                                        std::vector<LabelId>{person_lid}, std::move(known_tag_scan));
    // The child of post_to_f must be the reversed HAS_TAG expand. Build it now
    // and then attach it under post_to_f.
    auto tag_to_post = makeReversedExpand(*known, known_tag_var, post_var, cypher::RelationshipDirection::RIGHT_TO_LEFT,
                                          std::vector<LabelId>{post_lid}, BoundLogicalOperator{});
    tag_to_post.child = std::move(post_to_f.child);
    post_to_f.child = BoundLogicalOperator(std::make_unique<binder::BoundExpandOp>(std::move(tag_to_post)));

    auto cross = std::make_unique<binder::BoundBinaryJoinOp>();
    cross->join_type = binder::JoinType::Cross;
    cross->left = BoundLogicalOperator(std::make_unique<binder::BoundExpandOp>(std::move(post_to_f)));
    cross->right = BoundLogicalOperator(std::make_unique<binder::BoundAggregateOp>(std::move(new_agg)));

    auto in_filter = std::make_unique<binder::BoundFilterOp>();
    auto in_op = std::make_unique<binder::BoundBinaryOp>();
    in_op->op = cypher::BinaryOperator::IN;
    in_op->left = binder::BoundExpression(binder::BoundVariableRef(friend_var, binder::BoundType::Vertex()));
    in_op->right = std::move(friends_ref);
    in_op->result_type = binder::BoundType::Bool();
    in_filter->predicate = binder::BoundExpression(std::move(in_op));
    in_filter->child = BoundLogicalOperator(std::move(cross));
    op = BoundLogicalOperator(std::move(in_filter));

    replaced_from = known->dst_variable;
    replaced_to = known_tag_var;
    return true;
}

bool rewriteChildren(BoundLogicalOperator& op, const catalog::Catalog& catalog,
                     const std::unordered_map<LabelId, uint64_t>* label_vertex_counts, std::string& replaced_from,
                     std::string& replaced_to) {
    bool changed = false;
    forEachLogicalChild(op, [&](BoundLogicalOperator& child) {
        if (rewriteOne(child, catalog, label_vertex_counts, replaced_from, replaced_to)) {
            changed = true;
            return;
        }
        if (rewriteChildren(child, catalog, label_vertex_counts, replaced_from, replaced_to))
            changed = true;
    });
    return changed;
}

} // namespace

bool rewriteTagAnchoredJoin(binder::BoundLogicalPlan& plan, const catalog::Catalog* catalog,
                            const std::unordered_map<LabelId, uint64_t>* label_vertex_counts) {
    if (!catalog)
        return false;
    std::string from;
    std::string to;
    bool changed = rewriteOne(plan.root, *catalog, label_vertex_counts, from, to);
    if (!changed)
        changed = rewriteChildren(plan.root, *catalog, label_vertex_counts, from, to);
    if (changed && !from.empty() && !to.empty())
        replaceVarRefsInOp(plan.root, from, to);
    return changed;
}

} // namespace optimizer
} // namespace eugraph
