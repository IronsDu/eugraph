#include "compute_service/parser/ast.hpp"
#include "compute_service/parser/cypher_parser.hpp"
#include <gtest/gtest.h>

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
        EXPECT_TRUE(std::holds_alternative<ParseError>(result)) << "Expected parse error for: " << query;
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
    auto stmt = parseSuccess("MATCH (a:Person)-[:KNOWS]->(b:Person)-[:WORKS_AT]->(c:Company) RETURN a, b, c");
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
    auto stmt = parseSuccess("MATCH (n:Person) RETURN n.name UNION MATCH (m:Animal) RETURN m.name");
}

TEST_F(CypherParserTest, UnionAllQuery) {
    auto stmt = parseSuccess("MATCH (n:Person) RETURN n.name UNION ALL MATCH (m:Animal) RETURN m.name");
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
    ASSERT_EQ(query.first.clauses.size(), 2); // Match + Return

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

// ==================== 3. 详细 AST 验证 ====================

TEST_F(CypherParserTest, AstNodeWithProperties) {
    auto stmt = parseSuccess("CREATE (n:Person {name: 'Alice', age: 30})");

    auto& query = getRegularQuery(stmt);
    auto& create = std::get<std::unique_ptr<CreateClause>>(query.first.clauses[0]);
    ASSERT_EQ(create->patterns.size(), 1);

    auto& node = create->patterns[0].element.node;
    ASSERT_TRUE(node.variable.has_value());
    EXPECT_EQ(node.variable.value(), "n");
    ASSERT_EQ(node.labels.size(), 1);
    EXPECT_EQ(node.labels[0], "Person");

    // Verify properties map
    ASSERT_TRUE(node.properties.has_value());
    ASSERT_EQ(node.properties->entries.size(), 2);
    EXPECT_EQ(node.properties->entries[0].first, "name");
    EXPECT_EQ(node.properties->entries[1].first, "age");
}

TEST_F(CypherParserTest, AstEdgeWithProperties) {
    auto stmt = parseSuccess("MATCH (a)-[r:KNOWS {since: 2020}]->(b) RETURN r");

    auto& query = getRegularQuery(stmt);
    auto& match = std::get<std::unique_ptr<MatchClause>>(query.first.clauses[0]);
    auto& [rel, dstNode] = match->patterns[0].element.chain[0];

    ASSERT_TRUE(rel.variable.has_value());
    EXPECT_EQ(rel.variable.value(), "r");
    ASSERT_EQ(rel.rel_types.size(), 1);
    EXPECT_EQ(rel.rel_types[0], "KNOWS");

    ASSERT_TRUE(rel.properties.has_value());
    ASSERT_EQ(rel.properties->entries.size(), 1);
    EXPECT_EQ(rel.properties->entries[0].first, "since");
}

TEST_F(CypherParserTest, AstMultipleLabels) {
    auto stmt = parseSuccess("MATCH (n:Person:Employee) RETURN n");

    auto& query = getRegularQuery(stmt);
    auto& match = std::get<std::unique_ptr<MatchClause>>(query.first.clauses[0]);
    auto& node = match->patterns[0].element.node;

    ASSERT_EQ(node.labels.size(), 2);
    EXPECT_EQ(node.labels[0], "Person");
    EXPECT_EQ(node.labels[1], "Employee");
}

TEST_F(CypherParserTest, AstMultipleEdgeTypes) {
    auto stmt = parseSuccess("MATCH (a)-[r:KNOWS|FRIEND_OF]->(b) RETURN r");

    auto& query = getRegularQuery(stmt);
    auto& match = std::get<std::unique_ptr<MatchClause>>(query.first.clauses[0]);
    auto& [rel, dstNode] = match->patterns[0].element.chain[0];

    ASSERT_EQ(rel.rel_types.size(), 2);
    EXPECT_EQ(rel.rel_types[0], "KNOWS");
    EXPECT_EQ(rel.rel_types[1], "FRIEND_OF");
}

TEST_F(CypherParserTest, AstLeftToRightEdge) {
    auto stmt = parseSuccess("MATCH (a)-[r:KNOWS]->(b) RETURN r");
    auto& query = getRegularQuery(stmt);
    auto& match = std::get<std::unique_ptr<MatchClause>>(query.first.clauses[0]);
    auto& [rel, _] = match->patterns[0].element.chain[0];
    EXPECT_EQ(rel.direction, RelationshipDirection::LEFT_TO_RIGHT);
}

TEST_F(CypherParserTest, AstRightToLeftEdge) {
    auto stmt = parseSuccess("MATCH (a)<-[r:KNOWS]-(b) RETURN r");
    auto& query = getRegularQuery(stmt);
    auto& match = std::get<std::unique_ptr<MatchClause>>(query.first.clauses[0]);
    auto& [rel, _] = match->patterns[0].element.chain[0];
    EXPECT_EQ(rel.direction, RelationshipDirection::RIGHT_TO_LEFT);
}

TEST_F(CypherParserTest, AstUndirectedEdge) {
    auto stmt = parseSuccess("MATCH (a)-[r:KNOWS]-(b) RETURN r");
    auto& query = getRegularQuery(stmt);
    auto& match = std::get<std::unique_ptr<MatchClause>>(query.first.clauses[0]);
    auto& [rel, _] = match->patterns[0].element.chain[0];
    EXPECT_EQ(rel.direction, RelationshipDirection::UNDIRECTED);
}

TEST_F(CypherParserTest, AstThreeHopPath) {
    auto stmt = parseSuccess("MATCH (a)-[r1:X]->(b)-[r2:Y]->(c)-[r3:Z]->(d) RETURN a, d");

    auto& query = getRegularQuery(stmt);
    auto& match = std::get<std::unique_ptr<MatchClause>>(query.first.clauses[0]);
    auto& elem = match->patterns[0].element;

    ASSERT_EQ(elem.chain.size(), 3);

    EXPECT_EQ(elem.chain[0].first.rel_types[0], "X");
    EXPECT_EQ(elem.chain[1].first.rel_types[0], "Y");
    EXPECT_EQ(elem.chain[2].first.rel_types[0], "Z");

    // Verify target node variables
    EXPECT_EQ(elem.chain[0].second.variable.value(), "b");
    EXPECT_EQ(elem.chain[1].second.variable.value(), "c");
    EXPECT_EQ(elem.chain[2].second.variable.value(), "d");
}

TEST_F(CypherParserTest, AstDisconnectedPatterns) {
    auto stmt = parseSuccess("MATCH (a:Person), (b:City) RETURN a, b");

    auto& query = getRegularQuery(stmt);
    auto& match = std::get<std::unique_ptr<MatchClause>>(query.first.clauses[0]);
    ASSERT_EQ(match->patterns.size(), 2);

    // First pattern: standalone node
    EXPECT_EQ(match->patterns[0].element.node.labels[0], "Person");
    EXPECT_TRUE(match->patterns[0].element.chain.empty());

    // Second pattern: standalone node
    EXPECT_EQ(match->patterns[1].element.node.labels[0], "City");
    EXPECT_TRUE(match->patterns[1].element.chain.empty());
}

TEST_F(CypherParserTest, AstNamedPath) {
    auto stmt = parseSuccess("MATCH p = (a)-[:KNOWS]->(b) RETURN p");

    auto& query = getRegularQuery(stmt);
    auto& match = std::get<std::unique_ptr<MatchClause>>(query.first.clauses[0]);
    ASSERT_EQ(match->patterns.size(), 1);

    auto& part = match->patterns[0];
    ASSERT_TRUE(part.variable.has_value());
    EXPECT_EQ(part.variable.value(), "p");
}

// === Expression types ===

TEST_F(CypherParserTest, AstLiteralValues) {
    auto stmt = parseSuccess("RETURN 42, 3.14, 'hello', TRUE, NULL");

    auto& query = getRegularQuery(stmt);
    auto& ret = std::get<std::unique_ptr<ReturnClause>>(query.first.clauses[0]);
    ASSERT_EQ(ret->items.size(), 5);

    // 42 -> int64
    {
        auto* lit = std::get_if<std::unique_ptr<Literal>>(&ret->items[0].expr);
        ASSERT_NE(lit, nullptr);
        auto* intVal = std::get_if<int64_t>(&(*lit)->value);
        ASSERT_NE(intVal, nullptr);
        EXPECT_EQ(*intVal, 42);
    }

    // 3.14 -> double
    {
        auto* lit = std::get_if<std::unique_ptr<Literal>>(&ret->items[1].expr);
        ASSERT_NE(lit, nullptr);
        auto* dblVal = std::get_if<double>(&(*lit)->value);
        ASSERT_NE(dblVal, nullptr);
        EXPECT_DOUBLE_EQ(*dblVal, 3.14);
    }

    // 'hello' -> string
    {
        auto* lit = std::get_if<std::unique_ptr<Literal>>(&ret->items[2].expr);
        ASSERT_NE(lit, nullptr);
        auto* strVal = std::get_if<std::string>(&(*lit)->value);
        ASSERT_NE(strVal, nullptr);
        EXPECT_EQ(*strVal, "hello");
    }

    // TRUE -> bool
    {
        auto* lit = std::get_if<std::unique_ptr<Literal>>(&ret->items[3].expr);
        ASSERT_NE(lit, nullptr);
        auto* boolVal = std::get_if<bool>(&(*lit)->value);
        ASSERT_NE(boolVal, nullptr);
        EXPECT_TRUE(*boolVal);
    }

    // NULL -> NullValue
    {
        auto* lit = std::get_if<std::unique_ptr<Literal>>(&ret->items[4].expr);
        ASSERT_NE(lit, nullptr);
        auto* nullVal = std::get_if<NullValue>(&(*lit)->value);
        ASSERT_NE(nullVal, nullptr);
    }
}

TEST_F(CypherParserTest, AstBinaryOperators) {
    // Test arithmetic: a + b * c - d
    auto stmt = parseSuccess("MATCH (n) RETURN n.a + n.b * n.c - n.d");
    auto& query = getRegularQuery(stmt);
    auto& ret = std::get<std::unique_ptr<ReturnClause>>(query.first.clauses[1]);
    ASSERT_EQ(ret->items.size(), 1);

    // Top-level should be SUB
    auto* binOp = std::get_if<std::unique_ptr<BinaryOp>>(&ret->items[0].expr);
    ASSERT_NE(binOp, nullptr);
    EXPECT_EQ((*binOp)->op, BinaryOperator::SUB);
}

TEST_F(CypherParserTest, AstComparisonOperators) {
    auto stmt = parseSuccess("MATCH (n) WHERE n.age >= 18 AND n.age < 65 RETURN n");
    auto& query = getRegularQuery(stmt);
    auto& match = std::get<std::unique_ptr<MatchClause>>(query.first.clauses[0]);
    ASSERT_TRUE(match->where_pred.has_value());

    // Top-level: AND
    auto* andOp = std::get_if<std::unique_ptr<BinaryOp>>(&match->where_pred.value());
    ASSERT_NE(andOp, nullptr);
    EXPECT_EQ((*andOp)->op, BinaryOperator::AND);

    // Left child: GTE
    auto* leftBin = std::get_if<std::unique_ptr<BinaryOp>>(&(*andOp)->left);
    ASSERT_NE(leftBin, nullptr);
    EXPECT_EQ((*leftBin)->op, BinaryOperator::GTE);

    // Right child: LT
    auto* rightBin = std::get_if<std::unique_ptr<BinaryOp>>(&(*andOp)->right);
    ASSERT_NE(rightBin, nullptr);
    EXPECT_EQ((*rightBin)->op, BinaryOperator::LT);
}

TEST_F(CypherParserTest, AstNotOperator) {
    auto stmt = parseSuccess("MATCH (n) WHERE NOT n.active RETURN n");
    auto& query = getRegularQuery(stmt);
    auto& match = std::get<std::unique_ptr<MatchClause>>(query.first.clauses[0]);

    auto* unOp = std::get_if<std::unique_ptr<UnaryOp>>(&match->where_pred.value());
    ASSERT_NE(unOp, nullptr);
    EXPECT_EQ((*unOp)->op, UnaryOperator::NOT);
}

TEST_F(CypherParserTest, AstIsNullOperator) {
    auto stmt = parseSuccess("MATCH (n) WHERE n.name IS NULL RETURN n");
    auto& query = getRegularQuery(stmt);
    auto& match = std::get<std::unique_ptr<MatchClause>>(query.first.clauses[0]);

    auto* unOp = std::get_if<std::unique_ptr<UnaryOp>>(&match->where_pred.value());
    ASSERT_NE(unOp, nullptr);
    EXPECT_EQ((*unOp)->op, UnaryOperator::IS_NULL);
}

TEST_F(CypherParserTest, AstIsNotNullOperator) {
    auto stmt = parseSuccess("MATCH (n) WHERE n.name IS NOT NULL RETURN n");
    auto& query = getRegularQuery(stmt);
    auto& match = std::get<std::unique_ptr<MatchClause>>(query.first.clauses[0]);

    auto* unOp = std::get_if<std::unique_ptr<UnaryOp>>(&match->where_pred.value());
    ASSERT_NE(unOp, nullptr);
    EXPECT_EQ((*unOp)->op, UnaryOperator::IS_NOT_NULL);
}

TEST_F(CypherParserTest, AstStringOperators) {
    // STARTS WITH
    {
        auto stmt = parseSuccess("MATCH (n) WHERE n.name STARTS WITH 'Al' RETURN n");
        auto& query = getRegularQuery(stmt);
        auto& match = std::get<std::unique_ptr<MatchClause>>(query.first.clauses[0]);
        auto* bin = std::get_if<std::unique_ptr<BinaryOp>>(&match->where_pred.value());
        ASSERT_NE(bin, nullptr);
        EXPECT_EQ((*bin)->op, BinaryOperator::STARTS_WITH);
    }
    // ENDS WITH
    {
        auto stmt = parseSuccess("MATCH (n) WHERE n.name ENDS WITH 'ce' RETURN n");
        auto& query = getRegularQuery(stmt);
        auto& match = std::get<std::unique_ptr<MatchClause>>(query.first.clauses[0]);
        auto* bin = std::get_if<std::unique_ptr<BinaryOp>>(&match->where_pred.value());
        ASSERT_NE(bin, nullptr);
        EXPECT_EQ((*bin)->op, BinaryOperator::ENDS_WITH);
    }
    // CONTAINS
    {
        auto stmt = parseSuccess("MATCH (n) WHERE n.name CONTAINS 'li' RETURN n");
        auto& query = getRegularQuery(stmt);
        auto& match = std::get<std::unique_ptr<MatchClause>>(query.first.clauses[0]);
        auto* bin = std::get_if<std::unique_ptr<BinaryOp>>(&match->where_pred.value());
        ASSERT_NE(bin, nullptr);
        EXPECT_EQ((*bin)->op, BinaryOperator::CONTAINS);
    }
}

TEST_F(CypherParserTest, AstInOperator) {
    auto stmt = parseSuccess("MATCH (n) WHERE n.status IN ['active', 'pending'] RETURN n");
    auto& query = getRegularQuery(stmt);
    auto& match = std::get<std::unique_ptr<MatchClause>>(query.first.clauses[0]);

    auto* bin = std::get_if<std::unique_ptr<BinaryOp>>(&match->where_pred.value());
    ASSERT_NE(bin, nullptr);
    EXPECT_EQ((*bin)->op, BinaryOperator::IN);

    // Right side should be a list literal
    auto* listExpr = std::get_if<std::unique_ptr<ListExpr>>(&(*bin)->right);
    ASSERT_NE(listExpr, nullptr);
    ASSERT_EQ((*listExpr)->elements.size(), 2);
}

TEST_F(CypherParserTest, AstFunctionCall) {
    auto stmt = parseSuccess("MATCH (n) RETURN count(n), max(n.age), min(n.score)");

    auto& query = getRegularQuery(stmt);
    auto& ret = std::get<std::unique_ptr<ReturnClause>>(query.first.clauses[1]);
    ASSERT_EQ(ret->items.size(), 3);

    // count(n)
    {
        auto* fc = std::get_if<std::unique_ptr<FunctionCall>>(&ret->items[0].expr);
        ASSERT_NE(fc, nullptr);
        EXPECT_EQ((*fc)->name, "count");
        EXPECT_FALSE((*fc)->distinct);
        ASSERT_EQ((*fc)->args.size(), 1);
    }

    // max(n.age)
    {
        auto* fc = std::get_if<std::unique_ptr<FunctionCall>>(&ret->items[1].expr);
        ASSERT_NE(fc, nullptr);
        EXPECT_EQ((*fc)->name, "max");
    }

    // min(n.score)
    {
        auto* fc = std::get_if<std::unique_ptr<FunctionCall>>(&ret->items[2].expr);
        ASSERT_NE(fc, nullptr);
        EXPECT_EQ((*fc)->name, "min");
    }
}

TEST_F(CypherParserTest, AstFunctionCallDistinct) {
    auto stmt = parseSuccess("MATCH (n) RETURN count(DISTINCT n.name)");

    auto& query = getRegularQuery(stmt);
    auto& ret = std::get<std::unique_ptr<ReturnClause>>(query.first.clauses[1]);
    ASSERT_EQ(ret->items.size(), 1);

    auto* fc = std::get_if<std::unique_ptr<FunctionCall>>(&ret->items[0].expr);
    ASSERT_NE(fc, nullptr);
    EXPECT_EQ((*fc)->name, "count");
    EXPECT_TRUE((*fc)->distinct);
}

TEST_F(CypherParserTest, AstParameter) {
    auto stmt = parseSuccess("MATCH (n) WHERE n.age > $minAge RETURN n");

    auto& query = getRegularQuery(stmt);
    auto& match = std::get<std::unique_ptr<MatchClause>>(query.first.clauses[0]);

    // WHERE -> BinaryOp(GT) -> right side should be Parameter
    auto* bin = std::get_if<std::unique_ptr<BinaryOp>>(&match->where_pred.value());
    ASSERT_NE(bin, nullptr);
    auto* param = std::get_if<std::unique_ptr<Parameter>>(&(*bin)->right);
    ASSERT_NE(param, nullptr);
    EXPECT_EQ((*param)->name, "minAge");
}

TEST_F(CypherParserTest, AstPropertyAccess) {
    auto stmt = parseSuccess("MATCH (n) RETURN n.name, n.age");

    auto& query = getRegularQuery(stmt);
    auto& ret = std::get<std::unique_ptr<ReturnClause>>(query.first.clauses[1]);
    ASSERT_EQ(ret->items.size(), 2);

    // n.name
    {
        auto* pa = std::get_if<std::unique_ptr<PropertyAccess>>(&ret->items[0].expr);
        ASSERT_NE(pa, nullptr);
        EXPECT_EQ((*pa)->property, "name");

        auto* var = std::get_if<std::unique_ptr<Variable>>(&(*pa)->object);
        ASSERT_NE(var, nullptr);
        EXPECT_EQ((*var)->name, "n");
    }

    // n.age
    {
        auto* pa = std::get_if<std::unique_ptr<PropertyAccess>>(&ret->items[1].expr);
        ASSERT_NE(pa, nullptr);
        EXPECT_EQ((*pa)->property, "age");
    }
}

TEST_F(CypherParserTest, AstListLiteral) {
    auto stmt = parseSuccess("RETURN [1, 2, 3]");

    auto& query = getRegularQuery(stmt);
    auto& ret = std::get<std::unique_ptr<ReturnClause>>(query.first.clauses[0]);
    ASSERT_EQ(ret->items.size(), 1);

    auto* list = std::get_if<std::unique_ptr<ListExpr>>(&ret->items[0].expr);
    ASSERT_NE(list, nullptr);
    ASSERT_EQ((*list)->elements.size(), 3);
}

TEST_F(CypherParserTest, AstMapLiteral) {
    auto stmt = parseSuccess("RETURN {name: 'Alice', age: 30}");

    auto& query = getRegularQuery(stmt);
    auto& ret = std::get<std::unique_ptr<ReturnClause>>(query.first.clauses[0]);
    ASSERT_EQ(ret->items.size(), 1);

    auto* map = std::get_if<std::unique_ptr<MapExpr>>(&ret->items[0].expr);
    ASSERT_NE(map, nullptr);
    ASSERT_EQ((*map)->entries.size(), 2);
    EXPECT_EQ((*map)->entries[0].first, "name");
    EXPECT_EQ((*map)->entries[1].first, "age");
}

// === Clause structures ===

TEST_F(CypherParserTest, AstWithClause) {
    auto stmt = parseSuccess("MATCH (n) WITH n.name AS name, n.age RETURN name");

    auto& query = getRegularQuery(stmt);
    // Clauses: Match, With, Return
    ASSERT_EQ(query.first.clauses.size(), 3);

    auto& with = std::get<std::unique_ptr<WithClause>>(query.first.clauses[1]);
    ASSERT_EQ(with->items.size(), 2);
    EXPECT_TRUE(with->items[0].alias.has_value());
    EXPECT_EQ(with->items[0].alias.value(), "name");
    EXPECT_FALSE(with->items[1].alias.has_value());
}

TEST_F(CypherParserTest, AstWithWhere) {
    auto stmt = parseSuccess("MATCH (n) WITH n.age AS age WHERE age > 20 RETURN age");

    auto& query = getRegularQuery(stmt);
    auto& with = std::get<std::unique_ptr<WithClause>>(query.first.clauses[1]);
    ASSERT_TRUE(with->where_pred.has_value());
}

TEST_F(CypherParserTest, AstOptionalMatch) {
    auto stmt = parseSuccess("MATCH (n:Person) OPTIONAL MATCH (n)-[:HAS]->(c:Car) RETURN n, c");

    auto& query = getRegularQuery(stmt);
    // First MATCH
    auto& match1 = std::get<std::unique_ptr<MatchClause>>(query.first.clauses[0]);
    EXPECT_FALSE(match1->optional);

    // Second MATCH (OPTIONAL)
    auto& match2 = std::get<std::unique_ptr<MatchClause>>(query.first.clauses[1]);
    EXPECT_TRUE(match2->optional);
}

TEST_F(CypherParserTest, AstReturnDistinct) {
    auto stmt = parseSuccess("MATCH (n) RETURN DISTINCT n.age");

    auto& query = getRegularQuery(stmt);
    auto& ret = std::get<std::unique_ptr<ReturnClause>>(query.first.clauses[1]);
    EXPECT_TRUE(ret->distinct);
}

TEST_F(CypherParserTest, AstReturnAll) {
    auto stmt = parseSuccess("MATCH (n) RETURN *");

    auto& query = getRegularQuery(stmt);
    auto& ret = std::get<std::unique_ptr<ReturnClause>>(query.first.clauses[1]);
    EXPECT_TRUE(ret->return_all);
}

TEST_F(CypherParserTest, AstSkipLimit) {
    auto stmt = parseSuccess("MATCH (n) RETURN n SKIP 10 LIMIT 5");

    auto& query = getRegularQuery(stmt);
    auto& ret = std::get<std::unique_ptr<ReturnClause>>(query.first.clauses[1]);

    ASSERT_TRUE(ret->skip.has_value());
    ASSERT_TRUE(ret->limit.has_value());

    // skip = Literal(10)
    auto* skipLit = std::get_if<std::unique_ptr<Literal>>(&ret->skip.value());
    ASSERT_NE(skipLit, nullptr);
    EXPECT_EQ(std::get<int64_t>((*skipLit)->value), 10);

    // limit = Literal(5)
    auto* limitLit = std::get_if<std::unique_ptr<Literal>>(&ret->limit.value());
    ASSERT_NE(limitLit, nullptr);
    EXPECT_EQ(std::get<int64_t>((*limitLit)->value), 5);
}

TEST_F(CypherParserTest, AstUnion) {
    auto stmt = parseSuccess("MATCH (a:X) RETURN a UNION MATCH (b:Y) RETURN b");

    auto& query = getRegularQuery(stmt);
    ASSERT_EQ(query.unions.size(), 1);
    EXPECT_FALSE(query.unions[0].first.all);
}

TEST_F(CypherParserTest, AstUnionAll) {
    auto stmt = parseSuccess("MATCH (a:X) RETURN a UNION ALL MATCH (b:Y) RETURN b");

    auto& query = getRegularQuery(stmt);
    ASSERT_EQ(query.unions.size(), 1);
    EXPECT_TRUE(query.unions[0].first.all);
}

TEST_F(CypherParserTest, AstSetPropertyAndLabel) {
    auto stmt = parseSuccess("MATCH (n) SET n.name = 'Bob', n:Employee");

    auto& query = getRegularQuery(stmt);
    auto& set = std::get<std::unique_ptr<SetClause>>(query.first.clauses[1]);
    ASSERT_EQ(set->items.size(), 2);

    // SET n.name = 'Bob'
    EXPECT_EQ(set->items[0].kind, SetItemKind::SET_PROPERTY);

    // SET n:Employee
    EXPECT_EQ(set->items[1].kind, SetItemKind::SET_LABELS);
    EXPECT_EQ(set->items[1].label, "Employee");
}

TEST_F(CypherParserTest, AstRemovePropertyAndLabel) {
    auto stmt = parseSuccess("MATCH (n) REMOVE n.age, n:Employee");

    auto& query = getRegularQuery(stmt);
    auto& remove = std::get<std::unique_ptr<RemoveClause>>(query.first.clauses[1]);
    ASSERT_EQ(remove->items.size(), 2);

    // REMOVE n.age
    EXPECT_EQ(remove->items[0].kind, RemoveItem::Kind::PROPERTY);

    // REMOVE n:Employee
    EXPECT_EQ(remove->items[1].kind, RemoveItem::Kind::LABEL);
    EXPECT_EQ(remove->items[1].name, "Employee");
}

TEST_F(CypherParserTest, AstComplexNestedExpression) {
    // Test nested property access: a.b.c
    auto stmt = parseSuccess("MATCH (n) RETURN n.props.name");

    auto& query = getRegularQuery(stmt);
    auto& ret = std::get<std::unique_ptr<ReturnClause>>(query.first.clauses[1]);

    // n.props.name -> PropertyAccess(PropertyAccess(Variable(n), "props"), "name")
    auto* outer = std::get_if<std::unique_ptr<PropertyAccess>>(&ret->items[0].expr);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ((*outer)->property, "name");

    auto* inner = std::get_if<std::unique_ptr<PropertyAccess>>(&(*outer)->object);
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ((*inner)->property, "props");

    auto* var = std::get_if<std::unique_ptr<Variable>>(&(*inner)->object);
    ASSERT_NE(var, nullptr);
    EXPECT_EQ((*var)->name, "n");
}

TEST_F(CypherParserTest, AstParenthesizedExpression) {
    auto stmt = parseSuccess("MATCH (n) RETURN (n.a + n.b) * n.c");
    auto& query = getRegularQuery(stmt);
    auto& ret = std::get<std::unique_ptr<ReturnClause>>(query.first.clauses[1]);

    // Top level should be MUL
    auto* mul = std::get_if<std::unique_ptr<BinaryOp>>(&ret->items[0].expr);
    ASSERT_NE(mul, nullptr);
    EXPECT_EQ((*mul)->op, BinaryOperator::MUL);

    // Left child should be ADD (from parentheses)
    auto* add = std::get_if<std::unique_ptr<BinaryOp>>(&(*mul)->left);
    ASSERT_NE(add, nullptr);
    EXPECT_EQ((*add)->op, BinaryOperator::ADD);
}

// ==================== 4. 错误报告 ====================

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

// ==================== 5. 边界情况 ====================

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
