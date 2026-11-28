#include "query/optimizer/rules/physical/impl_rules.hpp"

#include "query/optimizer/framework/memo.hpp"
#include "query/optimizer/framework/physical_expr.hpp"
#include "query/planner/logical_plan/operator/bound_index_probe_op.hpp"
#include "query/planner/logical_plan/operator/bound_label_scan_op.hpp"
#include "query/planner/logical_plan/operator/bound_varlen_expand_op.hpp"

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
static std::vector<std::unique_ptr<GroupExpr>> makePhysicalExpr(GroupExpr& expr, Memo& memo, PhysicalOpTag tag,
                                                                bool build_left = false,
                                                                bool batch_shortest_path = false) {
    auto phys = std::make_unique<PhysicalExpr>();
    phys->tag = tag;
    phys->build_left = build_left;
    phys->batch_shortest_path = batch_shortest_path;
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

PatternNode ImplIndexScanRule::pattern() const {
    return {OptNodeType::LabelScan, {}};
}
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

PatternNode ImplPatternComprehensionApplyRule::pattern() const {
    return {OptNodeType::PatternComprehensionApply, {{}, {}}};
}
std::vector<std::unique_ptr<GroupExpr>> ImplPatternComprehensionApplyRule::substitute(GroupExpr& expr,
                                                                                      Memo& memo) const {
    return makePhysicalExpr(expr, memo, PhysicalOpTag::PatternComprehensionApply);
}

PatternNode ImplIndexProbeRule::pattern() const {
    return {OptNodeType::IndexProbe, {}};
}

bool ImplIndexProbeRule::condition(GroupExpr& expr, Memo& /*memo*/) const {
    // Unresolved probes have no physical contract yet; R2 must resolve first.
    const auto* probe = std::get_if<std::unique_ptr<binder::BoundIndexProbeOp>>(&expr.op);
    return probe && *probe && (*probe)->index_id != 0;
}

std::vector<std::unique_ptr<GroupExpr>> ImplIndexProbeRule::substitute(GroupExpr& expr, Memo& memo) const {
    return makePhysicalExpr(expr, memo, PhysicalOpTag::IndexProbe);
}

PatternNode ImplIndexProbeJoinRule::pattern() const {
    return {OptNodeType::IndexProbeJoin, {{}, {}}};
}

std::vector<std::unique_ptr<GroupExpr>> ImplIndexProbeJoinRule::substitute(GroupExpr& expr, Memo& memo) const {
    auto phys = std::make_unique<PhysicalExpr>();
    phys->tag = PhysicalOpTag::IndexProbeJoin;
    phys->source = cloneBoundLogicalOperator(expr.op);

    // The anchor input must provide whatever this group's expressions need.
    // The probe input reads its own index range: asking it to materialise the
    // consumed property would force an Enricher over a descriptor leaf.
    Group& group = memo.getGroup(expr.group_id);
    phys->required_input_mat.resize(expr.child_groups.size());
    if (group.requirementsValid() && !expr.child_groups.empty())
        phys->required_input_mat[0] = group.requirements();

    std::vector<std::unique_ptr<GroupExpr>> result;
    result.push_back(std::make_unique<GroupExpr>(INVALID_EXPR_ID, expr.group_id, std::move(phys), expr.child_groups));
    return result;
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
    // Bounded shortestPath shares one forward BFS among rows whose start vertex
    // is the same. The strategy is a physical descriptor (ChosenPlan attribute),
    // no longer a logical field set by a normalization pass: the rule decides
    // whether the candidate exists, lowering only forwards it.
    //
    // The depth bound is part of the operator contract (shortestPathsFromSource
    // refuses deeper searches), so a bounded search is the only eligible shape.
    // Batch vs per-row is a real trade-off that depends on how many rows share a
    // start (source NDV) and on the frontier growth; without those statistics a
    // cost comparison would be fabricated, so the rule emits this candidate only
    // and the second candidate + cost arrive with the P3 statistics.
    bool batch = false;
    if (const auto* vle = std::get_if<std::unique_ptr<binder::BoundVarLenExpandOp>>(&expr.op); vle && *vle) {
        batch = (*vle)->shortest_path && !(*vle)->all_shortest_paths && (*vle)->max_hops >= 0 && (*vle)->max_hops <= 8;
    }
    return makePhysicalExpr(expr, memo, PhysicalOpTag::VarLenExpand, /*build_left=*/false, batch);
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
    // Two candidates: build the left or the right side. Cost/memory decides;
    // the chosen side is part of the descriptor and lowering must not re-pick.
    auto out = makePhysicalExpr(expr, memo, PhysicalOpTag::HashJoin, /*build_left=*/false);
    auto left_build = makePhysicalExpr(expr, memo, PhysicalOpTag::HashJoin, /*build_left=*/true);
    out.insert(out.end(), std::make_move_iterator(left_build.begin()), std::make_move_iterator(left_build.end()));
    return out;
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
