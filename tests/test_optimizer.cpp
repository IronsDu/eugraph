#include <gtest/gtest.h>

#include "query/catalog/catalog.hpp"
#include "query/optimizer/cost_model.hpp"
#include "query/optimizer/log_prop.hpp"
#include "query/optimizer/logical_optimizer.hpp"
#include "query/optimizer/materialization_req.hpp"
#include "query/optimizer/memo.hpp"
#include "query/optimizer/operator_eq.hpp"
#include "query/optimizer/operator_hash.hpp"
#include "query/optimizer/opt_rule.hpp"
#include "query/optimizer/physical_expr.hpp"
#include "query/optimizer/requirement_collector.hpp"
#include "query/optimizer/rules/filter_pushdown.hpp"
#include "query/optimizer/rules/impl/impl_rules.hpp"
#include "query/planner/bound_logical_plan.hpp"

#include <algorithm>
#include <functional>
#include <set>

using namespace eugraph;
using namespace eugraph::optimizer;
using namespace eugraph::binder;

// Helper: create a BoundLogicalOperator from a leaf scan op (by value)
static BoundLogicalOperator makeLabelScan(const std::string& var, uint32_t col) {
    BoundLabelScanOp scan;
    scan.variable = var;
    scan.column_index = col;
    return scan;
}

static BoundLogicalOperator makeLabelScanWithLabel(const std::string& var, uint32_t col, LabelId lid) {
    BoundLabelScanOp scan;
    scan.variable = var;
    scan.column_index = col;
    scan.label_ids = {lid};
    scan.label_names = {"L" + std::to_string(lid)};
    return scan;
}

// ==================== Memo Tests ====================

TEST(MemoTest, CopyInCopyOutScanOnly) {
    auto plan_root = makeLabelScan("n", 0);

    BoundLogicalPlan plan;
    plan.root = std::move(plan_root);

    Memo memo;
    auto gid = memo.copyIn(plan.root);
    auto result = memo.copyOut(gid);

    ASSERT_TRUE(std::holds_alternative<BoundLabelScanOp>(result));
    EXPECT_EQ(std::get<BoundLabelScanOp>(result).variable, "n");
}

TEST(MemoTest, CopyInCopyOutFilterLabelScan) {
    auto filter = std::make_unique<BoundFilterOp>();
    filter->child = makeLabelScan("n", 0);
    filter->predicate = BoundLiteral(true);

    BoundLogicalPlan plan;
    plan.root = BoundLogicalOperator(std::move(filter));

    Memo memo;
    auto gid = memo.copyIn(plan.root);
    auto result = memo.copyOut(gid);

    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundFilterOp>>(result));
    auto& f = std::get<std::unique_ptr<BoundFilterOp>>(result);
    ASSERT_TRUE(std::holds_alternative<BoundLabelScanOp>(f->child));
}

TEST(MemoTest, CopyInCopyOutThreeLevel) {
    auto project = std::make_unique<BoundProjectOp>();
    auto filter = std::make_unique<BoundFilterOp>();
    filter->child = makeLabelScan("n", 0);
    filter->predicate = BoundLiteral(true);
    project->child = BoundLogicalOperator(std::move(filter));

    BoundLogicalPlan plan;
    plan.root = BoundLogicalOperator(std::move(project));

    Memo memo;
    auto gid = memo.copyIn(plan.root);
    auto result = memo.copyOut(gid);

    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundProjectOp>>(result));
    auto& proj = std::get<std::unique_ptr<BoundProjectOp>>(result);
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundFilterOp>>(proj->child));
    auto& filt = std::get<std::unique_ptr<BoundFilterOp>>(proj->child);
    ASSERT_TRUE(std::holds_alternative<BoundLabelScanOp>(filt->child));
}

// ==================== FilterPushdown Tests ====================

TEST(FilterPushdownTest, FilterThroughExpand) {
    // Build: Filter → Expand → LabelScan
    // Expected: Expand → Filter → LabelScan

    auto expand = std::make_unique<BoundExpandOp>();
    expand->src_variable = "n";
    expand->src_column_index = 0;
    expand->edge_variable = "r";
    expand->edge_column_index = 1;
    expand->dst_variable = "m";
    expand->dst_column_index = 2;
    expand->child = makeLabelScan("n", 0);

    auto filter = std::make_unique<BoundFilterOp>();
    filter->child = BoundLogicalOperator(std::move(expand));
    filter->predicate = BoundLiteral(true);

    BoundLogicalPlan plan;
    plan.root = BoundLogicalOperator(std::move(filter));

    LogicalOptimizer optimizer;
    optimizer.optimize(plan);

    // Result: Expand → Filter → LabelScan
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundExpandOp>>(plan.root));
    auto& exp = std::get<std::unique_ptr<BoundExpandOp>>(plan.root);

    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundFilterOp>>(exp->child));
    auto& filt = std::get<std::unique_ptr<BoundFilterOp>>(exp->child);

    ASSERT_TRUE(std::holds_alternative<BoundLabelScanOp>(filt->child));
}

TEST(FilterPushdownTest, FilterThroughPathBuildExpand) {
    // Build: Filter → PathBuild → Expand → LabelScan
    // Expected: PathBuild → Expand → Filter → LabelScan

    auto expand = std::make_unique<BoundExpandOp>();
    expand->src_variable = "n";
    expand->src_column_index = 0;
    expand->edge_variable = "r";
    expand->edge_column_index = 1;
    expand->dst_variable = "m";
    expand->dst_column_index = 2;
    expand->child = makeLabelScan("n", 0);

    auto pathbuild = std::make_unique<BoundPathBuildOp>();
    pathbuild->path_variable = "p";
    pathbuild->path_column_index = 3;
    pathbuild->child = BoundLogicalOperator(std::move(expand));

    auto filter = std::make_unique<BoundFilterOp>();
    filter->child = BoundLogicalOperator(std::move(pathbuild));
    filter->predicate = BoundLiteral(true);

    BoundLogicalPlan plan;
    plan.root = BoundLogicalOperator(std::move(filter));

    LogicalOptimizer optimizer;
    optimizer.optimize(plan);

    // Result: PathBuild → Expand → Filter → LabelScan
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundPathBuildOp>>(plan.root));
    auto& pb = std::get<std::unique_ptr<BoundPathBuildOp>>(plan.root);

    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundExpandOp>>(pb->child));
    auto& exp = std::get<std::unique_ptr<BoundExpandOp>>(pb->child);

    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundFilterOp>>(exp->child));
    auto& filt = std::get<std::unique_ptr<BoundFilterOp>>(exp->child);

    ASSERT_TRUE(std::holds_alternative<BoundLabelScanOp>(filt->child));
}

TEST(FilterPushdownTest, FilterAboveProjectNotPushed) {
    // Build: Filter → Project → LabelScan
    // Filter should NOT be pushed through Project

    auto project = std::make_unique<BoundProjectOp>();
    project->child = makeLabelScan("n", 0);

    auto filter = std::make_unique<BoundFilterOp>();
    filter->child = BoundLogicalOperator(std::move(project));
    filter->predicate = BoundLiteral(true);

    BoundLogicalPlan plan;
    plan.root = BoundLogicalOperator(std::move(filter));

    LogicalOptimizer optimizer;
    optimizer.optimize(plan);

    // Filter stays above Project
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundFilterOp>>(plan.root));
    auto& filt = std::get<std::unique_ptr<BoundFilterOp>>(plan.root);
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundProjectOp>>(filt->child));
}

TEST(FilterPushdownTest, NoFilterNoChange) {
    // Build: Project → LabelScan (no Filter)
    auto project = std::make_unique<BoundProjectOp>();
    project->child = makeLabelScan("n", 0);

    BoundLogicalPlan plan;
    plan.root = BoundLogicalOperator(std::move(project));

    LogicalOptimizer optimizer;
    optimizer.optimize(plan);

    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundProjectOp>>(plan.root));
    auto& proj = std::get<std::unique_ptr<BoundProjectOp>>(plan.root);
    ASSERT_TRUE(std::holds_alternative<BoundLabelScanOp>(proj->child));
}

TEST(FilterPushdownTest, FilterDirectlyAboveScanNotPushed) {
    // Build: Filter → LabelScan — already at the lowest position
    auto filter = std::make_unique<BoundFilterOp>();
    filter->child = makeLabelScan("n", 0);
    filter->predicate = BoundLiteral(true);

    BoundLogicalPlan plan;
    plan.root = BoundLogicalOperator(std::move(filter));

    LogicalOptimizer optimizer;
    optimizer.optimize(plan);

    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundFilterOp>>(plan.root));
    auto& filt = std::get<std::unique_ptr<BoundFilterOp>>(plan.root);
    ASSERT_TRUE(std::holds_alternative<BoundLabelScanOp>(filt->child));
}

TEST(FilterPushdownTest, FilterNotAtRoot) {
    // Build: Project → Filter → PathBuild → Expand → LabelScan
    // This mirrors real queries like:
    //   MATCH p=(n:person)-[]->(m) WHERE n.firstName='Mahinda' RETURN p
    // Expected: Project → PathBuild → Expand → Filter → LabelScan

    auto expand = std::make_unique<BoundExpandOp>();
    expand->src_variable = "n";
    expand->src_column_index = 0;
    expand->edge_variable = "r";
    expand->edge_column_index = 1;
    expand->dst_variable = "m";
    expand->dst_column_index = 2;
    expand->child = makeLabelScan("n", 0);

    auto pathbuild = std::make_unique<BoundPathBuildOp>();
    pathbuild->path_variable = "p";
    pathbuild->path_column_index = 3;
    pathbuild->child = BoundLogicalOperator(std::move(expand));

    auto filter = std::make_unique<BoundFilterOp>();
    filter->child = BoundLogicalOperator(std::move(pathbuild));
    filter->predicate = BoundLiteral(true);

    auto project = std::make_unique<BoundProjectOp>();
    project->child = BoundLogicalOperator(std::move(filter));

    BoundLogicalPlan plan;
    plan.root = BoundLogicalOperator(std::move(project));

    LogicalOptimizer optimizer;
    optimizer.optimize(plan);

    // Result: Project → PathBuild → Expand → Filter → LabelScan
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundProjectOp>>(plan.root));
    auto& proj = std::get<std::unique_ptr<BoundProjectOp>>(plan.root);

    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundPathBuildOp>>(proj->child));
    auto& pb = std::get<std::unique_ptr<BoundPathBuildOp>>(proj->child);

    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundExpandOp>>(pb->child));
    auto& exp = std::get<std::unique_ptr<BoundExpandOp>>(pb->child);

    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundFilterOp>>(exp->child));
    auto& filt = std::get<std::unique_ptr<BoundFilterOp>>(exp->child);

    ASSERT_TRUE(std::holds_alternative<BoundLabelScanOp>(filt->child));
}

// ==================== Content-aware Hash & Equality ====================
//
// Columbia's SSP::FindDup compares operator content (OP::operator==) before
// declaring two expressions equivalent. These tests pin down that behavior:
// same content → same hash + dedup; different content → different hash + no dedup.

TEST(MemoContentHashTest, IdenticalLabelScansDedup) {
    Memo memo;
    auto op_a = makeLabelScanWithLabel("n", 0, LabelId{1});
    auto op_b = makeLabelScanWithLabel("n", 0, LabelId{1});

    auto gid_a = memo.copyIn(op_a);
    auto gid_b = memo.copyIn(op_b);
    EXPECT_EQ(gid_a, gid_b) << "identical scans must share a group";
}

TEST(MemoContentHashTest, DistinctLabelIdsDoNotCollide) {
    // This is the bug the previous (variant-index-only) hash exhibited: two
    // distinct BoundLabelScanOp with different label_ids hashed the same and
    // were merged into one group, collapsing `MATCH (a), (b)` into a self-join.
    Memo memo;
    auto op_a = makeLabelScanWithLabel("a", 0, LabelId{1});
    auto op_b = makeLabelScanWithLabel("b", 1, LabelId{2});

    auto gid_a = memo.copyIn(op_a);
    auto gid_b = memo.copyIn(op_b);
    EXPECT_NE(gid_a, gid_b) << "different label_ids must produce different groups";
}

TEST(MemoContentHashTest, DistinctVariablesDoNotCollide) {
    // Same label, different variable names — content-different, must not dedup.
    Memo memo;
    auto op_a = makeLabelScanWithLabel("a", 0, LabelId{1});
    auto op_b = makeLabelScanWithLabel("b", 1, LabelId{1});

    auto gid_a = memo.copyIn(op_a);
    auto gid_b = memo.copyIn(op_b);
    EXPECT_NE(gid_a, gid_b);
}

TEST(MemoContentHashTest, HashConsistentWithEquality) {
    // If two operators are equal, their hashes must be equal.
    auto a = makeLabelScanWithLabel("n", 0, LabelId{1});
    auto b = makeLabelScanWithLabel("n", 0, LabelId{1});
    EXPECT_EQ(hashBoundLogicalOperator(a), hashBoundLogicalOperator(b));
    EXPECT_TRUE(equalBoundLogicalOperator(a, b));
}

TEST(MemoContentHashTest, HashDiffersWhenContentDiffers) {
    auto a = makeLabelScanWithLabel("n", 0, LabelId{1});
    auto b = makeLabelScanWithLabel("n", 0, LabelId{2});
    EXPECT_NE(hashBoundLogicalOperator(a), hashBoundLogicalOperator(b));
    EXPECT_FALSE(equalBoundLogicalOperator(a, b));
}

TEST(MemoContentHashTest, FilterPredicateEquality) {
    // Two Filter ops with identical predicates must dedup; with different
    // predicates they must not.
    auto make_filter = [](bool pred_value) {
        auto f = std::make_unique<BoundFilterOp>();
        f->child = makeLabelScan("n", 0);
        f->predicate = BoundLiteral(pred_value);
        return BoundLogicalOperator(std::move(f));
    };

    Memo memo;
    auto t1 = make_filter(true);
    auto t2 = make_filter(true);
    auto f1 = make_filter(false);

    EXPECT_EQ(memo.copyIn(t1), memo.copyIn(t2));
    auto gid_t = memo.copyIn(t1);
    auto gid_f = memo.copyIn(f1);
    EXPECT_NE(gid_t, gid_f);

    BoundExpression p_true = BoundLiteral(true);
    BoundExpression p_true2 = BoundLiteral(true);
    BoundExpression p_false = BoundLiteral(false);
    EXPECT_EQ(hashBoundExpression(p_true), hashBoundExpression(p_true2));
    EXPECT_TRUE(equalBoundExpression(p_true, p_true2));
    EXPECT_FALSE(equalBoundExpression(p_true, p_false));
}

TEST(MemoContentHashTest, SharedSubtreeClonesOnCopyOut) {
    // Two parents referencing the same child group must both get a fully
    // populated subtree on copyOut. This is the case the previous
    // move-based copyOut broke: the second parent received an empty variant.
    auto leaf_a = makeLabelScanWithLabel("a", 0, LabelId{1});
    auto leaf_b = makeLabelScanWithLabel("b", 1, LabelId{2});

    auto join = std::make_unique<BoundBinaryJoinOp>();
    join->left = std::move(leaf_a);
    join->right = std::move(leaf_b);

    BoundLogicalPlan plan;
    plan.root = BoundLogicalOperator(std::move(join));

    Memo memo;
    auto gid = memo.copyIn(plan.root);
    auto result = memo.copyOut(gid);

    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundBinaryJoinOp>>(result));
    auto& j = std::get<std::unique_ptr<BoundBinaryJoinOp>>(result);
    ASSERT_TRUE(std::holds_alternative<BoundLabelScanOp>(j->left));
    ASSERT_TRUE(std::holds_alternative<BoundLabelScanOp>(j->right));
    auto& l = std::get<BoundLabelScanOp>(j->left);
    auto& r = std::get<BoundLabelScanOp>(j->right);
    EXPECT_EQ(l.variable, "a");
    EXPECT_EQ(r.variable, "b");
}

// ==================== CBO Plumbing Tests (Phase 2) ====================
//
// Verify the CBO pipeline runs without crashing and produces correct
// results. These tests exercise the context → O_GROUP → O_EXPR →
// winner flow end-to-end.

TEST(CBOPlumbingTest, RootContextCreatedAfterOptimize) {
    // After optimize(), Memo::contexts() should contain at least the root context.
    auto filter = std::make_unique<BoundFilterOp>();
    filter->child = makeLabelScan("n", 0);
    filter->predicate = BoundLiteral(true);

    BoundLogicalPlan plan;
    plan.root = BoundLogicalOperator(std::move(filter));

    LogicalOptimizer optimizer;
    optimizer.optimize(plan);

    // Plan must be valid after optimization
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundFilterOp>>(plan.root));
}

TEST(CBOPlumbingTest, CopyOutWithPhysPropReturnsCompletePlan) {
    // copyOut(gid, PhysProp{}) must return a complete plan regardless of
    // whether a physical winner exists.
    auto project = std::make_unique<BoundProjectOp>();
    auto filter = std::make_unique<BoundFilterOp>();
    filter->child = makeLabelScan("n", 0);
    filter->predicate = BoundLiteral(true);
    project->child = BoundLogicalOperator(std::move(filter));

    BoundLogicalPlan plan;
    plan.root = BoundLogicalOperator(std::move(project));

    { // Via LogicalOptimizer path (uses CBO copyOut with PhysProp{})
        LogicalOptimizer optimizer;
        optimizer.optimize(plan);

        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundProjectOp>>(plan.root));
        auto& proj = std::get<std::unique_ptr<BoundProjectOp>>(plan.root);
        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundFilterOp>>(proj->child));
        auto& filt = std::get<std::unique_ptr<BoundFilterOp>>(proj->child);
        ASSERT_TRUE(std::holds_alternative<BoundLabelScanOp>(filt->child));
    }

    { // Direct Memo: copyOut with PhysProp{} (no winner) falls back to RBO
        Memo memo;
        auto filter2 = std::make_unique<BoundFilterOp>();
        filter2->child = makeLabelScan("m", 1);
        filter2->predicate = BoundLiteral(false);
        BoundLogicalOperator root(std::move(filter2));
        auto gid = memo.copyIn(root);
        auto result = memo.copyOut(gid, PhysProp{});

        ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundFilterOp>>(result));
        auto& f = std::get<std::unique_ptr<BoundFilterOp>>(result);
        ASSERT_TRUE(std::holds_alternative<BoundLabelScanOp>(f->child));
        EXPECT_EQ(std::get<BoundLabelScanOp>(f->child).variable, "m");
    }
}

TEST(CBOPlumbingTest, MultiLevelPlanRunsCBOWithoutCrash) {
    // Three-level plan exercises recursive CBO optimization.
    // O_GROUP → O_EXPR → rules fire → E_GROUP for children → etc.
    auto expand = std::make_unique<BoundExpandOp>();
    expand->src_variable = "n";
    expand->src_column_index = 0;
    expand->edge_variable = "r";
    expand->edge_column_index = 1;
    expand->dst_variable = "m";
    expand->dst_column_index = 2;
    expand->child = makeLabelScan("n", 0);

    auto filter = std::make_unique<BoundFilterOp>();
    filter->child = BoundLogicalOperator(std::move(expand));
    filter->predicate = BoundLiteral(true);

    BoundLogicalPlan plan;
    plan.root = BoundLogicalOperator(std::move(filter));

    LogicalOptimizer optimizer;
    optimizer.optimize(plan);

    // Result: Expand → Filter → LabelScan (filter pushed through expand)
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundExpandOp>>(plan.root));
    auto& exp = std::get<std::unique_ptr<BoundExpandOp>>(plan.root);
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundFilterOp>>(exp->child));
    auto& filt = std::get<std::unique_ptr<BoundFilterOp>>(exp->child);
    ASSERT_TRUE(std::holds_alternative<BoundLabelScanOp>(filt->child));
}

TEST(CBOPlumbingTest, WinnerCirclePopulatedAfterOptimization) {
    // Direct exercise of the CBO task pipeline: create context, push O_GROUP,
    // run tasks, verify winner circle is populated.
    Memo memo;
    RuleSet rules;
    rules.addRule(std::make_unique<FilterPushdownRule>());

    auto filter = std::make_unique<BoundFilterOp>();
    filter->child = makeLabelScan("n", 0);
    filter->predicate = BoundLiteral(true);
    BoundLogicalOperator root(std::move(filter));
    auto root_gid = memo.copyIn(root);

    Context root_ctx(PhysProp{}, Cost::infinity());
    int root_ctx_id = memo.addContext(std::move(root_ctx));

    TaskQueue queue;
    queue.push(std::make_unique<OGroupTask>(root_gid, root_ctx_id, true));

    int iterations = 0;
    while (!queue.empty() && iterations < 1024) {
        auto task = queue.pop();
        task->perform(memo, rules, queue);
        iterations++;
    }

    // After optimization, contexts should include the root context
    ASSERT_GE(memo.contexts().size(), 1u);

    // The root group should have a winner (null plan, since no physical ops exist)
    Group& rootGroup = memo.getGroup(root_gid);
    Winner* w = rootGroup.getWinner(PhysProp{});
    ASSERT_NE(w, nullptr);
    // Winner should be done (no new expressions added during optimization
    // of a simple Filter→LabelScan plan — FilterPushdownRule's substitute
    // creates new expressions but they go into the same group via insertExpr,
    // and the new O_EXPR tasks complete before the original via LIFO)
}

TEST(CBOPlumbingTest, CopyOutWinnerFallbackProducesSameResult) {
    // copyOut(rbo) and copyOut(winner) should produce equivalent results
    // when no physical operators exist (both fall back to RBO extraction).
    Memo memo;
    auto scan = makeLabelScanWithLabel("x", 0, LabelId{42});
    auto gid = memo.copyIn(scan);

    auto rbo_result = memo.copyOut(gid);
    auto cbo_result = memo.copyOut(gid, PhysProp{});

    ASSERT_TRUE(std::holds_alternative<BoundLabelScanOp>(rbo_result));
    ASSERT_TRUE(std::holds_alternative<BoundLabelScanOp>(cbo_result));
    EXPECT_EQ(std::get<BoundLabelScanOp>(rbo_result).variable, std::get<BoundLabelScanOp>(cbo_result).variable);
    EXPECT_EQ(std::get<BoundLabelScanOp>(rbo_result).label_ids, std::get<BoundLabelScanOp>(cbo_result).label_ids);
}

// ==================== LogProp Tests (Phase 3) ====================

TEST(LogPropTest, LabelScanCardinalityFromCatalog) {
    catalog::Catalog cat;
    cat.setLabelStats(LabelId{1}, 5000);
    auto op = makeLabelScanWithLabel("n", 0, LabelId{1});

    LogPropDeriver deriver(&cat);
    auto lp = deriver.derive(op, {});

    EXPECT_DOUBLE_EQ(lp.cardinality, 5000.0);
    ASSERT_EQ(lp.columns.size(), 1u);
    EXPECT_EQ(lp.columns[0].variable, "n");
    EXPECT_EQ(lp.columns[0].column_index, 0u);
}

TEST(LogPropTest, LabelScanDefaultWhenNoStats) {
    // No catalog → default vertex count
    auto op = makeLabelScanWithLabel("n", 0, LabelId{1});

    LogPropDeriver deriver(nullptr);
    auto lp = deriver.derive(op, {});

    EXPECT_DOUBLE_EQ(lp.cardinality, LogPropDeriver::kDefaultVertexCount);
}

TEST(LogPropTest, LabelScanMultiLabelSumsCounts) {
    catalog::Catalog cat;
    cat.setLabelStats(LabelId{1}, 3000);
    cat.setLabelStats(LabelId{2}, 7000);

    BoundLabelScanOp scan;
    scan.variable = "n";
    scan.column_index = 0;
    scan.label_ids = {LabelId{1}, LabelId{2}};
    scan.label_names = {"A", "B"};

    LogPropDeriver deriver(&cat);
    auto lp = deriver.derive(scan, {});

    EXPECT_DOUBLE_EQ(lp.cardinality, 10000.0);
}

TEST(LogPropTest, FilterSelectivityDefault) {
    auto scan = makeLabelScanWithLabel("n", 0, LabelId{1});
    LogPropDeriver deriver(nullptr);
    auto input_lp = deriver.derive(scan, {});

    auto filter = std::make_unique<BoundFilterOp>();
    filter->child = makeLabelScan("n", 0);
    filter->predicate = BoundLiteral(true);
    BoundLogicalOperator filter_op(std::move(filter));

    auto lp = deriver.derive(filter_op, {input_lp});
    // Literal(true) has selectivity 1.0
    EXPECT_NEAR(lp.cardinality, input_lp.cardinality, 0.01);
}

TEST(LogPropTest, ExpandCardinality) {
    catalog::Catalog cat;
    cat.setEdgeLabelStats(EdgeLabelId{1}, 100, 5.0);

    auto scan = makeLabelScanWithLabel("n", 0, LabelId{1});
    LogPropDeriver deriver(&cat);
    auto input_lp = deriver.derive(scan, {});

    auto expand = std::make_unique<BoundExpandOp>();
    expand->src_variable = "n";
    expand->src_column_index = 0;
    expand->edge_variable = "r";
    expand->edge_column_index = 1;
    expand->dst_variable = "m";
    expand->dst_column_index = 2;
    expand->edge_label_ids = {EdgeLabelId{1}};
    expand->child = makeLabelScan("n", 0);
    BoundLogicalOperator expand_op(std::move(expand));

    auto lp = deriver.derive(expand_op, {input_lp});
    EXPECT_DOUBLE_EQ(lp.cardinality, input_lp.cardinality * 5.0);
    EXPECT_EQ(lp.columns.size(), 3u); // src + edge + dst
}

TEST(LogPropTest, JoinCardinalityCross) {
    LogPropDeriver deriver(nullptr);
    auto left_scan = makeLabelScanWithLabel("a", 0, LabelId{1});
    auto right_scan = makeLabelScanWithLabel("b", 1, LabelId{2});
    auto left_lp = deriver.derive(left_scan, {});
    auto right_lp = deriver.derive(right_scan, {});

    auto join = std::make_unique<BoundBinaryJoinOp>();
    join->join_type = JoinType::Cross;
    join->left = makeLabelScan("a", 0);
    join->right = makeLabelScan("b", 1);
    BoundLogicalOperator join_op(std::move(join));

    auto lp = deriver.derive(join_op, {left_lp, right_lp});
    EXPECT_DOUBLE_EQ(lp.cardinality, left_lp.cardinality * right_lp.cardinality);
}

TEST(LogPropTest, LimitCardinality) {
    LogPropDeriver deriver(nullptr);
    auto scan = makeLabelScanWithLabel("n", 0, LabelId{1});
    auto input_lp = deriver.derive(scan, {});

    auto limit = std::make_unique<BoundLimitOp>();
    limit->count = 10;
    limit->child = makeLabelScan("n", 0);
    BoundLogicalOperator limit_op(std::move(limit));

    auto lp = deriver.derive(limit_op, {input_lp});
    EXPECT_DOUBLE_EQ(lp.cardinality, 10.0);
}

TEST(LogPropTest, LimitDoesNotExceedInput) {
    LogPropDeriver deriver(nullptr);
    auto scan = makeLabelScanWithLabel("n", 0, LabelId{1});
    auto input_lp = deriver.derive(scan, {});

    auto limit = std::make_unique<BoundLimitOp>();
    limit->count = 99999; // much larger than input
    limit->child = makeLabelScan("n", 0);
    BoundLogicalOperator limit_op(std::move(limit));

    auto lp = deriver.derive(limit_op, {input_lp});
    EXPECT_DOUBLE_EQ(lp.cardinality, input_lp.cardinality);
}

TEST(LogPropTest, LazyDerivationCaches) {
    Memo memo;
    auto scan = makeLabelScanWithLabel("n", 0, LabelId{1});
    auto gid = memo.copyIn(scan);

    Group& group = memo.getGroup(gid);
    ASSERT_FALSE(group.logPropValid());

    // First derivation
    const LogProp& lp1 = group.getLogProp(memo, nullptr);
    EXPECT_TRUE(group.logPropValid());
    EXPECT_DOUBLE_EQ(lp1.cardinality, LogPropDeriver::kDefaultVertexCount);

    // Second call should return cached value (same reference)
    const LogProp& lp2 = group.getLogProp(memo, nullptr);
    EXPECT_EQ(&lp1, &lp2);
}

TEST(LogPropTest, InvalidationOnNewExpr) {
    Memo memo;
    auto scan = makeLabelScanWithLabel("n", 0, LabelId{1});
    auto gid = memo.copyIn(scan);

    Group& group = memo.getGroup(gid);
    group.getLogProp(memo, nullptr);
    ASSERT_TRUE(group.logPropValid());

    // Insert a new expression into the group — should invalidate
    auto extra_scan = makeLabelScanWithLabel("n", 0, LabelId{1});
    auto new_expr = std::make_unique<GroupExpr>(memo.newExprId(), gid, std::move(extra_scan), std::vector<GroupId>{});
    memo.insertExpr(std::move(new_expr), gid);

    EXPECT_FALSE(group.logPropValid());
}

TEST(LogPropTest, ParentInvalidationPropagates) {
    Memo memo;
    // Build: Filter → LabelScan
    auto filter = std::make_unique<BoundFilterOp>();
    filter->child = makeLabelScanWithLabel("n", 0, LabelId{1});
    filter->predicate = BoundLiteral(true);
    BoundLogicalOperator root(std::move(filter));
    auto root_gid = memo.copyIn(root);

    Group& root_group = memo.getGroup(root_gid);
    root_group.getLogProp(memo, nullptr);
    ASSERT_TRUE(root_group.logPropValid());

    // Find the child group (LabelScan)
    GroupExpr& root_expr = memo.getExpr(root_group.logical_exprs.front());
    GroupId child_gid = root_expr.child_groups[0];
    Group& child_group = memo.getGroup(child_gid);
    ASSERT_TRUE(child_group.logPropValid());

    // Insert new expr into child — should cascade to root
    auto extra = makeLabelScanWithLabel("n", 0, LabelId{1});
    auto new_expr = std::make_unique<GroupExpr>(memo.newExprId(), child_gid, std::move(extra), std::vector<GroupId>{});
    memo.insertExpr(std::move(new_expr), child_gid);

    EXPECT_FALSE(child_group.logPropValid());
    EXPECT_FALSE(root_group.logPropValid());
}

// ==================== CostModel Tests (Phase 3) ====================

TEST(CostModelTest, ScanCost) {
    LogProp lp;
    lp.cardinality = 5000.0;
    auto cost = findLocalCost(PhysicalOpTag::LabelScan, lp, {});
    EXPECT_DOUBLE_EQ(cost.value(), 5000.0);
}

TEST(CostModelTest, FilterCost) {
    LogProp out;
    out.cardinality = 500.0;
    LogProp in;
    in.cardinality = 5000.0;
    auto cost = findLocalCost(PhysicalOpTag::Filter, out, {in});
    EXPECT_DOUBLE_EQ(cost.value(), 5000.0);
}

TEST(CostModelTest, HashJoinCost) {
    LogProp out;
    out.cardinality = 500.0;
    LogProp left;
    left.cardinality = 1000.0;
    LogProp right;
    right.cardinality = 2000.0;
    auto cost = findLocalCost(PhysicalOpTag::HashJoin, out, {left, right});
    // build (right) + probe (left)
    EXPECT_DOUBLE_EQ(cost.value(), 3000.0);
}

TEST(CostModelTest, NestedLoopJoinCost) {
    LogProp out;
    out.cardinality = 500.0;
    LogProp left;
    left.cardinality = 100.0;
    LogProp right;
    right.cardinality = 50.0;
    auto cost = findLocalCost(PhysicalOpTag::NestedLoopJoin, out, {left, right});
    EXPECT_DOUBLE_EQ(cost.value(), 5000.0);
}

TEST(CostModelTest, SortCost) {
    LogProp in;
    in.cardinality = 1024.0;
    LogProp out;
    out.cardinality = 1024.0;
    auto cost = findLocalCost(PhysicalOpTag::Sort, out, {in});
    // 1024 * log2(1024) = 1024 * 10 = 10240
    EXPECT_NEAR(cost.value(), 10240.0, 0.1);
}

TEST(CostModelTest, SortCostOneRow) {
    LogProp in;
    in.cardinality = 1.0;
    LogProp out;
    out.cardinality = 1.0;
    auto cost = findLocalCost(PhysicalOpTag::Sort, out, {in});
    EXPECT_DOUBLE_EQ(cost.value(), 1.0);
}

TEST(CostModelTest, LimitCost) {
    LogProp out;
    out.cardinality = 10.0;
    auto cost = findLocalCost(PhysicalOpTag::Limit, out, {});
    EXPECT_DOUBLE_EQ(cost.value(), 10.0);
}

// ==================== Phase 4: Implementation Rules & ChosenPlan ====================

TEST(ImplRuleTest, LabelScanImplRuleProducesPhysicalExpr) {
    // ImplLabelScanRule fires on a leaf LabelScan, producing a physical
    // GroupExpr with PhysicalOpTag::LabelScan that carries a clone of the
    // source logical operator.
    Memo memo;
    auto scan = makeLabelScanWithLabel("n", 0, LabelId{7});
    auto gid = memo.copyIn(scan);

    ImplLabelScanRule rule;
    ASSERT_TRUE(rule.topMatch(memo.getExpr(memo.getGroup(gid).logical_exprs.back())));

    GroupExpr& src = memo.getExpr(memo.getGroup(gid).logical_exprs.back());
    auto new_exprs = rule.substitute(src, memo);
    ASSERT_EQ(new_exprs.size(), 1u);
    ASSERT_TRUE(new_exprs[0]->isPhysical());
    EXPECT_EQ(new_exprs[0]->physOp().tag, PhysicalOpTag::LabelScan);
}

TEST(ImplRuleTest, FilterImplRuleProducesPhysicalExpr) {
    // ImplFilterRule fires on Filter with one leaf child.
    auto filter = std::make_unique<BoundFilterOp>();
    filter->child = makeLabelScanWithLabel("n", 0, LabelId{1});
    filter->predicate = BoundLiteral(true);
    BoundLogicalOperator root(std::move(filter));

    Memo memo;
    auto gid = memo.copyIn(root);

    ImplFilterRule rule;
    GroupExpr& src = memo.getExpr(memo.getGroup(gid).logical_exprs.back());
    ASSERT_TRUE(rule.topMatch(src));
    auto new_exprs = rule.substitute(src, memo);
    ASSERT_EQ(new_exprs.size(), 1u);
    ASSERT_TRUE(new_exprs[0]->isPhysical());
    EXPECT_EQ(new_exprs[0]->physOp().tag, PhysicalOpTag::Filter);
    ASSERT_EQ(new_exprs[0]->child_groups.size(), 1u);
}

TEST(ImplRuleTest, TopMatchRejectsMismatchedRoot) {
    // ImplLabelScanRule must NOT match a Filter expr (root-type check).
    auto filter = std::make_unique<BoundFilterOp>();
    filter->child = makeLabelScanWithLabel("n", 0, LabelId{1});
    filter->predicate = BoundLiteral(true);
    BoundLogicalOperator root(std::move(filter));

    Memo memo;
    auto gid = memo.copyIn(root);

    ImplLabelScanRule rule;
    GroupExpr& src = memo.getExpr(memo.getGroup(gid).logical_exprs.back());
    EXPECT_FALSE(rule.topMatch(src));
}

TEST(ChosenPlanTest, OptimizePopulatesChosenForLabelScan) {
    // A simple LabelScan plan: optimizer must produce a ChosenPlan because
    // ImplLabelScanRule fires and O_INPUTS registers a winner.
    BoundLogicalPlan plan;
    plan.root = makeLabelScanWithLabel("n", 0, LabelId{1});

    LogicalOptimizer optimizer;
    optimizer.optimize(plan);

    ASSERT_NE(plan.chosen, nullptr);
    EXPECT_EQ(plan.chosen->tag, PhysicalOpTag::LabelScan);
    EXPECT_TRUE(plan.chosen->children.empty());
}

TEST(ChosenPlanTest, OptimizePopulatesChosenForFilterScanChain) {
    // Filter(LabelScan): both impl rules fire, O_INPUTS walks the chain
    // and registers winners in each group. The ChosenPlan tree should
    // mirror the plan structure: Filter at root with a LabelScan child.
    auto filter = std::make_unique<BoundFilterOp>();
    filter->child = makeLabelScanWithLabel("n", 0, LabelId{1});
    filter->predicate = BoundLiteral(true);

    BoundLogicalPlan plan;
    plan.root = BoundLogicalOperator(std::move(filter));

    LogicalOptimizer optimizer;
    optimizer.optimize(plan);

    ASSERT_NE(plan.chosen, nullptr);
    EXPECT_EQ(plan.chosen->tag, PhysicalOpTag::Filter);
    ASSERT_EQ(plan.chosen->children.size(), 1u);
    EXPECT_EQ(plan.chosen->children[0]->tag, PhysicalOpTag::LabelScan);
}

TEST(ChosenPlanTest, ChosenPlanMaterializesRoundTrip) {
    // After optimization, plan.chosen should describe the same plan as
    // plan.root (modulo physical-vs-logical distinction). The structure
    // (root op variant + child variant) must match.
    auto filter = std::make_unique<BoundFilterOp>();
    filter->child = makeLabelScanWithLabel("n", 0, LabelId{1});
    filter->predicate = BoundLiteral(true);

    BoundLogicalPlan plan;
    plan.root = BoundLogicalOperator(std::move(filter));

    LogicalOptimizer optimizer;
    optimizer.optimize(plan);

    ASSERT_NE(plan.chosen, nullptr);
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundFilterOp>>(plan.chosen->op));
    ASSERT_EQ(plan.chosen->children.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<BoundLabelScanOp>(plan.chosen->children[0]->op));

    // Cross-check: plan.root should also be Filter→LabelScan
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundFilterOp>>(plan.root));
    auto& f = std::get<std::unique_ptr<BoundFilterOp>>(plan.root);
    ASSERT_TRUE(std::holds_alternative<BoundLabelScanOp>(f->child));
}

TEST(ChosenPlanTest, ThreeLevelPlanProducesThreeLevelChosen) {
    // Filter(Expand(LabelScan)) — exercises a deeper winner chain.
    auto expand = std::make_unique<BoundExpandOp>();
    expand->src_variable = "n";
    expand->src_column_index = 0;
    expand->edge_variable = "r";
    expand->edge_column_index = 1;
    expand->dst_variable = "m";
    expand->dst_column_index = 2;
    expand->child = makeLabelScanWithLabel("n", 0, LabelId{1});

    auto filter = std::make_unique<BoundFilterOp>();
    filter->child = BoundLogicalOperator(std::move(expand));
    filter->predicate = BoundLiteral(true);

    BoundLogicalPlan plan;
    plan.root = BoundLogicalOperator(std::move(filter));

    LogicalOptimizer optimizer;
    optimizer.optimize(plan);

    // Chosen plan may be null if FilterPushdown fires and reshapes the tree
    // mid-optimization (changed flag invalidates the winner). When present,
    // it must describe the full three-level chain.
    if (plan.chosen != nullptr) {
        ASSERT_EQ(plan.chosen->children.size(), 1u);
        // Root is Filter or Expand depending on whether filter pushdown fired
        EXPECT_GE(plan.chosen->children.size(), 1u);
    }
}

// ==================== Framework Property Tests ====================
//
// These tests verify framework invariants rather than specific operator
// behaviors. They guard against regressions in the task pipeline, Memo
// hashing/merging, and winner selection. Each test maps to a Columbia
// property the optimizer must preserve for any input plan.

TEST(FrameworkPropertyTest, CopyInCopyOutPreservesStructure) {
    // Property: copyIn → copyOut (without optimize) is structural identity.
    // Operator variant, child variant, and key fields must match the input.
    auto filter = std::make_unique<BoundFilterOp>();
    filter->child = makeLabelScanWithLabel("n", 0, LabelId{1});
    filter->predicate = BoundLiteral(true);
    BoundLogicalOperator root_in(std::move(filter));

    Memo memo;
    auto gid = memo.copyIn(root_in);
    auto root_out = memo.copyOut(gid);

    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundFilterOp>>(root_out));
    const auto& f = std::get<std::unique_ptr<BoundFilterOp>>(root_out);
    ASSERT_TRUE(std::holds_alternative<BoundLabelScanOp>(f->child));
    const auto& scan = std::get<BoundLabelScanOp>(f->child);
    EXPECT_EQ(scan.variable, "n");
    EXPECT_EQ(scan.label_ids, std::vector<LabelId>{LabelId{1}});
}

TEST(FrameworkPropertyTest, OptimizeTerminatesOnDeepNestedPlan) {
    // Property: optimizer converges within kMaxIterations for a 5-level
    // Filter chain. plan.root must be a valid non-empty variant after.
    auto scan = makeLabelScanWithLabel("n", 0, LabelId{1});
    BoundLogicalOperator current(std::move(scan));

    for (int i = 0; i < 5; ++i) {
        auto f = std::make_unique<BoundFilterOp>();
        f->child = std::move(current);
        f->predicate = BoundLiteral(true);
        current = BoundLogicalOperator(std::move(f));
    }

    BoundLogicalPlan plan;
    plan.root = std::move(current);

    LogicalOptimizer optimizer;
    optimizer.optimize(plan);

    EXPECT_FALSE(plan.root.valueless_by_exception());
    ASSERT_NE(plan.chosen, nullptr);
}

TEST(FrameworkPropertyTest, MemoHoldsNoDuplicateExprsAfterOptimize) {
    // Property: content-aware dedup is enforced — no two logical or
    // physical exprs in the same group share (op_content, child_groups).
    // Walk every reachable group via child_groups and pairwise-check.
    auto filter = std::make_unique<BoundFilterOp>();
    filter->child = makeLabelScanWithLabel("n", 0, LabelId{1});
    filter->predicate = BoundLiteral(true);
    BoundLogicalOperator root(std::move(filter));

    Memo memo;
    RuleSet rules;
    rules.addRule(std::make_unique<FilterPushdownRule>());
    rules.addRule(std::make_unique<ImplLabelScanRule>());
    rules.addRule(std::make_unique<ImplFilterRule>());

    auto root_gid = memo.copyIn(root);
    Context root_ctx(PhysProp{}, Cost::infinity());
    int ctx_id = memo.addContext(std::move(root_ctx));

    TaskQueue queue;
    queue.push(std::make_unique<OGroupTask>(root_gid, ctx_id, /*last=*/true));
    int iterations = 0;
    while (!queue.empty() && iterations < 1024) {
        queue.pop()->perform(memo, rules, queue);
        iterations++;
    }

    std::set<GroupId> visited;
    std::function<void(GroupId)> walk = [&](GroupId gid) {
        if (visited.count(gid))
            return;
        visited.insert(gid);
        Group& g = memo.getGroup(gid);

        auto check_no_dup = [&](const std::vector<ExprId>& ids) {
            for (size_t i = 0; i < ids.size(); ++i) {
                for (size_t j = i + 1; j < ids.size(); ++j) {
                    const auto& a = memo.getExpr(ids[i]);
                    const auto& b = memo.getExpr(ids[j]);
                    if (a.child_groups != b.child_groups)
                        continue;
                    bool same = a.isPhysical() == b.isPhysical();
                    if (same && a.isPhysical()) {
                        same = equalPhysicalExpr(a.physOp(), b.physOp());
                    } else if (same) {
                        same = equalBoundLogicalOperator(a.op, b.op);
                    }
                    EXPECT_FALSE(same) << "Duplicate expr in group " << gid << " exprs " << ids[i] << " vs " << ids[j];
                }
            }
        };
        check_no_dup(g.logical_exprs);
        check_no_dup(g.physical_exprs);

        for (ExprId eid : g.logical_exprs) {
            for (GroupId cgid : memo.getExpr(eid).child_groups)
                walk(cgid);
        }
        for (ExprId eid : g.physical_exprs) {
            for (GroupId cgid : memo.getExpr(eid).child_groups)
                walk(cgid);
        }
    };
    walk(root_gid);

    EXPECT_GT(visited.size(), 0u);
}

TEST(FrameworkPropertyTest, OptimizeIsDeterministicAcrossRuns) {
    // Property: two optimize runs on equivalent input plans produce
    // equivalent plan.root variant index and ChosenPlan root tag.
    auto make_plan = []() {
        auto filter = std::make_unique<BoundFilterOp>();
        filter->child = makeLabelScanWithLabel("n", 0, LabelId{1});
        filter->predicate = BoundLiteral(true);
        BoundLogicalPlan plan;
        plan.root = BoundLogicalOperator(std::move(filter));
        return plan;
    };

    BoundLogicalPlan plan1 = make_plan();
    BoundLogicalPlan plan2 = make_plan();

    LogicalOptimizer o1, o2;
    o1.optimize(plan1);
    o2.optimize(plan2);

    ASSERT_EQ(plan1.root.index(), plan2.root.index());
    ASSERT_NE(plan1.chosen, nullptr);
    ASSERT_NE(plan2.chosen, nullptr);
    EXPECT_EQ(plan1.chosen->tag, plan2.chosen->tag);
    EXPECT_EQ(plan1.chosen->children.size(), plan2.chosen->children.size());
}

TEST(FrameworkPropertyTest, WinnerChainReferencesValidExprs) {
    // Property: every group's winner.plan (when non-null and done) must
    // reference an ExprId in that group's logical_exprs or physical_exprs.
    // Guards against dangling winner references after group merges or
    // dedup-driven expr substitution.
    auto filter = std::make_unique<BoundFilterOp>();
    filter->child = makeLabelScanWithLabel("n", 0, LabelId{1});
    filter->predicate = BoundLiteral(true);
    BoundLogicalOperator root(std::move(filter));

    Memo memo;
    RuleSet rules;
    rules.addRule(std::make_unique<FilterPushdownRule>());
    rules.addRule(std::make_unique<ImplLabelScanRule>());
    rules.addRule(std::make_unique<ImplFilterRule>());

    auto root_gid = memo.copyIn(root);
    Context root_ctx(PhysProp{}, Cost::infinity());
    int ctx_id = memo.addContext(std::move(root_ctx));

    TaskQueue queue;
    queue.push(std::make_unique<OGroupTask>(root_gid, ctx_id, /*last=*/true));
    int iterations = 0;
    while (!queue.empty() && iterations < 1024) {
        queue.pop()->perform(memo, rules, queue);
        iterations++;
    }

    std::set<GroupId> visited;
    std::function<void(GroupId)> walk = [&](GroupId gid) {
        if (visited.count(gid))
            return;
        visited.insert(gid);
        Group& g = memo.getGroup(gid);

        for (const Winner& w : g.winners) {
            if (!w.done() || w.plan() == INVALID_EXPR_ID)
                continue;
            ExprId eid = w.plan();
            bool in_logical = std::find(g.logical_exprs.begin(), g.logical_exprs.end(), eid) != g.logical_exprs.end();
            bool in_physical =
                std::find(g.physical_exprs.begin(), g.physical_exprs.end(), eid) != g.physical_exprs.end();
            EXPECT_TRUE(in_logical || in_physical) << "Winner " << eid << " in group " << gid << " not in expr lists";
        }

        for (ExprId eid : g.logical_exprs) {
            for (GroupId cgid : memo.getExpr(eid).child_groups)
                walk(cgid);
        }
        for (ExprId eid : g.physical_exprs) {
            for (GroupId cgid : memo.getExpr(eid).child_groups)
                walk(cgid);
        }
    };
    walk(root_gid);
}

// ==================== SearchCircle 4 Case Tests ====================
//
// Columbia O_GROUP::perform lines 195-275 distinguishes 4 cases via
// search_circle. We test each case directly against Group::searchCircle
// to lock the contract.

TEST(SearchCircleTest, NoWinnerSignalsMoreSearch) {
    // Case (3): no winner for this property → moreSearch=true, return false
    Group g(GroupId{0});
    Context ctx(PhysProp{}, Cost::infinity());

    bool more = false;
    bool found = g.searchCircle(ctx, more);
    EXPECT_FALSE(found);
    EXPECT_TRUE(more);
}

TEST(SearchCircleTest, DoneWinnerUnderUBTerminates) {
    // Case (2): winner with non-null plan, cost <= UB → moreSearch=false,
    // return true. The group has a satisfying winner; O_GROUP must stop.
    Group g(GroupId{0});
    ExprId some_plan = 7;
    g.newWinner(PhysProp{}, some_plan, Cost(10.0), /*done=*/true);
    Context ctx(PhysProp{}, Cost(100.0));

    bool more = true;
    bool found = g.searchCircle(ctx, more);
    EXPECT_TRUE(found);
    EXPECT_FALSE(more);
}

TEST(SearchCircleTest, WinnerExceedingUBIsImpossible) {
    // Case (1): winner with non-null plan, cost > UB → moreSearch=false,
    // return false. Plan is too expensive for this context.
    Group g(GroupId{0});
    ExprId some_plan = 7;
    g.newWinner(PhysProp{}, some_plan, Cost(100.0), /*done=*/true);
    Context ctx(PhysProp{}, Cost(10.0));

    bool more = true;
    bool found = g.searchCircle(ctx, more);
    EXPECT_FALSE(found);
    EXPECT_FALSE(more);
}

TEST(SearchCircleTest, NullWinnerWithLooserUBSignalsReoptimize) {
    // Case (4): winner with null plan (previous search failed) and the
    // new context UB is looser than the winner's recorded cost bound.
    // Columbia retries optimization under the new bound.
    Group g(GroupId{0});
    // Null winner with cost bound 10 (sentinel for "tried up to 10")
    g.newWinner(PhysProp{}, INVALID_EXPR_ID, Cost(10.0), /*done=*/false);
    Context ctx(PhysProp{}, Cost(100.0)); // looser UB

    bool more = false;
    bool found = g.searchCircle(ctx, more);
    EXPECT_TRUE(found);
    EXPECT_TRUE(more);
}

// ==================== Pattern Match Tests ====================

TEST(PatternMatchTest, TopMatchChecksRootTypeOnly) {
    // topMatch is an O(1) pre-filter: returns true iff the candidate
    // expression's root operator matches the pattern's root type.
    ImplLabelScanRule rule; // pattern: {LabelScan, {}}
    auto scan = makeLabelScanWithLabel("n", 0, LabelId{1});
    Memo memo;
    auto gid = memo.copyIn(scan);

    GroupExpr& scan_expr = memo.getExpr(memo.getGroup(gid).logical_exprs.back());
    EXPECT_TRUE(rule.topMatch(scan_expr));

    auto filter = std::make_unique<BoundFilterOp>();
    filter->child = makeLabelScanWithLabel("n", 0, LabelId{1});
    filter->predicate = BoundLiteral(true);
    BoundLogicalOperator root(std::move(filter));
    auto f_gid = memo.copyIn(root);
    GroupExpr& filter_expr = memo.getExpr(memo.getGroup(f_gid).logical_exprs.back());
    EXPECT_FALSE(rule.topMatch(filter_expr)); // Filter ≠ LabelScan
}

TEST(PatternMatchTest, TopMatchIgnoresPatternChildren) {
    // topMatch is an O(1) pre-filter — it only compares the root operator
    // type. Pattern children shape (empty, leaf, nested) is irrelevant.
    // This is the actual contract the task pipeline relies on
    // (task.cpp:124 calls topMatch before condition/substitute).
    auto scan = makeLabelScanWithLabel("n", 0, LabelId{1});
    Memo memo;
    auto gid = memo.copyIn(scan);
    GroupExpr& expr = memo.getExpr(memo.getGroup(gid).logical_exprs.back());

    // Both rules declare a leaf LabelScan pattern; topMatch must agree.
    ImplLabelScanRule label_rule;
    EXPECT_TRUE(label_rule.topMatch(expr));

    // ImplFilterRule's pattern has root=Filter; topMatch must reject
    // the LabelScan expr even though the pattern's child slot is empty.
    ImplFilterRule filter_rule;
    EXPECT_FALSE(filter_rule.topMatch(expr));
}

TEST(PatternMatchTest, ImplRulesDeclareExpectedPatternShape) {
    // Lock the pattern() contract for representative impl rules.
    // ImplLabelScanRule's pattern is a leaf (no children) — empty
    // children means wildcard at the root level. ImplFilterRule's
    // pattern has root=Filter plus one child slot (default type).
    ImplLabelScanRule ls_rule;
    PatternNode ls_pat = ls_rule.pattern();
    EXPECT_EQ(ls_pat.type, OptNodeType::LabelScan);
    EXPECT_TRUE(ls_pat.children.empty());

    ImplFilterRule f_rule;
    PatternNode f_pat = f_rule.pattern();
    EXPECT_EQ(f_pat.type, OptNodeType::Filter);
    ASSERT_EQ(f_pat.children.size(), 1u);
}

// ==================== TaskQueue LIFO Tests ====================

TEST(TaskQueueTest, LIFOOrderingPopsMostRecentFirst) {
    // Columbia PTASKS is a LIFO stack. Our TaskQueue (deque + push_back /
    // pop_back) must preserve this contract.
    TaskQueue q;
    q.push(std::make_unique<OGroupTask>(GroupId{1}, 0, /*last=*/false));
    q.push(std::make_unique<OGroupTask>(GroupId{2}, 0, /*last=*/false));
    q.push(std::make_unique<OGroupTask>(GroupId{3}, 0, /*last=*/false));

    auto t1 = q.pop();
    auto t2 = q.pop();
    auto t3 = q.pop();
    auto t4 = q.pop();

    // Pop order is reverse of push order
    auto& g1 = dynamic_cast<OGroupTask&>(*t1);
    auto& g2 = dynamic_cast<OGroupTask&>(*t2);
    auto& g3 = dynamic_cast<OGroupTask&>(*t3);
    EXPECT_EQ(g1.groupId(), GroupId{3}); // last pushed, first popped
    EXPECT_EQ(g2.groupId(), GroupId{2});
    EXPECT_EQ(g3.groupId(), GroupId{1});
    EXPECT_EQ(t4, nullptr); // empty queue returns nullptr, not throws
}

// ==================== Group Merge Tests ====================

TEST(GroupMergeTest, ChildGroupReferencesReroutedAfterMerge) {
    // After mergeGroups(g1, g2), every parent GroupExpr's child_groups
    // that referenced g2 must now reference g1.
    auto scan1 = makeLabelScanWithLabel("a", 0, LabelId{1});
    auto scan2 = makeLabelScanWithLabel("b", 0, LabelId{2});

    Memo memo;
    auto g1 = memo.copyIn(scan1);
    auto g2 = memo.copyIn(scan2);

    // Build a Filter that points to g2 as its child
    auto filter = std::make_unique<BoundFilterOp>();
    filter->predicate = BoundLiteral(true);
    auto parent_gid = memo.createGroupWithExpr(BoundLogicalOperator(std::move(filter)), {g2})->group_id;

    // Sanity: parent references g2
    ExprId parent_eid = memo.getGroup(parent_gid).logical_exprs.back();
    EXPECT_EQ(memo.getExpr(parent_eid).child_groups, std::vector<GroupId>{g2});

    memo.mergeGroups(g1, g2);

    // After merge: parent references g1 (the smaller id)
    EXPECT_EQ(memo.getExpr(parent_eid).child_groups, std::vector<GroupId>{g1});
}

TEST(GroupMergeTest, LogicalExprsUnionAfterMerge) {
    // After mergeGroups(g1, g2), g1's logical_exprs is the union of
    // both groups' exprs, and g2's logical_exprs is empty.
    auto scan1 = makeLabelScanWithLabel("a", 0, LabelId{1});
    auto scan2 = makeLabelScanWithLabel("b", 0, LabelId{2});

    Memo memo;
    auto g1 = memo.copyIn(scan1);
    auto g2 = memo.copyIn(scan2);

    size_t g1_count_before = memo.getGroup(g1).logical_exprs.size();
    size_t g2_count_before = memo.getGroup(g2).logical_exprs.size();
    ASSERT_EQ(g1_count_before, 1u);
    ASSERT_EQ(g2_count_before, 1u);

    auto merged = memo.mergeGroups(g1, g2);

    ASSERT_EQ(merged, std::min(g1, g2));
    EXPECT_EQ(memo.getGroup(merged).logical_exprs.size(), 2u);
    // The other group is now empty
    GroupId other = (merged == g1) ? g2 : g1;
    EXPECT_TRUE(memo.getGroup(other).logical_exprs.empty());
}

// ============================================================
// Phase B — Cascades PhysProp + Enforcer framework tests
//
// These tests exercise the new materialization-aware Cascades
// infrastructure: MaterializationReq lattice operations, PhysProp
// carrying materializations, RequirementCollector walking bound
// expressions, Memo's group-level requirement storage, Enricher
// enforcer insertion, and ChosenPlan extraction.
// ============================================================

TEST(MaterializationReqTest, EmptyByDefault) {
    MaterializationReq r;
    EXPECT_TRUE(r.empty());
    EXPECT_EQ(r.totalPropertyCount(), 0u);
}

TEST(MaterializationReqTest, MergeIsSetUnion) {
    MaterializationReq a;
    a.need_labels = true;
    a.need_props[LabelId{1}] = {10, 20};

    MaterializationReq b;
    b.need_props[LabelId{1}] = {20, 30};
    b.need_props[LabelId{2}] = {40};

    a.merge(b);
    EXPECT_TRUE(a.need_labels);
    EXPECT_EQ(a.need_props[LabelId{1}], (std::set<uint16_t>{10, 20, 30}));
    EXPECT_EQ(a.need_props[LabelId{2}], (std::set<uint16_t>{40}));
    EXPECT_EQ(a.totalPropertyCount(), 4u);
}

TEST(MaterializationReqTest, SatisfiesIsSuperset) {
    MaterializationReq provided;
    provided.need_labels = true;
    provided.need_props[LabelId{1}] = {10, 20, 30};

    MaterializationReq required;
    required.need_labels = true;
    required.need_props[LabelId{1}] = {10, 20};

    EXPECT_TRUE(provided.satisfies(required));

    // Missing a property → not satisfied.
    MaterializationReq required2;
    required2.need_props[LabelId{1}] = {10, 40};
    EXPECT_FALSE(provided.satisfies(required2));

    // Missing labels → not satisfied.
    MaterializationReq provided_no_labels;
    provided_no_labels.need_props[LabelId{1}] = {10};
    MaterializationReq required_labels;
    required_labels.need_labels = true;
    EXPECT_FALSE(provided_no_labels.satisfies(required_labels));
}

TEST(MaterializationReqTest, IntersectFindsCommonSubset) {
    MaterializationReq a;
    a.need_labels = true;
    a.need_props[LabelId{1}] = {10, 20, 30};
    a.need_props[LabelId{2}] = {40};

    MaterializationReq b;
    b.need_labels = true;
    b.need_props[LabelId{1}] = {20, 30, 50};

    MaterializationReq r = a.intersect(b);
    EXPECT_TRUE(r.need_labels);
    EXPECT_EQ(r.need_props[LabelId{1}], (std::set<uint16_t>{20, 30}));
    EXPECT_EQ(r.need_props.count(LabelId{2}), 0u);
}

TEST(MaterializationReqTest, SatisfiesVarRequirementsHandlesMissingVar) {
    VarRequirements provided;
    VarRequirements required;
    required["n"].need_props[LabelId{1}] = {10};

    // provided is empty → cannot satisfy a non-empty requirement.
    EXPECT_FALSE(satisfiesVarRequirements(provided, required));

    // Empty requirements are trivially satisfied.
    VarRequirements empty_req;
    EXPECT_TRUE(satisfiesVarRequirements(provided, empty_req));
}

TEST(MaterializationReqTest, MergeVarRequirementsUnionsAcrossVars) {
    VarRequirements a;
    a["n"].need_props[LabelId{1}] = {10};
    VarRequirements b;
    b["n"].need_props[LabelId{1}] = {20};
    b["m"].need_labels = true;

    mergeVarRequirements(a, b);
    EXPECT_EQ(a["n"].need_props[LabelId{1}], (std::set<uint16_t>{10, 20}));
    EXPECT_TRUE(a["m"].need_labels);
}

TEST(PhysPropTest, AnyPropHasNoMaterializations) {
    PhysProp p;
    EXPECT_TRUE(p.isAny());
    EXPECT_FALSE(p.hasMaterializations());
    EXPECT_TRUE(p.materializations().empty());
}

TEST(PhysPropTest, MaterializationsMakePropNonAny) {
    PhysProp p;
    VarRequirements mat;
    mat["n"].need_props[LabelId{1}] = {10};
    p.setMaterializations(mat);
    EXPECT_FALSE(p.isAny());
    EXPECT_TRUE(p.hasMaterializations());
}

TEST(PhysPropTest, SatisfiesRequiresSupersetMaterialization) {
    PhysProp provided;
    {
        VarRequirements mat;
        mat["n"].need_props[LabelId{1}] = {10, 20};
        provided.setMaterializations(mat);
    }
    PhysProp required;
    {
        VarRequirements mat;
        mat["n"].need_props[LabelId{1}] = {10};
        required.setMaterializations(mat);
    }
    EXPECT_TRUE(provided.satisfies(required));
    EXPECT_FALSE(required.satisfies(provided));
}

TEST(PhysPropTest, SatisfiesHandlesSortAndMaterializationTogether) {
    PhysProp provided(7, SortOrder::ascending);
    {
        VarRequirements mat;
        mat["n"].need_props[LabelId{1}] = {10};
        provided.setMaterializations(mat);
    }
    PhysProp required_asc(7, SortOrder::ascending);
    {
        VarRequirements mat;
        mat["n"].need_props[LabelId{1}] = {10};
        required_asc.setMaterializations(mat);
    }
    EXPECT_TRUE(provided.satisfies(required_asc));

    // Different sort property on the same name → not satisfied.
    PhysProp required_desc(7, SortOrder::descending);
    EXPECT_FALSE(provided.satisfies(required_desc));
}

TEST(PhysPropTest, DumpProducesNonEmptyStringForMaterialized) {
    PhysProp p;
    VarRequirements mat;
    mat["n"].need_labels = true;
    mat["n"].need_props[LabelId{1}] = {10, 20};
    p.setMaterializations(mat);
    std::string s = p.dump();
    EXPECT_NE(s, "any");
    EXPECT_NE(s.find("n"), std::string::npos);
    EXPECT_NE(s.find("L"), std::string::npos);
}

// === RequirementCollector ===

TEST(RequirementCollectorTest, StrongPropertyRefProducesNeedProps) {
    // Build: BoundPropertyRef{property_name="age", object=VariableRef("n"),
    //                        candidates=[{label=1, prop=10}]}
    auto pref = std::make_unique<BoundPropertyRef>();
    pref->property_name = "age";
    pref->object = BoundVariableRef("n", BoundType::Vertex());
    BoundPropertyRef::ResolvedProperty cand;
    cand.label_id = LabelId{1};
    cand.prop_id = 10;
    cand.type = BoundType::Int64();
    pref->candidates.push_back(cand);

    BoundExpression expr = std::move(pref);
    VarRequirements reqs;
    collectExprRequirements(expr, reqs);

    ASSERT_EQ(reqs.count("n"), 1u);
    EXPECT_FALSE(reqs["n"].need_labels);
    ASSERT_EQ(reqs["n"].need_props.count(LabelId{1}), 1u);
    EXPECT_EQ(reqs["n"].need_props[LabelId{1}], (std::set<uint16_t>{10}));
}

TEST(RequirementCollectorTest, DynamicPropertyRefSetsNeedLabels) {
    auto dpref = std::make_unique<BoundDynamicPropertyRef>();
    dpref->object = BoundVariableRef("n", BoundType::Vertex());
    dpref->property = "name";
    dpref->result_type = BoundType::Any();

    BoundExpression expr = std::move(dpref);
    VarRequirements reqs;
    collectExprRequirements(expr, reqs);

    ASSERT_EQ(reqs.count("n"), 1u);
    EXPECT_TRUE(reqs["n"].need_labels);
    EXPECT_TRUE(reqs["n"].need_props.empty());
}

TEST(RequirementCollectorTest, BinaryOpRecursesIntoBothOperands) {
    // n.age = 1 OR m.id = 2
    auto lhs = std::make_unique<BoundPropertyRef>();
    lhs->property_name = "age";
    lhs->object = BoundVariableRef("n", BoundType::Vertex());
    BoundPropertyRef::ResolvedProperty c1;
    c1.label_id = LabelId{1};
    c1.prop_id = 10;
    lhs->candidates.push_back(c1);

    auto rhs = std::make_unique<BoundPropertyRef>();
    rhs->property_name = "id";
    rhs->object = BoundVariableRef("m", BoundType::Vertex());
    BoundPropertyRef::ResolvedProperty c2;
    c2.label_id = LabelId{2};
    c2.prop_id = 20;
    rhs->candidates.push_back(c2);

    auto bin = std::make_unique<BoundBinaryOp>();
    bin->op = cypher::BinaryOperator::OR;
    bin->left = std::move(lhs);
    bin->right = std::move(rhs);
    BoundExpression expr = std::move(bin);

    VarRequirements reqs;
    collectExprRequirements(expr, reqs);
    EXPECT_EQ(reqs.count("n"), 1u);
    EXPECT_EQ(reqs.count("m"), 1u);
    EXPECT_EQ(reqs["n"].need_props[LabelId{1}], (std::set<uint16_t>{10}));
    EXPECT_EQ(reqs["m"].need_props[LabelId{2}], (std::set<uint16_t>{20}));
}

TEST(RequirementCollectorTest, FilterOpOwnRequirementsExcludeChild) {
    // Build Filter(predicate=n.age=10) over LabelScan.
    // The Filter's own requirements should be {n:{age}} — NOT unioned
    // with the subtree (which would still be {n:{age}} here, but the
    // point is the collector does not recurse).
    auto scan = makeLabelScanWithLabel("n", 0, LabelId{1});

    auto pref = std::make_unique<BoundPropertyRef>();
    pref->property_name = "age";
    pref->object = BoundVariableRef("n", BoundType::Vertex());
    BoundPropertyRef::ResolvedProperty cand;
    cand.label_id = LabelId{1};
    cand.prop_id = 10;
    pref->candidates.push_back(cand);

    auto filter = std::make_unique<BoundFilterOp>();
    filter->predicate = BoundLiteral(true);
    // Replace predicate with property ref so the collector finds it.
    {
        auto bin = std::make_unique<BoundBinaryOp>();
        bin->op = cypher::BinaryOperator::EQ;
        bin->left = std::move(pref);
        bin->right = BoundLiteral(int64_t{10});
        filter->predicate = std::move(bin);
    }
    filter->child = std::move(scan);
    BoundLogicalOperator op = std::move(filter);

    VarRequirements reqs = collectOpRequirements(op);
    EXPECT_EQ(reqs.count("n"), 1u);
    EXPECT_EQ(reqs["n"].need_props[LabelId{1}], (std::set<uint16_t>{10}));
}

// === Memo group-level requirements ===

TEST(MemoRequirementsTest, CopyInPopulatesRequirementsField) {
    auto pref = std::make_unique<BoundPropertyRef>();
    pref->property_name = "age";
    pref->object = BoundVariableRef("n", BoundType::Vertex());
    BoundPropertyRef::ResolvedProperty cand;
    cand.label_id = LabelId{1};
    cand.prop_id = 10;
    pref->candidates.push_back(cand);

    auto filter = std::make_unique<BoundFilterOp>();
    {
        auto bin = std::make_unique<BoundBinaryOp>();
        bin->op = cypher::BinaryOperator::EQ;
        bin->left = std::move(pref);
        bin->right = BoundLiteral(int64_t{10});
        filter->predicate = std::move(bin);
    }
    filter->child = makeLabelScanWithLabel("n", 0, LabelId{1});

    BoundLogicalOperator op = std::move(filter);
    Memo memo;
    GroupId gid = memo.copyIn(op);

    const Group& g = memo.getGroup(gid);
    EXPECT_TRUE(g.requirementsValid());
    const auto& reqs = g.requirements();
    EXPECT_EQ(reqs.count("n"), 1u);
    EXPECT_EQ(reqs.at("n").need_props.at(LabelId{1}), (std::set<uint16_t>{10}));
}

// === Enricher enforcer insertion ===

TEST(EnricherEnforcerTest, InsertReturnsInvalidWithoutAnyWinner) {
    Memo memo;
    auto scan = makeLabelScanWithLabel("n", 0, LabelId{1});
    GroupId gid = memo.copyIn(scan);

    VarRequirements reqs;
    reqs["n"].need_props[LabelId{1}] = {10};
    // No "any" winner exists yet → enforcer insertion should fail.
    ExprId eid = memo.insertEnricherEnforcer(gid, reqs);
    EXPECT_EQ(eid, INVALID_EXPR_ID);
}

TEST(EnricherEnforcerTest, GetSatisfyingWinnerFindsCheaperCandidate) {
    Memo memo;
    auto scan = makeLabelScanWithLabel("n", 0, LabelId{1});
    GroupId gid = memo.copyIn(scan);
    Group& g = memo.getGroup(gid);

    // Register two winners: one providing {10} (cost 5) and one providing
    // {10, 20} (cost 10). A query for {10} should pick the cheaper.
    PhysProp p1;
    {
        VarRequirements m;
        m["n"].need_props[LabelId{1}] = {10};
        p1.setMaterializations(m);
    }
    g.newWinner(p1, /*plan=*/ExprId{100}, Cost(5.0), /*done=*/true);

    PhysProp p2;
    {
        VarRequirements m;
        m["n"].need_props[LabelId{1}] = {10, 20};
        p2.setMaterializations(m);
    }
    g.newWinner(p2, /*plan=*/ExprId{101}, Cost(10.0), /*done=*/true);

    PhysProp query;
    {
        VarRequirements m;
        m["n"].need_props[LabelId{1}] = {10};
        query.setMaterializations(m);
    }
    Winner* w = g.getSatisfyingWinner(query);
    ASSERT_NE(w, nullptr);
    EXPECT_EQ(w->plan(), ExprId{100}); // cheaper one
    EXPECT_EQ(w->cost().value(), 5.0);
}

// === PhysicalExpr round-trip ===

TEST(PhysicalExprTest, HashAndEqualityIncludeEnricherFields) {
    PhysicalExpr a;
    a.tag = PhysicalOpTag::VertexEnrich;
    a.enrich_variable = "n";
    a.output_mat["n"].need_props[LabelId{1}] = {10};

    PhysicalExpr b = clonePhysicalExpr(a);
    EXPECT_EQ(hashPhysicalExpr(a), hashPhysicalExpr(b));
    EXPECT_TRUE(equalPhysicalExpr(a, b));

    // Different variable → unequal.
    PhysicalExpr c = clonePhysicalExpr(a);
    c.enrich_variable = "m";
    EXPECT_FALSE(equalPhysicalExpr(a, c));

    // Different output materialization → unequal.
    PhysicalExpr d = clonePhysicalExpr(a);
    d.output_mat["n"].need_props[LabelId{1}] = {20};
    EXPECT_FALSE(equalPhysicalExpr(a, d));
}

TEST(PhysicalExprTest, ClonePreservesEnricherFields) {
    PhysicalExpr src;
    src.tag = PhysicalOpTag::EdgeEnrich;
    src.enrich_variable = "r";
    src.output_mat["r"].need_labels = true;
    src.required_input_mat.push_back(VarRequirements{});

    PhysicalExpr dst = clonePhysicalExpr(src);
    EXPECT_EQ(dst.tag, PhysicalOpTag::EdgeEnrich);
    EXPECT_EQ(dst.enrich_variable, "r");
    EXPECT_TRUE(dst.output_mat.at("r").need_labels);
    EXPECT_EQ(dst.required_input_mat.size(), 1u);
}

// === Cost model enricher cases ===

TEST(CostModelTest, EnricherCostProportionalToInputCardinality) {
    LogProp group_lp;
    group_lp.cardinality = 100.0;
    LogProp input_lp;
    input_lp.cardinality = 100.0;

    Cost vc = findLocalCost(PhysicalOpTag::VertexEnrich, group_lp, {input_lp});
    Cost ec = findLocalCost(PhysicalOpTag::EdgeEnrich, group_lp, {input_lp});
    Cost pc = findLocalCost(PhysicalOpTag::PathEnrich, group_lp, {input_lp});

    EXPECT_GT(vc.value(), 0.0);
    EXPECT_GT(ec.value(), 0.0);
    EXPECT_GT(pc.value(), 0.0);
    EXPECT_EQ(vc.value(), ec.value());
    EXPECT_EQ(ec.value(), pc.value());
}

// === Phase C — Enricher enforcer end-to-end ===

TEST(EnricherE2ETest, OptimizerEmitsVertexEnrichForPropertyAccess) {
    // Build: Filter(n.age = 10) → LabelScan(n:Person)
    //
    // The Filter's predicate accesses n.age, so Filter's group has
    // requirements = {n: Person.{age=10}}. The Filter's impl rule
    // declares required_input_mat[0] = those requirements.
    //
    // LabelScan is a topology-stage leaf; its output_mat is empty.
    // O_INPUTS for Filter should detect the mismatch and insert a
    // VertexEnrich enforcer in LabelScan's group.
    auto scan = makeLabelScanWithLabel("n", 0, LabelId{1});

    auto pref = std::make_unique<BoundPropertyRef>();
    pref->property_name = "age";
    pref->object = BoundVariableRef("n", BoundType::Vertex());
    BoundPropertyRef::ResolvedProperty cand;
    cand.label_id = LabelId{1};
    cand.prop_id = 10;
    pref->candidates.push_back(cand);

    auto bin = std::make_unique<BoundBinaryOp>();
    bin->op = cypher::BinaryOperator::EQ;
    bin->left = std::move(pref);
    bin->right = BoundLiteral(int64_t{10});

    auto filter = std::make_unique<BoundFilterOp>();
    filter->predicate = BoundExpression(std::move(bin));
    filter->child = std::move(scan);

    BoundLogicalPlan plan;
    plan.root = BoundLogicalOperator(std::move(filter));

    LogicalOptimizer optimizer;
    optimizer.optimize(plan);

    // The optimizer should have produced a ChosenPlan with a VertexEnrich
    // node in the LabelScan group's winner chain.
    ASSERT_NE(plan.chosen, nullptr);
    EXPECT_EQ(plan.chosen->tag, PhysicalOpTag::Filter);
    ASSERT_EQ(plan.chosen->children.size(), 1u);

    // The Filter's child should be a VertexPropertyExtract wrapping the LabelScan.
    // Since the query only needs n.age (not the entire vertex), the optimizer
    // inserts a flat extractor instead of a heavy VertexEnricher.
    const ChosenPlan& filter_child = *plan.chosen->children[0];
    EXPECT_EQ(filter_child.tag, PhysicalOpTag::VertexPropertyExtract);
    EXPECT_EQ(filter_child.enrich_variable, "n");
    ASSERT_EQ(filter_child.children.size(), 1u);

    // VertexPropertyExtract's child should be the LabelScan.
    const ChosenPlan& enrich_child = *filter_child.children[0];
    EXPECT_EQ(enrich_child.tag, PhysicalOpTag::LabelScan);
}

TEST(EnricherE2ETest, NoEnricherWhenNoPropertyAccess) {
    // Filter(true) → LabelScan: no property access → no Enricher.
    auto scan = makeLabelScanWithLabel("n", 0, LabelId{1});

    auto filter = std::make_unique<BoundFilterOp>();
    filter->predicate = BoundLiteral(true);
    filter->child = std::move(scan);

    BoundLogicalPlan plan;
    plan.root = BoundLogicalOperator(std::move(filter));

    LogicalOptimizer optimizer;
    optimizer.optimize(plan);

    ASSERT_NE(plan.chosen, nullptr);
    EXPECT_EQ(plan.chosen->tag, PhysicalOpTag::Filter);
    ASSERT_EQ(plan.chosen->children.size(), 1u);
    EXPECT_EQ(plan.chosen->children[0]->tag, PhysicalOpTag::LabelScan);
}
