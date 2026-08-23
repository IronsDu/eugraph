#include "query/planner/binder/pattern/pattern_legality_analyzer.hpp"

#include <gtest/gtest.h>

namespace eugraph {
namespace binder {
namespace pattern {

namespace {

MatchPatternGraph twoNodePathGraph(const std::string& node_var, const std::string& rel_var) {
    MatchPatternGraph g;
    PatternNodeInfo n0;
    n0.id = 0;
    n0.variable = node_var;
    n0.anonymous = node_var.empty();
    g.nodes.push_back(n0);

    PatternNodeInfo n1;
    n1.id = 1;
    n1.variable = "";
    n1.anonymous = true;
    g.nodes.push_back(n1);

    PatternRelationshipInfo r;
    r.id = 0;
    r.src_node = 0;
    r.dst_node = 1;
    r.variable = rel_var;
    r.anonymous = rel_var.empty();
    g.relationships.push_back(r);

    PatternPartInfo part;
    part.id = 0;
    part.ordered_elements = {0, 0, 1};
    g.parts.push_back(part);
    return g;
}

ColumnInfo makeOuter(const std::string& name, BoundType type, SlotId slot) {
    ColumnInfo info;
    info.name = name;
    info.type = std::move(type);
    info.slot_id = slot;
    return info;
}

} // namespace

TEST(PatternLegalityAnalyzerTest, OuterScalarAsPathReportsAlreadyBound) {
    auto graph = twoNodePathGraph("a", "r");
    graph.parts[0].path_variable = "p";
    std::unordered_map<std::string, ColumnInfo> outer;
    outer.emplace("p", makeOuter("p", BoundType::Bool(), 1));
    PatternLegalityAnalyzer analyzer;
    auto result = analyzer.analyze(graph, outer);
    ASSERT_EQ(result.issues.size(), 1u);
    EXPECT_EQ(result.issues[0].variable, "p");
    EXPECT_EQ(result.issues[0].error_code, "VariableAlreadyBound");
}

TEST(PatternLegalityAnalyzerTest, OuterPathAsRelationshipReportsTypeConflict) {
    auto graph = twoNodePathGraph("a", "p");
    std::unordered_map<std::string, ColumnInfo> outer;
    outer.emplace("p", makeOuter("p", BoundType::Path(), 1));
    PatternLegalityAnalyzer analyzer;
    auto result = analyzer.analyze(graph, outer);
    ASSERT_EQ(result.issues.size(), 1u);
    EXPECT_EQ(result.issues[0].variable, "p");
    EXPECT_EQ(result.issues[0].error_code, "VariableTypeConflict");
}

TEST(PatternLegalityAnalyzerTest, SamePatternPathNodeConflictReportsAlreadyBound) {
    auto graph = twoNodePathGraph("p", "r");
    graph.parts[0].path_variable = "p";
    PatternLegalityAnalyzer analyzer;
    auto result = analyzer.analyze(graph, {});
    ASSERT_EQ(result.issues.size(), 1u);
    EXPECT_EQ(result.issues[0].variable, "p");
    EXPECT_EQ(result.issues[0].error_code, "VariableAlreadyBound");
}

TEST(PatternLegalityAnalyzerTest, WildcardOuterIsCompatible) {
    auto graph = twoNodePathGraph("p", "r");
    std::unordered_map<std::string, ColumnInfo> outer;
    outer.emplace("p", makeOuter("p", BoundType::Any(), 1));
    PatternLegalityAnalyzer analyzer;
    auto result = analyzer.analyze(graph, outer);
    EXPECT_TRUE(result.issues.empty());
}

TEST(PatternLegalityAnalyzerTest, ClassificationMarksReusedVariables) {
    MatchPatternGraph graph;
    for (size_t i = 0; i < 4; ++i) {
        PatternNodeInfo n;
        n.id = i;
        n.variable = (i == 0 || i == 1) ? "x" : "";
        n.anonymous = n.variable.empty();
        graph.nodes.push_back(n);
    }
    PatternRelationshipInfo r0;
    r0.id = 0;
    r0.src_node = 0;
    r0.dst_node = 2;
    graph.relationships.push_back(r0);
    PatternRelationshipInfo r1;
    r1.id = 1;
    r1.src_node = 1;
    r1.dst_node = 3;
    graph.relationships.push_back(r1);
    PatternPartInfo p0;
    p0.id = 0;
    p0.ordered_elements = {0, 0, 2};
    PatternPartInfo p1;
    p1.id = 1;
    p1.ordered_elements = {1, 1, 3};
    graph.parts = {p0, p1};

    PatternLegalityAnalyzer analyzer;
    auto result = analyzer.analyze(graph, {});
    ASSERT_EQ(result.issues.size(), 0u);
    ASSERT_EQ(result.parts.size(), 2u);
    EXPECT_TRUE(result.parts[0].new_variables.count("x"));
    EXPECT_TRUE(result.parts[1].reused_variables.count("x"));
}

} // namespace pattern
} // namespace binder
} // namespace eugraph
