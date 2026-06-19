#include "query/optimizer/rules/impl/impl_rules.hpp"

#include "query/optimizer/memo.hpp"
#include "query/optimizer/physical_expr.hpp"

namespace eugraph {
namespace optimizer {

// ============================================================
// Helper: create a physical GroupExpr from a logical source
// ============================================================
static std::vector<std::unique_ptr<GroupExpr>> makePhysicalExpr(GroupExpr& expr, Memo& memo, PhysicalOpTag tag) {
    auto phys = std::make_unique<PhysicalExpr>();
    phys->tag = tag;
    phys->source = cloneBoundLogicalOperator(expr.op);
    auto ge = std::make_unique<GroupExpr>(memo.newExprId(), expr.group_id, std::move(phys), expr.child_groups);
    std::vector<std::unique_ptr<GroupExpr>> result;
    result.push_back(std::move(ge));
    return result;
}

// ============================================================
// Leaf sources
// ============================================================

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
    return makePhysicalExpr(expr, memo, PhysicalOpTag::CrossProduct);
}

} // namespace optimizer
} // namespace eugraph
