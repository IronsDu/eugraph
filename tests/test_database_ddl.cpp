#include <gtest/gtest.h>

#include "common/types/graph_types.hpp"
#include "query/parser/database_ddl_parser.hpp"
#include "service/graph_service.hpp"

#include <filesystem>
#include <folly/coro/BlockingWait.h>

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
        auto exec_ctx = blockingWait(svc_->executeCypher(query, std::unordered_map<std::string, Value>{}, graph_name));
        EXPECT_NE(exec_ctx.ctx, nullptr);
        if (!exec_ctx.ctx)
            return {};

        std::vector<Row> rows;
        auto gen = std::move(exec_ctx.ctx->gen);
        blockingWait(folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
            while (auto chunk = co_await gen.next()) {
                for (auto& row : chunk->toRows())
                    rows.push_back(std::move(row));
            }
            co_return;
        }));
        return rows;
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
    EXPECT_EQ(exec_ctx.ctx->columns.size(), 4u);
    EXPECT_EQ(exec_ctx.ctx->columns[0], "name");
    EXPECT_EQ(exec_ctx.ctx->columns[1], "status");
    EXPECT_EQ(exec_ctx.ctx->columns[2], "type");
    EXPECT_EQ(exec_ctx.ctx->columns[3], "current");
}

TEST_F(GraphServiceDdlTest, CreateDuplicateDatabaseReturnsIdempotent) {
    auto rows1 = execute("CREATE DATABASE dupdb");
    ASSERT_EQ(rows1.size(), 1u);

    // Creating the same database again should succeed (idempotent)
    auto rows2 = execute("CREATE DATABASE dupdb");
    ASSERT_EQ(rows2.size(), 1u);
}

} // namespace
