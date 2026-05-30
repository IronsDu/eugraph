#include "query/optimizer/opt_rule.hpp"

#include "query/planner/bound_logical_plan.hpp"

namespace eugraph {
namespace optimizer {

OptNodeType nodeTypeFromVariantIndex(size_t index) {
    static constexpr OptNodeType mapping[] = {
        OptNodeType::Singleton,        // 0
        OptNodeType::CorrelatedSource, // 1
        OptNodeType::Scan,             // 2
        OptNodeType::LabelScan,        // 3
        OptNodeType::Expand,           // 4
        OptNodeType::Filter,           // 5
        OptNodeType::Project,          // 6
        OptNodeType::Aggregate,        // 7
        OptNodeType::Sort,             // 8
        OptNodeType::Skip,             // 9
        OptNodeType::Limit,            // 10
        OptNodeType::Distinct,         // 11
        OptNodeType::CreateNode,       // 12
        OptNodeType::CreateEdge,       // 13
        OptNodeType::Set,              // 14
        OptNodeType::Remove,           // 15
        OptNodeType::Delete,           // 16
        OptNodeType::PathBuild,        // 17
        OptNodeType::VarLenExpand,     // 18
        OptNodeType::BinaryJoin,       // 19
        OptNodeType::LeftJoin,         // 20
        OptNodeType::SemiJoin,         // 21
        OptNodeType::Unwind,           // 22
        OptNodeType::Union             // 23
    };
    return mapping[index];
}

bool OptRule::topMatch(const GroupExpr& expr) const {
    OptNodeType actual = nodeTypeFromVariantIndex(expr.op.index());
    return actual == pattern().type;
}

} // namespace optimizer
} // namespace eugraph
