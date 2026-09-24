#include <gtest/gtest.h>

#include "test_chunk_helpers.hpp"

#include "common/types/graph_types.hpp"
#include "query/parser/database_ddl_parser.hpp"
#include "service/graph_service.hpp"

#include <algorithm>
#include <filesystem>
#include <folly/coro/BlockingWait.h>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using namespace eugraph;
using namespace eugraph::service;
using namespace folly::coro;

namespace {

std::string getDdlTestDbPath() {
    return "/tmp/eugraph_ddl_test_" + std::to_string(getpid());
}

// ==================== DatabaseDdlParser 单元测试 ====================

TEST(DatabaseDdlParserTest, ParseCreateDatabase) {
    auto stmt = DatabaseDdlParser::tryParse("CREATE DATABASE mydb");
    ASSERT_TRUE(stmt.has_value());
    EXPECT_EQ(stmt->type, DatabaseDdlStatement::CREATE_DATABASE);
    EXPECT_EQ(stmt->name, "mydb");
}

TEST(DatabaseDdlParserTest, ParseCreateDatabaseLowercase) {
    auto stmt = DatabaseDdlParser::tryParse("create database mydb");
    ASSERT_TRUE(stmt.has_value());
    EXPECT_EQ(stmt->type, DatabaseDdlStatement::CREATE_DATABASE);
    EXPECT_EQ(stmt->name, "mydb");
}

TEST(DatabaseDdlParserTest, ParseCreateDatabaseMixedCase) {
    auto stmt = DatabaseDdlParser::tryParse("CreATe DaTaBaSe mydb");
    ASSERT_TRUE(stmt.has_value());
    EXPECT_EQ(stmt->type, DatabaseDdlStatement::CREATE_DATABASE);
    EXPECT_EQ(stmt->name, "mydb");
}

TEST(DatabaseDdlParserTest, ParseCreateDatabaseExtraWhitespace) {
    auto stmt = DatabaseDdlParser::tryParse("  CREATE   DATABASE   mydb  ");
    ASSERT_TRUE(stmt.has_value());
    EXPECT_EQ(stmt->type, DatabaseDdlStatement::CREATE_DATABASE);
    EXPECT_EQ(stmt->name, "mydb");
}

TEST(DatabaseDdlParserTest, ParseDropDatabase) {
    auto stmt = DatabaseDdlParser::tryParse("DROP DATABASE mydb");
    ASSERT_TRUE(stmt.has_value());
    EXPECT_EQ(stmt->type, DatabaseDdlStatement::DROP_DATABASE);
    EXPECT_EQ(stmt->name, "mydb");
}

TEST(DatabaseDdlParserTest, ParseShowDatabases) {
    auto stmt = DatabaseDdlParser::tryParse("SHOW DATABASES");
    ASSERT_TRUE(stmt.has_value());
    EXPECT_EQ(stmt->type, DatabaseDdlStatement::SHOW_DATABASES);
}

TEST(DatabaseDdlParserTest, ParseShowDatabase) {
    auto stmt = DatabaseDdlParser::tryParse("SHOW DATABASE mydb");
    ASSERT_TRUE(stmt.has_value());
    EXPECT_EQ(stmt->type, DatabaseDdlStatement::SHOW_DATABASE);
    EXPECT_EQ(stmt->name, "mydb");
}

TEST(DatabaseDdlParserTest, ParseUse) {
    auto stmt = DatabaseDdlParser::tryParse("USE mydb");
    ASSERT_TRUE(stmt.has_value());
    EXPECT_EQ(stmt->type, DatabaseDdlStatement::USE_GRAPH);
    EXPECT_EQ(stmt->name, "mydb");
}

TEST(DatabaseDdlParserTest, ParseUseLowercase) {
    auto stmt = DatabaseDdlParser::tryParse("use mydb");
    ASSERT_TRUE(stmt.has_value());
    EXPECT_EQ(stmt->type, DatabaseDdlStatement::USE_GRAPH);
    EXPECT_EQ(stmt->name, "mydb");
}

TEST(DatabaseDdlParserTest, NonDdlReturnsNullopt) {
    EXPECT_FALSE(DatabaseDdlParser::tryParse("MATCH (n) RETURN n").has_value());
    EXPECT_FALSE(DatabaseDdlParser::tryParse("RETURN 1").has_value());
    EXPECT_FALSE(DatabaseDdlParser::tryParse("CREATE (n:Person {name: 'Alice'})").has_value());
    EXPECT_FALSE(DatabaseDdlParser::tryParse("").has_value());
    EXPECT_FALSE(DatabaseDdlParser::tryParse("   ").has_value());
}

TEST(DatabaseDdlParserTest, PartialMatchReturnsNullopt) {
    EXPECT_FALSE(DatabaseDdlParser::tryParse("CREATE INDEX").has_value());
    EXPECT_FALSE(DatabaseDdlParser::tryParse("CREATE").has_value());
    EXPECT_FALSE(DatabaseDdlParser::tryParse("DROP INDEX").has_value());
    EXPECT_FALSE(DatabaseDdlParser::tryParse("SHOW INDEXES").has_value());
}

TEST(DatabaseDdlParserTest, ParseShowDatabaseExtraWhitespace) {
    auto stmt = DatabaseDdlParser::tryParse("  SHOW   DATABASE   mydb  ");
    ASSERT_TRUE(stmt.has_value());
    EXPECT_EQ(stmt->type, DatabaseDdlStatement::SHOW_DATABASE);
    EXPECT_EQ(stmt->name, "mydb");
}

// ==================== DESCRIBE family（parse） ====================

TEST(DatabaseDdlParserTest, ParseDescribeLabels) {
    for (const auto* q : {"DESCRIBE LABELS", "describe labels", "DeScRiBe LaBeLs", "DESC LABELS"}) {
        auto stmt = DatabaseDdlParser::tryParse(q);
        ASSERT_TRUE(stmt.has_value()) << q;
        EXPECT_EQ(stmt->type, DatabaseDdlStatement::DESCRIBE_LABELS) << q;
    }
}

TEST(DatabaseDdlParserTest, ParseDescribeRelationships) {
    for (const auto* q : {"DESCRIBE RELATIONSHIPS", "describe relationships", "DESC RELATIONSHIPS"}) {
        auto stmt = DatabaseDdlParser::tryParse(q);
        ASSERT_TRUE(stmt.has_value()) << q;
        EXPECT_EQ(stmt->type, DatabaseDdlStatement::DESCRIBE_RELATIONSHIPS) << q;
    }
}

TEST(DatabaseDdlParserTest, ParseDescribeLabel) {
    auto stmt = DatabaseDdlParser::tryParse("DESCRIBE LABEL Person");
    ASSERT_TRUE(stmt.has_value());
    EXPECT_EQ(stmt->type, DatabaseDdlStatement::DESCRIBE_LABEL);
    EXPECT_EQ(stmt->name, "Person");

    auto lower = DatabaseDdlParser::tryParse("describe label person");
    ASSERT_TRUE(lower.has_value());
    EXPECT_EQ(lower->type, DatabaseDdlStatement::DESCRIBE_LABEL);
    EXPECT_EQ(lower->name, "person"); // names are case-sensitive, keywords are not
}

TEST(DatabaseDdlParserTest, ParseDescribeRelationship) {
    for (const auto* q : {"DESCRIBE RELATIONSHIP KNOWS", "DESCRIBE REL KNOWS", "DESC REL KNOWS"}) {
        auto stmt = DatabaseDdlParser::tryParse(q);
        ASSERT_TRUE(stmt.has_value()) << q;
        EXPECT_EQ(stmt->type, DatabaseDdlStatement::DESCRIBE_RELATIONSHIP) << q;
        EXPECT_EQ(stmt->name, "KNOWS") << q;
    }
}

/// A quoted name may contain spaces; the surrounding backticks are quoting, not
/// part of the name the schema stores.
TEST(DatabaseDdlParserTest, ParseDescribeQuotedName) {
    auto stmt = DatabaseDdlParser::tryParse("DESCRIBE LABEL `My Label`");
    ASSERT_TRUE(stmt.has_value());
    EXPECT_EQ(stmt->type, DatabaseDdlStatement::DESCRIBE_LABEL);
    EXPECT_EQ(stmt->name, "My Label");
}

/// The DESCRIBE branches must not swallow the statements that already existed.
TEST(DatabaseDdlParserTest, DescribeDoesNotShadowExistingStatements) {
    struct Case {
        const char* query;
        DatabaseDdlStatement::Type type;
    };
    const Case cases[] = {
        {"SHOW DATABASES", DatabaseDdlStatement::SHOW_DATABASES},
        {"SHOW DATABASE mydb", DatabaseDdlStatement::SHOW_DATABASE},
        {"SHOW PROCEDURES", DatabaseDdlStatement::SHOW_PROCEDURES},
        {"SHOW FUNCTIONS", DatabaseDdlStatement::SHOW_FUNCTIONS},
        {"SHOW CURRENT USER", DatabaseDdlStatement::SHOW_CURRENT_USER},
        {"SHOW VECTOR INDEXES", DatabaseDdlStatement::SHOW_VECTOR_INDEXES},
    };
    for (const auto& c : cases) {
        auto stmt = DatabaseDdlParser::tryParse(c.query);
        ASSERT_TRUE(stmt.has_value()) << c.query;
        EXPECT_EQ(stmt->type, c.type) << c.query;
    }
}

/// Incomplete DESCRIBE forms stay unmatched so the Cypher parser reports the
/// syntax error, instead of being read as a label literally named "LABEL".
///
/// Note the boundary: only the *name-less* forms are rejected. `DESCRIBE RELATIONSHIP
/// TYPE` is accepted and means "describe a relationship type called TYPE" -- there is
/// no reserved-word list for target names, so a keyword is a legal name. The same
/// applies to the retired plural spelling: `DESCRIBE RELATIONSHIP TYPES` now reads as
/// the singular form naming a type literally called "TYPES", and returns 0 rows.
TEST(DatabaseDdlParserTest, IncompleteDescribeReturnsNullopt) {
    for (const auto* q : {"DESCRIBE", "DESC", "DESCRIBE LABEL", "DESCRIBE RELATIONSHIP", "DESCRIBE REL"}) {
        EXPECT_FALSE(DatabaseDdlParser::tryParse(q).has_value()) << q;
    }
}

// ==================== GraphService DDL 集成测试 ====================

class GraphServiceDdlTest : public ::testing::Test {
protected:
    std::string db_path_;
    std::unique_ptr<GraphManager> gm_;
    std::unique_ptr<GraphService> svc_;

    void SetUp() override {
        db_path_ = getDdlTestDbPath();
        std::filesystem::remove_all(db_path_);

        gm_ = std::make_unique<GraphManager>();
        ASSERT_TRUE(gm_->init(db_path_, 2, 2));

        svc_ = std::make_unique<GraphService>(*gm_);
    }

    void TearDown() override {
        svc_.reset();
        if (gm_) {
            gm_->shutdown();
        }
        gm_.reset();
        std::filesystem::remove_all(db_path_);
    }

    /// Execute a Cypher query via GraphService and collect result rows.
    std::vector<Row> execute(const std::string& query, const std::string& graph_name = "default") {
        return executeWithColumns(query, graph_name).rows;
    }

    /// Same, but also reports the result column names -- the DESCRIBE family's
    /// contract is about the record shape, so the tests have to assert on it.
    struct ExecResult {
        std::vector<std::string> columns;
        std::vector<Row> rows;
    };

    ExecResult executeWithColumns(const std::string& query, const std::string& graph_name = "default") {
        ExecResult out;
        auto exec_ctx = blockingWait(svc_->executeCypher(query, std::unordered_map<std::string, Value>{}, graph_name));
        EXPECT_NE(exec_ctx.ctx, nullptr);
        if (!exec_ctx.ctx)
            return out;

        out.columns = exec_ctx.ctx->columns;
        auto gen = std::move(exec_ctx.ctx->gen);
        blockingWait(folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
            while (auto chunk = co_await gen.next()) {
                for (auto& row : eugraph::test::chunkToRows(*chunk))
                    out.rows.push_back(std::move(row));
            }
            co_return;
        }));
        return out;
    }

    /// Metadata of the currently opened graph, for arranging schema in tests.
    AsyncGraphMetaStore& meta() {
        return *gm_->getGraph("default")->async_meta;
    }

    /// Collect the single string column of a result set.
    static std::vector<std::string> strings(const std::vector<Row>& rows) {
        std::vector<std::string> out;
        for (const auto& row : rows) {
            EXPECT_EQ(row.size(), 1u);
            if (!row.empty() && std::holds_alternative<std::string>(row[0]))
                out.push_back(std::get<std::string>(row[0]));
        }
        return out;
    }
};

TEST_F(GraphServiceDdlTest, CreateDatabaseThenShowDatabases) {
    // Start with just the default database
    auto rows_before = execute("SHOW DATABASES");
    ASSERT_EQ(rows_before.size(), 1u);
    EXPECT_EQ(std::get<std::string>(rows_before[0][0]), "default");

    // Create a new database
    auto rows_create = execute("CREATE DATABASE testdb");
    ASSERT_EQ(rows_create.size(), 1u);
    EXPECT_NE(std::get<std::string>(rows_create[0][0]).find("created"), std::string::npos);

    // Verify it appears in SHOW DATABASES
    auto rows_after = execute("SHOW DATABASES");
    ASSERT_EQ(rows_after.size(), 2u);
    bool found = false;
    for (auto& row : rows_after) {
        if (std::get<std::string>(row[0]) == "testdb") {
            found = true;
            EXPECT_EQ(std::get<std::string>(row[1]), "online");
            EXPECT_EQ(std::get<std::string>(row[2]), "standard");
        }
    }
    EXPECT_TRUE(found) << "testdb should appear in SHOW DATABASES";
}

TEST_F(GraphServiceDdlTest, ShowDatabaseSpecific) {
    execute("CREATE DATABASE mygraph");

    auto rows = execute("SHOW DATABASE mygraph");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(std::get<std::string>(rows[0][0]), "mygraph");
    EXPECT_EQ(std::get<std::string>(rows[0][1]), "online");
    EXPECT_EQ(std::get<std::string>(rows[0][2]), "standard");
}

TEST_F(GraphServiceDdlTest, ShowDatabaseNotFoundReturnsEmpty) {
    auto rows = execute("SHOW DATABASE nonexistent");
    EXPECT_EQ(rows.size(), 0u);
}

TEST_F(GraphServiceDdlTest, DropDatabase) {
    execute("CREATE DATABASE todelete");
    auto rows_before = execute("SHOW DATABASES");
    ASSERT_EQ(rows_before.size(), 2u);

    auto rows_drop = execute("DROP DATABASE todelete");
    ASSERT_EQ(rows_drop.size(), 1u);
    EXPECT_NE(std::get<std::string>(rows_drop[0][0]).find("dropped"), std::string::npos);

    auto rows_after = execute("SHOW DATABASES");
    ASSERT_EQ(rows_after.size(), 1u);
    EXPECT_EQ(std::get<std::string>(rows_after[0][0]), "default");
}

TEST_F(GraphServiceDdlTest, DropNonexistentDatabase) {
    auto rows = execute("DROP DATABASE no_such_db");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_NE(std::get<std::string>(rows[0][0]).find("Failed"), std::string::npos);
}

TEST_F(GraphServiceDdlTest, UseGraphSetsSwitchedDatabase) {
    execute("CREATE DATABASE otherdb");
    auto exec_ctx =
        blockingWait(svc_->executeCypher("USE otherdb", std::unordered_map<std::string, Value>{}, "default"));
    EXPECT_EQ(exec_ctx.switched_database, "otherdb");
    EXPECT_NE(exec_ctx.ctx, nullptr);
}

TEST_F(GraphServiceDdlTest, UseGraphReturnsCurrentDatabaseRow) {
    execute("CREATE DATABASE otherdb");
    auto rows = execute("USE otherdb");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(std::get<std::string>(rows[0][0]), "otherdb");
}

TEST_F(GraphServiceDdlTest, ShowDatabasesHasColumnNames) {
    execute("CREATE DATABASE db1");
    auto exec_ctx =
        blockingWait(svc_->executeCypher("SHOW DATABASES", std::unordered_map<std::string, Value>{}, "default"));
    ASSERT_NE(exec_ctx.ctx, nullptr);
    EXPECT_EQ(exec_ctx.ctx->columns.size(), 5u);
    EXPECT_EQ(exec_ctx.ctx->columns[0], "name");
    EXPECT_EQ(exec_ctx.ctx->columns[1], "status");
    EXPECT_EQ(exec_ctx.ctx->columns[2], "type");
    EXPECT_EQ(exec_ctx.ctx->columns[3], "current");
    EXPECT_EQ(exec_ctx.ctx->columns[4], "currentStatus");
}

TEST_F(GraphServiceDdlTest, ShowDatabasesYieldAllReturnsBrowserRecordShape) {
    execute("CREATE DATABASE db1");

    auto exec_ctx = blockingWait(
        svc_->executeCypher("SHOW DATABASES YIELD *", std::unordered_map<std::string, Value>{}, "default"));
    ASSERT_NE(exec_ctx.ctx, nullptr);
    EXPECT_EQ(exec_ctx.ctx->columns.size(), 15u);
    EXPECT_EQ(exec_ctx.ctx->columns[0], "name");
    EXPECT_EQ(exec_ctx.ctx->columns[6], "requestedStatus");
    EXPECT_EQ(exec_ctx.ctx->columns[7], "currentStatus");
    EXPECT_EQ(exec_ctx.ctx->columns[10], "default");

    auto rows = execute("SHOW DATABASES YIELD *");
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(std::get<std::string>(rows[0][0]), "db1");
    EXPECT_EQ(std::get<std::string>(rows[0][4]), "localhost:17687");
    EXPECT_TRUE(std::holds_alternative<bool>(rows[0][10]));
}

TEST_F(GraphServiceDdlTest, ShowCurrentUserReturnsValidRecord) {
    auto exec_ctx =
        blockingWait(svc_->executeCypher("SHOW CURRENT USER", std::unordered_map<std::string, Value>{}, "system"));
    ASSERT_NE(exec_ctx.ctx, nullptr);
    EXPECT_EQ(exec_ctx.ctx->columns.size(), 5u);
    EXPECT_EQ(exec_ctx.ctx->columns[0], "user");

    auto rows = execute("SHOW CURRENT USER");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(std::get<std::string>(rows[0][0]), "neo4j");
    EXPECT_TRUE(std::holds_alternative<bool>(rows[0][2]));
}

TEST_F(GraphServiceDdlTest, ShowProceduresAndFunctions) {
    auto procs = execute("SHOW PROCEDURES YIELD *");
    ASSERT_GT(procs.size(), 0u);
    EXPECT_EQ(std::get<std::string>(procs[0][0]), "db.ping");

    auto funcs = execute("SHOW FUNCTIONS YIELD *");
    ASSERT_GT(funcs.size(), 0u);
    bool has_id = false;
    for (const auto& row : funcs) {
        if (std::holds_alternative<std::string>(row[0]) && std::get<std::string>(row[0]) == "id") {
            has_id = true;
            break;
        }
    }
    EXPECT_TRUE(has_id);
}

TEST_F(GraphServiceDdlTest, ShowVectorIndexesReturnsEmptyResultWithColumns) {
    auto exec_ctx = blockingWait(
        svc_->executeCypher("SHOW VECTOR INDEXES YIELD *", std::unordered_map<std::string, Value>{}, "default"));
    ASSERT_NE(exec_ctx.ctx, nullptr);
    EXPECT_GT(exec_ctx.ctx->columns.size(), 0u);
    EXPECT_EQ(execute("SHOW VECTOR INDEXES YIELD *").size(), 0u);
}

TEST_F(GraphServiceDdlTest, CreateDuplicateDatabaseReturnsIdempotent) {
    auto rows1 = execute("CREATE DATABASE dupdb");
    ASSERT_EQ(rows1.size(), 1u);

    // Creating the same database again should succeed (idempotent)
    auto rows2 = execute("CREATE DATABASE dupdb");
    ASSERT_EQ(rows2.size(), 1u);
}

// ==================== DESCRIBE family（GraphService） ====================

namespace {

/// One schema field of one label/relationship type, as DESCRIBE reports it.
struct DescribeField {
    std::string owner;
    std::string property_name;
    std::string type_name;
};

/// Assert the DESCRIBE result shape and flatten it into (owner, field, type) triples.
///
/// The type column is `propertyType`: singular and a plain STRING, because a declared
/// field carries exactly one PropertyType. The procedures keep the plural LIST form
/// (neo4j's shape); DESCRIBE is our own surface, so the two differ on purpose.
std::vector<DescribeField> describeFields(const std::vector<std::string>& columns, const std::vector<Row>& rows,
                                          const std::string& owner_column) {
    EXPECT_EQ(columns.size(), 3u);
    if (columns.size() == 3u) {
        EXPECT_EQ(columns[0], owner_column);
        EXPECT_EQ(columns[1], "propertyName");
        EXPECT_EQ(columns[2], "propertyType");
    }
    std::vector<DescribeField> out;
    for (const auto& row : rows) {
        EXPECT_EQ(row.size(), 3u);
        if (row.size() != 3u)
            continue;
        EXPECT_TRUE(std::holds_alternative<std::string>(row[0]));
        EXPECT_TRUE(std::holds_alternative<std::string>(row[1]));
        EXPECT_TRUE(std::holds_alternative<std::string>(row[2])) << "propertyType must be a plain string, not a list";
        if (!std::holds_alternative<std::string>(row[0]) || !std::holds_alternative<std::string>(row[1]) ||
            !std::holds_alternative<std::string>(row[2]))
            continue;
        out.push_back({std::get<std::string>(row[0]), std::get<std::string>(row[1]), std::get<std::string>(row[2])});
    }
    return out;
}

} // namespace

TEST_F(GraphServiceDdlTest, DescribeLabelsListsEveryLabelIncludingAnonymous) {
    auto created =
        blockingWait(svc_->createLabel("Person", {{0, "name", PropertyType::STRING, false, std::nullopt}}, "default"));
    ASSERT_NE(created.id, INVALID_LABEL_ID);

    auto result = executeWithColumns("DESCRIBE LABELS");
    EXPECT_EQ(result.columns, (std::vector<std::string>{"name", "anonymous"}));

    std::map<std::string, bool> anon_by_name;
    for (const auto& row : result.rows) {
        ASSERT_EQ(row.size(), 2u);
        ASSERT_TRUE(std::holds_alternative<std::string>(row[0]));
        ASSERT_TRUE(std::holds_alternative<bool>(row[1]));
        anon_by_name[std::get<std::string>(row[0])] = std::get<bool>(row[1]);
    }

    // A label created above through GraphService, and the anonymous label that
    // GraphManager installs for every graph -- the latter must now be reported.
    ASSERT_TRUE(anon_by_name.count("Person")) << "Person missing from DESCRIBE LABELS";
    EXPECT_FALSE(anon_by_name["Person"]);
    ASSERT_TRUE(anon_by_name.count(std::string(kAnonLabelName))) << "__anon__ missing from DESCRIBE LABELS";
    EXPECT_TRUE(anon_by_name[std::string(kAnonLabelName)]);

    // Sorted by name, so the output is reproducible.
    std::vector<std::string> order;
    for (const auto& [name, anon] : anon_by_name)
        order.push_back(name);
    EXPECT_TRUE(std::is_sorted(order.begin(), order.end()));
}

TEST_F(GraphServiceDdlTest, DescribeRelationshipsListsEdgeTypes) {
    auto created = blockingWait(
        svc_->createEdgeLabel("KNOWS", {{0, "since", PropertyType::INT64, false, std::nullopt}}, "default"));
    ASSERT_NE(created.id, INVALID_EDGE_LABEL_ID);

    auto result = executeWithColumns("DESCRIBE RELATIONSHIPS");
    EXPECT_EQ(result.columns, (std::vector<std::string>{"relationshipType"}));
    auto names = strings(result.rows);
    EXPECT_NE(std::find(names.begin(), names.end(), "KNOWS"), names.end());
    EXPECT_TRUE(std::is_sorted(names.begin(), names.end()));
}

TEST_F(GraphServiceDdlTest, DescribeLabelReportsFieldsAndTypes) {
    auto created = blockingWait(svc_->createLabel("Person",
                                                  {{0, "name", PropertyType::STRING, false, std::nullopt},
                                                   {1, "age", PropertyType::INT64, false, std::nullopt},
                                                   {2, "scores", PropertyType::DOUBLE_ARRAY, false, std::nullopt}},
                                                  "default"));
    ASSERT_NE(created.id, INVALID_LABEL_ID);

    auto result = executeWithColumns("DESCRIBE LABEL Person");
    auto fields = describeFields(result.columns, result.rows, "label");
    ASSERT_EQ(fields.size(), 3u);
    // Sorted by property name: age, name, scores.
    EXPECT_EQ(fields[0].property_name, "age");
    EXPECT_EQ(fields[0].type_name, "INTEGER");
    EXPECT_EQ(fields[1].property_name, "name");
    EXPECT_EQ(fields[1].type_name, "STRING");
    EXPECT_EQ(fields[2].property_name, "scores");
    EXPECT_EQ(fields[2].type_name, "FLOAT_ARRAY");
    for (const auto& f : fields)
        EXPECT_EQ(f.owner, "Person");
}

TEST_F(GraphServiceDdlTest, DescribeRelationshipReportsFieldsAndTypes) {
    auto created = blockingWait(
        svc_->createEdgeLabel("KNOWS", {{0, "since", PropertyType::INT64, false, std::nullopt}}, "default"));
    ASSERT_NE(created.id, INVALID_EDGE_LABEL_ID);

    auto result = executeWithColumns("DESCRIBE RELATIONSHIP KNOWS");
    auto fields = describeFields(result.columns, result.rows, "relType");
    ASSERT_EQ(fields.size(), 1u);
    EXPECT_EQ(fields[0].owner, "KNOWS");
    EXPECT_EQ(fields[0].property_name, "since");
    EXPECT_EQ(fields[0].type_name, "INTEGER");
}

/// The anonymous label's fields must be describable too -- that is the whole point
/// of exposing it: an unlabeled node still has a schema.
TEST_F(GraphServiceDdlTest, DescribeAnonymousLabelReportsItsFields) {
    // Mirror how the engine registers unlabeled-node properties: the anonymous label
    // holds them, so a real unlabeled write is what makes a field appear.
    auto exec = execute("CREATE ({nickname: 'solo'})");
    (void)exec;

    auto result = executeWithColumns("DESCRIBE LABEL __anon__");
    auto fields = describeFields(result.columns, result.rows, "label");
    bool saw_nickname = false;
    for (const auto& f : fields) {
        EXPECT_EQ(f.owner, std::string(kAnonLabelName));
        if (f.property_name == "nickname")
            saw_nickname = true;
    }
    EXPECT_TRUE(saw_nickname) << "anonymous label must report the fields of unlabeled nodes";
}

/// Unknown names are an empty result, not an error -- and the shape is still right.
TEST_F(GraphServiceDdlTest, DescribeUnknownNameReturnsEmptyResultWithShape) {
    for (const auto* q : {"DESCRIBE LABEL NoSuchLabel", "DESCRIBE RELATIONSHIP NO_SUCH_TYPE"}) {
        auto result = executeWithColumns(q);
        EXPECT_TRUE(result.rows.empty()) << q;
        EXPECT_EQ(result.columns.size(), 3u) << q;
    }
}

/// 改名后的边界：`DESCRIBE RELATIONSHIP TYPES` 不再是"列出全部关系类型"，而是落到单数形式
/// 去查一个名叫 `TYPES` 的关系类型（没有保留字表，关键字也可以是名字）——该名字未登记，
/// 于是返回 0 行。列形状是字段三元组这一点同时证明它确实被单数分支接走了。
TEST_F(GraphServiceDdlTest, RetiredRelationshipTypesSpellingFallsToSingularForm) {
    auto created = blockingWait(
        svc_->createEdgeLabel("KNOWS", {{0, "since", PropertyType::INT64, false, std::nullopt}}, "default"));
    ASSERT_NE(created.id, INVALID_EDGE_LABEL_ID);

    auto list = executeWithColumns("DESCRIBE RELATIONSHIPS");
    EXPECT_EQ(list.columns, (std::vector<std::string>{"relationshipType"}));
    auto names = strings(list.rows);
    EXPECT_NE(std::find(names.begin(), names.end(), "KNOWS"), names.end());

    auto retired = executeWithColumns("DESCRIBE RELATIONSHIP TYPES");
    EXPECT_EQ(retired.columns, (std::vector<std::string>{"relType", "propertyName", "propertyType"}));
    EXPECT_TRUE(retired.rows.empty()) << "TYPES is not a registered relationship type";
}

/// Cross-check against the pre-existing CALL procedures, which read the same
/// metadata through a different code path (CallPhysicalOp).
TEST_F(GraphServiceDdlTest, DescribeAgreesWithCallProcedures) {
    blockingWait(svc_->createLabel(
        "Person",
        {{0, "age", PropertyType::INT64, false, std::nullopt}, {1, "name", PropertyType::STRING, false, std::nullopt}},
        "default"));

    auto described = describeFields(executeWithColumns("DESCRIBE LABEL Person").columns,
                                    executeWithColumns("DESCRIBE LABEL Person").rows, "label");
    auto called = execute("CALL db.schema.nodeTypeProperties() YIELD nodeLabels, propertyName, propertyTypes "
                          "RETURN nodeLabels, propertyName, propertyTypes");

    // Build (label, field, type) triples from the procedure result and require the
    // DESCRIBE rows to be present in it.
    std::set<std::tuple<std::string, std::string, std::string>> called_triples;
    for (const auto& row : called) {
        if (row.size() != 3u || !std::holds_alternative<ListValuePtr>(row[0]))
            continue;
        const auto& labels = *std::get<ListValuePtr>(row[0]);
        if (labels.elements.empty() || !std::holds_alternative<std::string>(labels.elements[0].value))
            continue;
        if (!std::holds_alternative<std::string>(row[1]))
            continue;
        std::string type_name;
        if (std::holds_alternative<ListValuePtr>(row[2])) {
            const auto& types = *std::get<ListValuePtr>(row[2]);
            if (!types.elements.empty() && std::holds_alternative<std::string>(types.elements[0].value))
                type_name = std::get<std::string>(types.elements[0].value);
        }
        called_triples.emplace(std::get<std::string>(labels.elements[0].value), std::get<std::string>(row[1]),
                               type_name);
    }

    ASSERT_FALSE(described.empty());
    for (const auto& f : described) {
        EXPECT_TRUE(called_triples.count({f.owner, f.property_name, f.type_name}))
            << "DESCRIBE reported " << f.owner << "." << f.property_name << " : " << f.type_name
            << " which CALL db.schema.nodeTypeProperties() does not";
    }
}

/// DESCRIBE is graph-scoped: it must read the schema of the graph the session is on,
/// not the default one. Regression for handleDatabaseDdl always taking the default
/// instance.
TEST_F(GraphServiceDdlTest, DescribeFollowsSelectedGraph) {
    blockingWait(svc_->createLabel("OnlyInDefault", {{0, "a", PropertyType::INT64, false, std::nullopt}}, "default"));
    execute("CREATE DATABASE g2");
    blockingWait(svc_->createLabel("OnlyInG2", {{0, "b", PropertyType::STRING, false, std::nullopt}}, "g2"));

    // DESCRIBE LABELS is two columns (name, anonymous); take the name column.
    auto labelNames = [](const std::vector<Row>& rows) {
        std::vector<std::string> names;
        for (const auto& row : rows) {
            if (!row.empty() && std::holds_alternative<std::string>(row[0]))
                names.push_back(std::get<std::string>(row[0]));
        }
        return names;
    };

    auto in_default = labelNames(executeWithColumns("DESCRIBE LABELS", "default").rows);
    auto in_g2 = labelNames(executeWithColumns("DESCRIBE LABELS", "g2").rows);

    EXPECT_NE(std::find(in_default.begin(), in_default.end(), "OnlyInDefault"), in_default.end());
    EXPECT_EQ(std::find(in_default.begin(), in_default.end(), "OnlyInG2"), in_default.end())
        << "default graph must not report g2's label";

    EXPECT_NE(std::find(in_g2.begin(), in_g2.end(), "OnlyInG2"), in_g2.end())
        << "DESCRIBE LABELS on g2 must report g2's label";
    EXPECT_EQ(std::find(in_g2.begin(), in_g2.end(), "OnlyInDefault"), in_g2.end())
        << "DESCRIBE LABELS on g2 must not report the default graph's label";

    // The graph-scoped variant with an explicit name must follow too.
    EXPECT_EQ(executeWithColumns("DESCRIBE LABEL OnlyInG2", "g2").rows.size(), 1u);
    EXPECT_EQ(executeWithColumns("DESCRIBE LABEL OnlyInDefault", "g2").rows.size(), 0u);
}

/// Database-level statements keep resolving the default graph, so a bogus selected
/// graph must not break them.
TEST_F(GraphServiceDdlTest, DatabaseLevelDdlStillWorksForUnknownSelectedGraph) {
    auto rows = execute("SHOW DATABASES", "no_such_graph");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(std::get<std::string>(rows[0][0]), "default");
}

} // namespace
