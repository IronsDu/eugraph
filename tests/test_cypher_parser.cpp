#include <gtest/gtest.h>
#include "compute_service/parser/cypher_parser.hpp"
#include "compute_service/parser/ast.hpp"

using namespace eugraph::cypher;

class CypherParserTest : public ::testing::Test {
protected:
    CypherParser parser;

    Statement parseSuccess(const std::string& query) {
        auto result = parser.parse(query);
        if (std::holds_alternative<ParseError>(result)) {
            auto& err = std::get<ParseError>(result);
            ADD_FAILURE() << "Parse error at " << err.line << ":" << err.column << " - " << err.message;
        }
        return std::get<Statement>(std::move(result));
    }

    ParseError parseError(const std::string& query) {
        auto result = parser.parse(query);
        EXPECT_TRUE(std::holds_alternative<ParseError>(result))
            << "Expected parse error for: " << query;
        return std::get<ParseError>(result);
    }

    // 获取 RegularQuery
    RegularQuery& getRegularQuery(Statement& stmt) {
        return *std::get<std::unique_ptr<RegularQuery>>(stmt);
    }
};

// ==================== 1. 解析正确性 ====================

TEST_F(CypherParserTest, SimpleMatch) {
    auto stmt = parseSuccess("MATCH (n:Person) RETURN n");
}

TEST_F(CypherParserTest, MatchWithWhere) {
    auto stmt = parseSuccess("MATCH (n:Person) WHERE n.age > 30 RETURN n.name");
}

TEST_F(CypherParserTest, MatchMultiHop) {
    auto stmt = parseSuccess(
        "MATCH (a:Person)-[:KNOWS]->(b:Person)-[:WORKS_AT]->(c:Company) RETURN a, b, c");
}

TEST_F(CypherParserTest, MatchNoLabel) {
    auto stmt = parseSuccess("MATCH (n) RETURN n");
}

TEST_F(CypherParserTest, CreateNode) {
    auto stmt = parseSuccess("CREATE (n:Person {name: 'Alice', age: 30})");
}

TEST_F(CypherParserTest, CreateEdge) {
    auto stmt = parseSuccess("MATCH (a:Person), (b:Person) CREATE (a)-[:KNOWS]->(b)");
}

TEST_F(CypherParserTest, DeleteNode) {
    auto stmt = parseSuccess("MATCH (n:Person) DELETE n");
}

TEST_F(CypherParserTest, DetachDelete) {
    auto stmt = parseSuccess("MATCH (n:Person) DETACH DELETE n");
}

TEST_F(CypherParserTest, SetProperty) {
    auto stmt = parseSuccess("MATCH (n:Person) SET n.age = 31");
}

TEST_F(CypherParserTest, WithPipeline) {
    auto stmt = parseSuccess("MATCH (n:Person) WITH n.name AS name RETURN name");
}

TEST_F(CypherParserTest, OrderByLimitSkip) {
    auto stmt = parseSuccess("MATCH (n) RETURN n ORDER BY n.age DESC SKIP 5 LIMIT 10");
}

TEST_F(CypherParserTest, DistinctReturn) {
    auto stmt = parseSuccess("MATCH (n) RETURN DISTINCT n.age");
}

TEST_F(CypherParserTest, ComplexQuery) {
    auto stmt = parseSuccess(R"(
        MATCH (n:Person)-[:KNOWS]->(m:Person)
        WHERE n.age > 30 AND m.city = 'Beijing'
        WITH n, count(m) AS friend_count
        ORDER BY friend_count DESC
        LIMIT 10
        RETURN n.name, friend_count
    )");
}

TEST_F(CypherParserTest, CaseInsensitiveKeywords) {
    auto stmt = parseSuccess("match (n:Person) return n");
}

TEST_F(CypherParserTest, BacktickIdentifier) {
    auto stmt = parseSuccess("MATCH (`weird name`) RETURN `weird name`");
}

TEST_F(CypherParserTest, UnionQuery) {
    auto stmt = parseSuccess(
        "MATCH (n:Person) RETURN n.name UNION MATCH (m:Animal) RETURN m.name");
}

TEST_F(CypherParserTest, UnionAllQuery) {
    auto stmt = parseSuccess(
        "MATCH (n:Person) RETURN n.name UNION ALL MATCH (m:Animal) RETURN m.name");
}

TEST_F(CypherParserTest, MultiplePatterns) {
    auto stmt = parseSuccess("MATCH (a:Person), (b:City) RETURN a, b");
}

TEST_F(CypherParserTest, BidirectionalEdge) {
    auto stmt = parseSuccess("MATCH (a)-[r]-(b) RETURN r");
}

TEST_F(CypherParserTest, MultipleEdgeTypes) {
    auto stmt = parseSuccess("MATCH (a)-[r:KNOWS|FRIEND]->(b) RETURN r");
}

// ==================== 2. AST 结构验证 ====================

TEST_F(CypherParserTest, AstStructureSimpleMatch) {
    auto stmt = parseSuccess("MATCH (n:Person)-[:KNOWS]->(m) RETURN n");

    auto& query = getRegularQuery(stmt);
    ASSERT_EQ(query.first.clauses.size(), 2);  // Match + Return

    // Match clause
    auto& match = std::get<std::unique_ptr<MatchClause>>(query.first.clauses[0]);
    EXPECT_FALSE(match->optional);
    ASSERT_EQ(match->patterns.size(), 1);

    auto& part = match->patterns[0];
    ASSERT_EQ(part.element.chain.size(), 1);

    // Source node
    auto& srcNode = part.element.node;
    ASSERT_TRUE(srcNode.variable.has_value());
    EXPECT_EQ(srcNode.variable.value(), "n");
    ASSERT_EQ(srcNode.labels.size(), 1);
    EXPECT_EQ(srcNode.labels[0], "Person");

    // Relationship
    auto& [rel, dstNode] = part.element.chain[0];
    EXPECT_EQ(rel.direction, RelationshipDirection::LEFT_TO_RIGHT);
    ASSERT_EQ(rel.rel_types.size(), 1);
    EXPECT_EQ(rel.rel_types[0], "KNOWS");

    // Target node
    ASSERT_TRUE(dstNode.variable.has_value());
    EXPECT_EQ(dstNode.variable.value(), "m");
    EXPECT_TRUE(dstNode.labels.empty());
}

TEST_F(CypherParserTest, AstStructureWhere) {
    auto stmt = parseSuccess("MATCH (n:Person) WHERE n.age > 30 RETURN n");

    auto& query = getRegularQuery(stmt);
    auto& match = std::get<std::unique_ptr<MatchClause>>(query.first.clauses[0]);

    ASSERT_TRUE(match->where_pred.has_value());
    auto& whereExpr = match->where_pred.value();

    // Should be a BinaryOp (>)
    auto* binOp = std::get_if<std::unique_ptr<BinaryOp>>(&whereExpr);
    ASSERT_NE(binOp, nullptr);
    EXPECT_EQ((*binOp)->op, BinaryOperator::GT);
}

TEST_F(CypherParserTest, AstStructureReturnWithAlias) {
    auto stmt = parseSuccess("MATCH (n) RETURN n.name AS person_name, n.age");

    auto& query = getRegularQuery(stmt);
    auto& ret = std::get<std::unique_ptr<ReturnClause>>(query.first.clauses[1]);

    ASSERT_EQ(ret->items.size(), 2);

    // First item: n.name AS person_name
    EXPECT_TRUE(ret->items[0].alias.has_value());
    EXPECT_EQ(ret->items[0].alias.value(), "person_name");

    // Second item: n.age (no alias)
    EXPECT_FALSE(ret->items[1].alias.has_value());
}

TEST_F(CypherParserTest, AstStructureOrderBy) {
    auto stmt = parseSuccess("MATCH (n) RETURN n ORDER BY n.age DESC");

    auto& query = getRegularQuery(stmt);
    auto& ret = std::get<std::unique_ptr<ReturnClause>>(query.first.clauses[1]);

    ASSERT_TRUE(ret->order_by.has_value());
    ASSERT_EQ(ret->order_by->items.size(), 1);
    EXPECT_EQ(ret->order_by->items[0].direction, OrderBy::Direction::DESC);
}

TEST_F(CypherParserTest, AstStructureDeleteDetach) {
    auto stmt = parseSuccess("MATCH (n) DETACH DELETE n");

    auto& query = getRegularQuery(stmt);
    // Clauses: Match, Delete
    ASSERT_GE(query.first.clauses.size(), 2);

    auto& del = std::get<std::unique_ptr<DeleteClause>>(query.first.clauses[1]);
    EXPECT_TRUE(del->detach);
    ASSERT_EQ(del->expressions.size(), 1);
}

TEST_F(CypherParserTest, AstStructureSetProperty) {
    auto stmt = parseSuccess("MATCH (n) SET n.name = 'Bob'");

    auto& query = getRegularQuery(stmt);
    // Clauses: Match, Set
    ASSERT_GE(query.first.clauses.size(), 2);

    auto& set = std::get<std::unique_ptr<SetClause>>(query.first.clauses[1]);
    ASSERT_EQ(set->items.size(), 1);
    EXPECT_EQ(set->items[0].kind, SetItemKind::SET_PROPERTY);
    ASSERT_TRUE(set->items[0].value.has_value());
}

// ==================== 3. 错误报告 ====================

TEST_F(CypherParserTest, SyntaxErrorTypo) {
    auto err = parseError("MATCHH (n) RETURN n");
    EXPECT_GT(err.line, 0);
    EXPECT_FALSE(err.message.empty());
}

TEST_F(CypherParserTest, SyntaxErrorMissingReturn) {
    auto err = parseError("MATCH (n:Person)");
    EXPECT_FALSE(err.message.empty());
}

TEST_F(CypherParserTest, SyntaxErrorInvalidExpression) {
    auto err = parseError("MATCH (n) RETURN n +");
    EXPECT_FALSE(err.message.empty());
}

// ==================== 4. 边界情况 ====================

TEST_F(CypherParserTest, EmptyString) {
    auto result = parser.parse("");
    EXPECT_TRUE(std::holds_alternative<ParseError>(result));
}

TEST_F(CypherParserTest, WhitespaceOnly) {
    auto result = parser.parse("   ");
    EXPECT_TRUE(std::holds_alternative<ParseError>(result));
}

TEST_F(CypherParserTest, MultipleClauses) {
    auto stmt = parseSuccess(R"(
        MATCH (n:Person)
        WHERE n.age > 20
        SET n.score = 100
        RETURN n.name, n.score
        ORDER BY n.score DESC
        LIMIT 5
    )");
}
