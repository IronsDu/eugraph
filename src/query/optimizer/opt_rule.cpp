#include "query/optimizer/opt_rule.hpp"

#include "query/planner/bound_logical_plan.hpp"

namespace eugraph {
namespace optimizer {

OptNodeType nodeTypeFromVariantIndex(size_t index) {
    static constexpr OptNodeType mapping[] = {
        OptNodeType::Singleton,    // 0
        OptNodeType::Scan,         // 1
        OptNodeType::LabelScan,    // 2
        OptNodeType::Expand,       // 3
        OptNodeType::Filter,       // 4
        OptNodeType::Project,      // 5
        OptNodeType::Aggregate,    // 6
        OptNodeType::Sort,         // 7
        OptNodeType::Skip,         // 8
        OptNodeType::Limit,        // 9
        OptNodeType::Distinct,     // 10
        OptNodeType::CreateNode,   // 11
        OptNodeType::CreateEdge,   // 12
        OptNodeType::Set,          // 13
        OptNodeType::Remove,       // 14
        OptNodeType::PathBuild,    // 15
        OptNodeType::VarLenExpand, // 16
        OptNodeType::BinaryJoin,   // 17
        OptNodeType::Unwind        // 18
    };
    return mapping[index];
}

bool OptRule::topMatch(const GroupExpr& expr) const {
    OptNodeType actual = nodeTypeFromVariantIndex(expr.op.index());
    return actual == pattern().type;
}

} // namespace optimizer
} // namespace eugraph
