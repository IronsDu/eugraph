#include "compute_service/optimizer/opt_rule.hpp"

#include "compute_service/planner/bound_logical_plan.hpp"

namespace eugraph {
namespace optimizer {

OptNodeType nodeTypeFromVariantIndex(size_t index) {
    static constexpr OptNodeType mapping[] = {
        OptNodeType::Scan,       // 0
        OptNodeType::LabelScan,  // 1
        OptNodeType::Expand,     // 2
        OptNodeType::Filter,     // 3
        OptNodeType::Project,    // 4
        OptNodeType::Aggregate,  // 5
        OptNodeType::Sort,       // 6
        OptNodeType::Skip,       // 7
        OptNodeType::Limit,      // 8
        OptNodeType::Distinct,   // 9
        OptNodeType::CreateNode, // 10
        OptNodeType::CreateEdge, // 11
        OptNodeType::Set,        // 12
        OptNodeType::Remove,     // 13
        OptNodeType::PathBuild   // 14
    };
    return mapping[index];
}

bool OptRule::topMatch(const GroupExpr& expr) const {
    OptNodeType actual = nodeTypeFromVariantIndex(expr.op.index());
    return actual == pattern().type;
}

} // namespace optimizer
} // namespace eugraph
