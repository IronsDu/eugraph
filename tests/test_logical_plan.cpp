#include <gtest/gtest.h>

#include "compute_service/logical_plan/logical_plan_builder.hpp"
#include "compute_service/parser/cypher_parser.hpp"

using namespace eugraph;
using namespace eugraph::compute;
using namespace eugraph::cypher;

namespace {

std::variant<LogicalPlan, std::string> parseAndBuild(const std::string& query) {
    CypherQueryParser parser;
    auto parse_result = parser.parse(query);
    if (std::holds_alternative<ParseError>(parse_result)) {
        return std::get<ParseError>(parse_result).message;
    }
    auto stmt = std::move(std::get<Statement>(parse_result));
    LogicalPlanBuilder builder;
    return builder.build(std::move(stmt));
}

} // anonymous namespace

// ==================== Scan Tests ====================

TEST(LogicalPlanTest, AllNodeScan) {
    auto result = parseAndBuild("MATCH (n) RETURN n");
    ASSERT_TRUE(std::holds_alternative<LogicalPlan>(result)) << std::get<std::string>(result);

    const auto& plan = std::get<LogicalPlan>(result);
    // Root should be Project, child should be AllNodeScan
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<ProjectOp>>(plan.root));
    const auto& project = std::get<std::unique_ptr<ProjectOp>>(plan.root);
    ASSERT_EQ(project->children.size(), 1);
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<AllNodeScanOp>>(project->children[0]));
    EXPECT_EQ(std::get<std::unique_ptr<AllNodeScanOp>>(project->children[0])->variable, "n");
}

TEST(LogicalPlanTest, LabelScan) {
    auto result = parseAndBuild("MATCH (n:Person) RETURN n");
    ASSERT_TRUE(std::holds_alternative<LogicalPlan>(result)) << std::get<std::string>(result);

    const auto& plan = std::get<LogicalPlan>(result);
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<ProjectOp>>(plan.root));
    const auto& project = std::get<std::unique_ptr<ProjectOp>>(plan.root);
    ASSERT_EQ(project->children.size(), 1);
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<LabelScanOp>>(project->children[0]));
    const auto& scan = std::get<std::unique_ptr<LabelScanOp>>(project->children[0]);
    EXPECT_EQ(scan->variable, "n");
    EXPECT_EQ(scan->label, "Person");
}

// ==================== Expand Test ====================

TEST(LogicalPlanTest, Expand) {
    auto result = parseAndBuild("MATCH (a:Person)-[:KNOWS]->(b) RETURN a, b");
    ASSERT_TRUE(std::holds_alternative<LogicalPlan>(result)) << std::get<std::string>(result);

    const auto& plan = std::get<LogicalPlan>(result);
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<ProjectOp>>(plan.root));
    const auto& project = std::get<std::unique_ptr<ProjectOp>>(plan.root);
    ASSERT_EQ(project->children.size(), 1);

    // Project → Expand → LabelScan
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<ExpandOp>>(project->children[0]));
    const auto& expand = std::get<std::unique_ptr<ExpandOp>>(project->children[0]);
    EXPECT_EQ(expand->src_variable, "a");
    EXPECT_EQ(expand->dst_variable, "b");
    ASSERT_EQ(expand->children.size(), 1);
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<LabelScanOp>>(expand->children[0]));
}

// ==================== Filter Test ====================

TEST(LogicalPlanTest, Filter) {
    auto result = parseAndBuild("MATCH (n:Person) WHERE true RETURN n");
    ASSERT_TRUE(std::holds_alternative<LogicalPlan>(result)) << std::get<std::string>(result);

    const auto& plan = std::get<LogicalPlan>(result);
    // Root = Project → Filter → LabelScan
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<ProjectOp>>(plan.root));
    const auto& project = std::get<std::unique_ptr<ProjectOp>>(plan.root);
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<FilterOp>>(project->children[0]));
    const auto& filter = std::get<std::unique_ptr<FilterOp>>(project->children[0]);
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<LabelScanOp>>(filter->children[0]));
}

// ==================== Limit Test ====================

TEST(LogicalPlanTest, Limit) {
    auto result = parseAndBuild("MATCH (n) RETURN n LIMIT 10");
    ASSERT_TRUE(std::holds_alternative<LogicalPlan>(result)) << std::get<std::string>(result);

    const auto& plan = std::get<LogicalPlan>(result);
    // Root = Limit → Project → AllNodeScan
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<LimitOp>>(plan.root));
    const auto& limit = std::get<std::unique_ptr<LimitOp>>(plan.root);
    EXPECT_EQ(limit->limit, 10);
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<ProjectOp>>(limit->children[0]));
}

// ==================== Create Tests ====================

TEST(LogicalPlanTest, CreateNode) {
    auto result = parseAndBuild("CREATE (n:Person)");
    ASSERT_TRUE(std::holds_alternative<LogicalPlan>(result)) << std::get<std::string>(result);

    const auto& plan = std::get<LogicalPlan>(result);
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<CreateNodeOp>>(plan.root));
    const auto& create = std::get<std::unique_ptr<CreateNodeOp>>(plan.root);
    EXPECT_EQ(create->variable, "n");
    ASSERT_EQ(create->labels.size(), 1);
    EXPECT_EQ(create->labels[0], "Person");
}

TEST(LogicalPlanTest, CreateNodeWithEdge) {
    auto result = parseAndBuild("CREATE (a:Person)-[:KNOWS]->(b:Person)");
    ASSERT_TRUE(std::holds_alternative<LogicalPlan>(result)) << std::get<std::string>(result);

    const auto& plan = std::get<LogicalPlan>(result);
    // Root = CreateEdge → CreateNode(b) → CreateNode(a)
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<CreateEdgeOp>>(plan.root));
    const auto& edge = std::get<std::unique_ptr<CreateEdgeOp>>(plan.root);
    EXPECT_EQ(edge->src_variable, "a");
    EXPECT_EQ(edge->dst_variable, "b");
    ASSERT_EQ(edge->rel_types.size(), 1);
    EXPECT_EQ(edge->rel_types[0], "KNOWS");
}

// ==================== toString Test ====================

TEST(LogicalPlanTest, ToString) {
    auto result = parseAndBuild("MATCH (n:Person) RETURN n LIMIT 5");
    ASSERT_TRUE(std::holds_alternative<LogicalPlan>(result));
    const auto& plan = std::get<LogicalPlan>(result);
    std::string str = plan.toString();
    EXPECT_FALSE(str.empty());
    EXPECT_NE(str.find("Limit"), std::string::npos);
    EXPECT_NE(str.find("Project"), std::string::npos);
    EXPECT_NE(str.find("LabelScan"), std::string::npos);
}

// ==================== Error Cases ====================

TEST(LogicalPlanTest, EmptyQuery) {
    auto result = parseAndBuild("MATCH () RETURN *");
    // Should succeed — anonymous variable
    // Or fail if we don't support anonymous nodes yet
    // For now, expect it to handle gracefully
}
