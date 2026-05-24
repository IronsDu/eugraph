#pragma once

#include "query/optimizer/memo.hpp"

#include <memory>
#include <string>
#include <vector>

namespace eugraph {
namespace optimizer {

// Maps BoundLogicalOperator variant indices to operator type enum.
enum class OptNodeType {
    Singleton,        // 0
    CorrelatedSource, // 1
    Scan,             // 2
    LabelScan,        // 3
    Expand,           // 4
    Filter,           // 5
    Project,          // 6
    Aggregate,        // 7
    Sort,             // 8
    Skip,             // 9
    Limit,            // 10
    Distinct,         // 11
    CreateNode,       // 12
    CreateEdge,       // 13
    Set,              // 14
    Remove,           // 15
    Delete,           // 16
    PathBuild,        // 17
    VarLenExpand,     // 18
    BinaryJoin,       // 19
    SemiJoin,         // 20
    Unwind            // 21
};

// Convert BoundLogicalOperator variant index to OptNodeType.
OptNodeType nodeTypeFromVariantIndex(size_t index);

// Pattern node for rule matching — describes the tree structure to match.
// A leaf (subtree wildcard) has empty children.
struct PatternNode {
    OptNodeType type;
    std::vector<PatternNode> children; // empty = leaf (match any subtree)
};

class RuleSet;

class OptRule {
public:
    virtual ~OptRule() = default;
    virtual std::string name() const = 0;
    virtual PatternNode pattern() const = 0;

    // Fast top-operator match check (O(1)).
    bool topMatch(const GroupExpr& expr) const;

    // Detailed condition check after binding.
    virtual bool condition(GroupExpr& /*expr*/, Memo& /*memo*/) const {
        return true;
    }

    // Generate replacement expressions. Non-const because rules may move
    // operator data from the original GroupExpr (BoundExpression contains
    // unique_ptr members that cannot be copied).
    virtual std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const = 0;

    // Priority: 0 = skip, higher = fire first.
    virtual int promise() const {
        return 1;
    }

    int index() const {
        return index_;
    }
    void setIndex(int idx) {
        index_ = idx;
    }

private:
    int index_ = -1;
};

class RuleSet {
public:
    void addRule(std::unique_ptr<OptRule> rule) {
        rule->setIndex(static_cast<int>(rules_.size()));
        rules_.push_back(std::move(rule));
    }

    const std::vector<std::unique_ptr<OptRule>>& rules() const {
        return rules_;
    }

private:
    std::vector<std::unique_ptr<OptRule>> rules_;
};

} // namespace optimizer
} // namespace eugraph
