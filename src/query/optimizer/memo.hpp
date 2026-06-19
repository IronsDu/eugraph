#pragma once

#include "query/optimizer/cbo.hpp"
#include "query/optimizer/chosen_plan.hpp"
#include "query/optimizer/log_prop.hpp"
#include "query/optimizer/physical_expr.hpp"
#include "query/planner/bound_expression/bound_expression_fwd.hpp"
#include "query/planner/bound_logical_plan_fwd.hpp"

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace catalog {
class Catalog;
}

namespace optimizer {

using GroupId = uint32_t;
using ExprId = uint32_t;

inline constexpr GroupId INVALID_GROUP_ID = UINT32_MAX;
inline constexpr ExprId INVALID_EXPR_ID = UINT32_MAX;

class Memo; // forward declaration (Group methods reference Memo)

// ============================================================
// Winner — best plan for a given physical property (Columbia: WINNER)
//
// Stores the cheapest M_EXPR (by ExprId) that satisfies a given
// PhysProp, along with its Cost and a Done flag.
// Done=false means the winner is under construction.
// ============================================================
class Winner {
public:
    Winner(ExprId plan, PhysProp prop, Cost cost, bool done = false)
        : plan_(plan), prop_(std::move(prop)), cost_(std::move(cost)), done_(done) {}

    ExprId plan() const {
        return plan_;
    }
    const PhysProp& physProp() const {
        return prop_;
    }
    const Cost& cost() const {
        return cost_;
    }
    bool done() const {
        return done_;
    }

    void setPlan(ExprId p) {
        plan_ = p;
    }
    void setCost(Cost c) {
        cost_ = std::move(c);
    }
    void setDone(bool d) {
        done_ = d;
    }

private:
    ExprId plan_ = INVALID_EXPR_ID;
    PhysProp prop_;
    Cost cost_;
    bool done_ = false;
};

// ============================================================
// Group — a set of logically equivalent GroupExprs
//
// State flags match Columbia's BIT_STATE:
//   exploring  — E_GROUP has been pushed, exploration in progress
//   explored   — group has been fully explored (all logical exprs generated)
//   optimizing — O_GROUP has been pushed, CBO optimization in progress
//   optimized  — group has been fully optimized (CBO phase)
//
// Winner's circle: maps physical property → best plan.
// ============================================================
class Group {
public:
    explicit Group(GroupId id) : id(id) {}

    GroupId id;

    std::vector<ExprId> logical_exprs;
    std::vector<ExprId> physical_exprs;

    // State flags (Columbia: BIT_STATE)
    bool changed = false;
    bool exploring = false;
    bool explored = false;
    bool optimizing = false; // CBO: is optimization in progress?
    bool optimized = false;  // CBO: has the group been fully optimized?

    // ----- Winner's circle (CBO) -----

    // Find a winner for the given physical property. Returns nullptr if none.
    Winner* getWinner(const PhysProp& prop);

    // Create or update a winner for the given physical property.
    void newWinner(PhysProp prop, ExprId plan, Cost cost, bool done);

    // Columbia: search_circle — check winner status for a context.
    // Returns true if a satisfying winner exists.
    // Sets moreSearch to indicate whether more optimization is needed.
    bool searchCircle(const Context& ctx, bool& moreSearch) const;

    // Check if any winner is done (Columbia: CheckWinnerDone).
    bool hasDoneWinner() const;

    std::vector<Winner> winners;

    // ----- LogProp (CBO Phase 3) -----

    // Lazily derive and cache the group's LogProp. Uses the first logical
    // expression and recursively derives child group LogProps.
    const LogProp& getLogProp(Memo& memo, const catalog::Catalog* catalog);

    // Invalidate cached LogProp and propagate to parent groups.
    void invalidateLogProp(Memo& memo);

    bool logPropValid() const {
        return log_prop_valid_;
    }

private:
    LogProp log_prop_;
    bool log_prop_valid_ = false;
    // Groups that derived their LogProp from this group (for invalidation propagation)
    std::set<GroupId> parents_;
};

// ============================================================
// GroupExpr — operator data + child group references
// (corresponds to Columbia M_EXPR)
//
// Holds either a logical operator (is_physical_ == false) or a
// physical expression (is_physical_ == true). Implementation rules
// produce physical GroupExprs; the task pipeline routes them through
// OInputsTask for costing.
// ============================================================
class GroupExpr {
public:
    // Logical expression constructor
    GroupExpr(ExprId id, GroupId group_id, binder::BoundLogicalOperator op, std::vector<GroupId> child_groups)
        : id(id), group_id(group_id), op(std::move(op)), child_groups(std::move(child_groups)) {}

    // Physical expression constructor (Phase 4)
    GroupExpr(ExprId id, GroupId group_id, std::unique_ptr<PhysicalExpr> phys, std::vector<GroupId> child_groups)
        : id(id), group_id(group_id), child_groups(std::move(child_groups)), is_physical_(true),
          phys_op_(std::move(phys)) {}

    ExprId id;
    GroupId group_id;

    // The operator data. For logical expressions, this holds the actual op.
    // For physical expressions, this is default-constructed (use phys_op_ instead).
    binder::BoundLogicalOperator op;
    std::vector<GroupId> child_groups;

    // Bit mask tracking which rules have been applied to avoid re-firing.
    uint64_t rule_mask = 0;

    void markRuleFired(int rule_idx) {
        rule_mask |= (uint64_t{1} << rule_idx);
    }
    bool canFire(int rule_idx) const {
        return (rule_mask & (uint64_t{1} << rule_idx)) == 0;
    }

    // CBO: is this expression a physical operator?
    bool isPhysical() const {
        return is_physical_;
    }

    const PhysicalExpr& physOp() const {
        return *phys_op_;
    }

private:
    bool is_physical_ = false;
    std::unique_ptr<PhysicalExpr> phys_op_;
};

// ============================================================
// Memo — the search space (corresponds to Columbia SSP)
// ============================================================
class Memo {
public:
    // Insert a BoundLogicalOperator tree into the Memo.
    // Returns the GroupId of the root group.
    GroupId copyIn(binder::BoundLogicalOperator& op);

    // Extract the best plan from the Memo as a BoundLogicalOperator tree.
    // RBO fallback: takes the last logical expression in each group.
    binder::BoundLogicalOperator copyOut(GroupId root_gid);

    // CBO: extract the best plan for a given physical property.
    // Looks up the winner's circle in the root group; falls back to RBO
    // if no winner exists (e.g. no physical operators registered yet).
    binder::BoundLogicalOperator copyOut(GroupId root_gid, const PhysProp& prop);

    // CBO Phase 4: extract the chosen physical plan tree.
    // Returns null if any group in the winner chain lacks a physical winner.
    std::unique_ptr<ChosenPlan> extractChosen(GroupId root_gid, const PhysProp& prop);

    // Check if an equivalent expression already exists in the Memo.
    // Matches operator content via equalBoundLogicalOperator and child_groups
    // by value (Columbia: SSP::FindDup + OP::operator==).
    GroupExpr* findDuplicate(const binder::BoundLogicalOperator& op, const std::vector<GroupId>& child_groups);

    // CBO Phase 4: find duplicate physical expression.
    GroupExpr* findPhysDuplicate(const PhysicalExpr& phys, const std::vector<GroupId>& child_groups);

    // Merge two equivalent groups (larger ID into smaller ID).
    GroupId mergeGroups(GroupId g1, GroupId g2);

    // Insert a new GroupExpr into an existing group (or a new group).
    GroupExpr* insertExpr(std::unique_ptr<GroupExpr> expr, GroupId target_group);

    // Insert a physical expression into a group.
    GroupExpr* insertPhysExpr(std::unique_ptr<GroupExpr> expr, GroupId target_group);

    // Create a new group with a single GroupExpr. Returns the GroupExpr pointer.
    GroupExpr* createGroupWithExpr(binder::BoundLogicalOperator op, std::vector<GroupId> child_groups);

    // Accessors
    Group& getGroup(GroupId id) {
        return *groups_[id];
    }
    GroupExpr& getExpr(ExprId id) {
        return *exprs_[id];
    }

    GroupId newGroupId();
    ExprId newExprId();

    // Global hash: (op_type, op_content, child_groups) → ExprId
    // Used by findDuplicate to detect duplicates across all groups.
    // (Columbia: SSP::HashTbl). The hash combines the operator variant
    // index with operator content (via hashBoundLogicalOperator) and the
    // child group IDs — this is content-aware dedup, mirroring Columbia's
    // OP::hash + M_EXPR::hash.
    uint64_t computeHash(const binder::BoundLogicalOperator& op, const std::vector<GroupId>& child_groups) const;

    // CBO Phase 4: hash for physical expressions
    uint64_t computePhysHash(const PhysicalExpr& phys, const std::vector<GroupId>& child_groups) const;
    void registerInHash(ExprId eid);
    void unregisterInHash(const GroupExpr& expr);

    // CBO: context vector (Columbia: CONT::vc)
    std::vector<Context>& contexts() {
        return contexts_;
    }
    const std::vector<Context>& contexts() const {
        return contexts_;
    }
    int addContext(Context ctx) {
        contexts_.push_back(std::move(ctx));
        return static_cast<int>(contexts_.size()) - 1;
    }

    // Catalog for LogProp derivation. Set by LogicalOptimizer::optimize so
    // Group::getLogProp callers (e.g. OInputsTask) can fetch cardinality
    // statistics without threading the pointer through every call site.
    // nullptr → default estimates (LogPropDeriver::kDefaultVertexCount etc.).
    const catalog::Catalog* getCatalog() const {
        return catalog_;
    }
    void setCatalog(const catalog::Catalog* catalog) {
        catalog_ = catalog;
    }

private:
    std::vector<std::unique_ptr<Group>> groups_;
    std::vector<std::unique_ptr<GroupExpr>> exprs_;
    GroupId next_group_id_ = 0;
    ExprId next_expr_id_ = 0;

    // Global hash table for duplicate detection (Columbia: SSP::HashTbl)
    std::unordered_map<uint64_t, std::vector<ExprId>> hash_table_;

    // CBO: shared context vector (Columbia: CONT::vc)
    std::vector<Context> contexts_;

    // CBO: shared catalog pointer for LogProp derivation
    const catalog::Catalog* catalog_ = nullptr;
};

// ============================================================
// Helper: set the child field of a BoundLogicalOperator.
// The child field of leaf operators (BoundScanOp, BoundLabelScanOp) is left unchanged.
// ============================================================
void setChild(binder::BoundLogicalOperator& op, binder::BoundLogicalOperator child);

// Get the number of children for a BoundLogicalOperator (0, 1, 2).
int getChildCount(const binder::BoundLogicalOperator& op);

// Deep clone a BoundLogicalOperator (operator data only, child field is set separately).
binder::BoundLogicalOperator cloneBoundLogicalOperator(const binder::BoundLogicalOperator& op);

// Deep clone a BoundExpression tree.
binder::BoundExpression cloneBoundExpression(const binder::BoundExpression& expr);

} // namespace optimizer
} // namespace eugraph
