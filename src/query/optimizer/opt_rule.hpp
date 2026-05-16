#pragma once

#include "query/optimizer/memo.hpp"

#include <memory>
#include <string>
#include <vector>

namespace eugraph {
namespace optimizer {

// Maps BoundLogicalOperator variant indices to operator type enum.
enum class OptNodeType {
    Scan,         // 0
    LabelScan,    // 1
    Expand,       // 2
    Filter,       // 3
    Project,      // 4
    Aggregate,    // 5
    Sort,         // 6
    Skip,         // 7
    Limit,        // 8
    Distinct,     // 9
    CreateNode,   // 10
    CreateEdge,   // 11
    Set,          // 12
    Remove,       // 13
    PathBuild,    // 14
    VarLenExpand, // 15
    BinaryJoin    // 16
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
