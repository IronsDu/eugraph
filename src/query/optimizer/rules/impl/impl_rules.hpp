#pragma once

#include "query/optimizer/opt_rule.hpp"

namespace eugraph {
namespace optimizer {

// ============================================================
// Implementation rules (Phase 4) — convert logical operators
// to physical expressions.
//
// Each rule matches a logical operator type and produces a
// physical GroupExpr carrying a PhysicalExpr with the matching
// PhysicalOpTag. All rules override isImplementation() → true
// so O_EXPR fires them only during optimization (not exploration).
//
// substitute() clones the source logical operator into the
// PhysicalExpr's `source` field and creates a physical GroupExpr
// with the same child_groups as the source.
// ============================================================

// ---- Leaf sources ----
class ImplSingletonRule : public OptRule {
public:
    std::string name() const override {
        return "ImplSingleton";
    }
    bool isImplementation() const override {
        return true;
    }
    PatternNode pattern() const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};

class ImplCorrelatedSourceRule : public OptRule {
public:
    std::string name() const override {
        return "ImplCorrelatedSource";
    }
    bool isImplementation() const override {
        return true;
    }
    PatternNode pattern() const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};

class ImplScanRule : public OptRule {
public:
    std::string name() const override {
        return "ImplScan";
    }
    bool isImplementation() const override {
        return true;
    }
    PatternNode pattern() const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};

class ImplLabelScanRule : public OptRule {
public:
    std::string name() const override {
        return "ImplLabelScan";
    }
    bool isImplementation() const override {
        return true;
    }
    PatternNode pattern() const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};

// ---- Unary operators ----
class ImplFilterRule : public OptRule {
public:
    std::string name() const override {
        return "ImplFilter";
    }
    bool isImplementation() const override {
        return true;
    }
    PatternNode pattern() const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};

class ImplProjectRule : public OptRule {
public:
    std::string name() const override {
        return "ImplProject";
    }
    bool isImplementation() const override {
        return true;
    }
    PatternNode pattern() const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};

class ImplExpandRule : public OptRule {
public:
    std::string name() const override {
        return "ImplExpand";
    }
    bool isImplementation() const override {
        return true;
    }
    PatternNode pattern() const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};

class ImplVarLenExpandRule : public OptRule {
public:
    std::string name() const override {
        return "ImplVarLenExpand";
    }
    bool isImplementation() const override {
        return true;
    }
    PatternNode pattern() const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};

class ImplSortRule : public OptRule {
public:
    std::string name() const override {
        return "ImplSort";
    }
    bool isImplementation() const override {
        return true;
    }
    PatternNode pattern() const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};

class ImplLimitRule : public OptRule {
public:
    std::string name() const override {
        return "ImplLimit";
    }
    bool isImplementation() const override {
        return true;
    }
    PatternNode pattern() const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};

class ImplSkipRule : public OptRule {
public:
    std::string name() const override {
        return "ImplSkip";
    }
    bool isImplementation() const override {
        return true;
    }
    PatternNode pattern() const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};

class ImplDistinctRule : public OptRule {
public:
    std::string name() const override {
        return "ImplDistinct";
    }
    bool isImplementation() const override {
        return true;
    }
    PatternNode pattern() const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};

class ImplAggregateRule : public OptRule {
public:
    std::string name() const override {
        return "ImplAggregate";
    }
    bool isImplementation() const override {
        return true;
    }
    PatternNode pattern() const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};

class ImplPathBuildRule : public OptRule {
public:
    std::string name() const override {
        return "ImplPathBuild";
    }
    bool isImplementation() const override {
        return true;
    }
    PatternNode pattern() const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};

class ImplUnwindRule : public OptRule {
public:
    std::string name() const override {
        return "ImplUnwind";
    }
    bool isImplementation() const override {
        return true;
    }
    PatternNode pattern() const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};

// ---- Binary operators ----
class ImplBinaryJoinRule : public OptRule {
public:
    std::string name() const override {
        return "ImplBinaryJoin";
    }
    bool isImplementation() const override {
        return true;
    }
    PatternNode pattern() const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};

class ImplLeftJoinRule : public OptRule {
public:
    std::string name() const override {
        return "ImplLeftJoin";
    }
    bool isImplementation() const override {
        return true;
    }
    PatternNode pattern() const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};

class ImplSemiJoinRule : public OptRule {
public:
    std::string name() const override {
        return "ImplSemiJoin";
    }
    bool isImplementation() const override {
        return true;
    }
    PatternNode pattern() const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};

class ImplUnionRule : public OptRule {
public:
    std::string name() const override {
        return "ImplUnion";
    }
    bool isImplementation() const override {
        return true;
    }
    PatternNode pattern() const override;
    std::vector<std::unique_ptr<GroupExpr>> substitute(GroupExpr& expr, Memo& memo) const override;
};

} // namespace optimizer
} // namespace eugraph
