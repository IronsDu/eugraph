#include <gtest/gtest.h>

#include "query/optimizer/logical_optimizer.hpp"
#include "query/optimizer/memo.hpp"
#include "query/optimizer/opt_rule.hpp"
#include "query/planner/bound_logical_plan.hpp"

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
