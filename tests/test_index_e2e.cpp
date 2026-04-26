#include <gtest/gtest.h>

#include "common/types/constants.hpp"
#include "storage/data/sync_graph_data_store.hpp"
#include "storage/data/async_graph_data_store.hpp"
#include "storage/meta/sync_graph_meta_store.hpp"
#include "storage/meta/async_graph_meta_store.hpp"
#include "storage/io_scheduler.hpp"
#include "compute_service/executor/query_executor.hpp"
#include "compute_service/parser/index_ddl_parser.hpp"

#include <filesystem>
#include <folly/coro/BlockingWait.h>
#include <spdlog/spdlog.h>

using namespace eugraph;
using namespace eugraph::compute;
using namespace folly::coro;

namespace {
std::string makeTempDir(const std::string& suffix) {
    auto dir = std::filesystem::temp_directory_path() / ("eugraph_idx_e2e_" + suffix);
    std::filesystem::create_directories(dir);
    return dir.string();
}

struct TestEnv {
    std::string db_path;
    std::unique_ptr<SyncGraphDataStore> data_store;
    std::unique_ptr<SyncGraphMetaStore> meta_store;
    std::unique_ptr<IoScheduler> io;
    std::unique_ptr<AsyncGraphMetaStore> async_meta;
    std::unique_ptr<AsyncGraphDataStore> async_data;

    void init(const std::string& name) {
        db_path = makeTempDir(name);
        data_store = std::make_unique<SyncGraphDataStore>();
        meta_store = std::make_unique<SyncGraphMetaStore>();
        ASSERT_TRUE(data_store->open(db_path + "/data"));
        ASSERT_TRUE(meta_store->open(db_path + "/meta"));

        io = std::make_unique<IoScheduler>(1);

        async_meta = std::make_unique<AsyncGraphMetaStore>();
        auto ok = blockingWait(async_meta->open(*meta_store, *io));
        ASSERT_TRUE(ok);

        async_data = std::make_unique<AsyncGraphDataStore>(*data_store, *io);
    }

    void shutdown() {
        async_data.reset();
        blockingWait(async_meta->close());
        async_meta.reset();
        io.reset();
        data_store->close();
        meta_store->close();
        std::filesystem::remove_all(db_path);
    }
};
} // namespace

class IndexE2ETest : public ::testing::Test {
protected:
    TestEnv env;

    void SetUp() override { env.init(testing::UnitTest::GetInstance()->current_test_info()->name()); }
    void TearDown() override { env.shutdown(); }
};

// Helper: run query and collect all rows
static std::vector<Row> runQuery(QueryExecutor& executor, const std::string& query) {
    auto result = blockingWait(executor.executeAsync(query));
    return result.rows;
}

// Helper: create label with properties via meta + data stores
static void createLabel(TestEnv& env, const std::string& name,
                        const std::vector<PropertyDef>& props) {
    auto label_id = blockingWait(env.async_meta->createLabel(name, props));
    ASSERT_NE(label_id, INVALID_LABEL_ID);
    ASSERT_TRUE(blockingWait(env.async_data->createLabel(label_id)));
}

// Helper: manually insert vertices with properties and index entries
static void insertVertexWithIndex(TestEnv& env, LabelId label_id, VertexId vid,
                                  const Properties& props,
                                  const std::vector<LabelDef::IndexDef>& indexes) {
    auto txn = env.data_store->beginTransaction();
    std::pair<LabelId, Properties> lp{label_id, props};
    env.data_store->insertVertex(txn, vid, {&lp, 1}, nullptr);
    // Manually insert index entries for all WRITE_ONLY/PUBLIC indexes
    for (const auto& idx : indexes) {
        if (idx.state == IndexState::WRITE_ONLY || idx.state == IndexState::PUBLIC) {
            for (auto prop_id : idx.prop_ids) {
                if (prop_id < props.size() && props[prop_id].has_value()) {
                    auto table = vidxTable(label_id, prop_id);
                    env.data_store->insertIndexEntry(table, *props[prop_id], vid);
                }
            }
        }
    }
    env.data_store->commitTransaction(txn);
}

TEST_F(IndexE2ETest, QueryByIndexEquality) {
    // Create label "Person" with properties name, age
    createLabel(env, "Person", {
        {0, "name", PropertyType::STRING, false, std::nullopt},
        {0, "age", PropertyType::INT64, false, std::nullopt},
    });

    // Get the label definition
    auto label_def = blockingWait(env.async_meta->getLabelDef("Person"));
    ASSERT_TRUE(label_def.has_value());
    LabelId label_id = label_def->id;
    auto name_prop_id = label_def->properties[0].id;
    auto age_prop_id = label_def->properties[1].id;

    // Create index on age (manually set to PUBLIC for immediate use)
    ASSERT_TRUE(blockingWait(env.async_meta->createVertexIndex("idx_age", "Person", "age", false)));
    ASSERT_TRUE(blockingWait(env.async_meta->updateIndexState("idx_age", IndexState::PUBLIC)));

    // Create the index storage table
    auto index_table = vidxTable(label_id, age_prop_id);
    ASSERT_TRUE(env.data_store->createIndex(index_table));

    // Insert test data
    auto updated_def = blockingWait(env.async_meta->getLabelDef("Person"));
    for (int i = 1; i <= 5; ++i) {
        Properties props(3); // index 0 unused, 1=name, 2=age
        props[name_prop_id] = std::string("person_") + std::to_string(i);
        props[age_prop_id] = int64_t(i * 10);
        insertVertexWithIndex(env, label_id, static_cast<VertexId>(i), props, updated_def->indexes);
    }

    // Now query with WHERE n.age = 30 — should use index scan
    QueryExecutor executor(*env.async_data, *env.async_meta, {});
    auto rows = runQuery(executor, "MATCH (n:Person) WHERE n.age = 30 RETURN n");

    ASSERT_EQ(rows.size(), 1u);
    // The vertex with age=30 is vid=3
    auto& val = rows[0][0];
    ASSERT_TRUE(std::holds_alternative<VertexValue>(val));
    EXPECT_EQ(std::get<VertexValue>(val).id, 3u);
}

TEST_F(IndexE2ETest, QueryWithoutIndexUsesLabelScan) {
    // Create label without any index
    createLabel(env, "Person", {
        {0, "name", PropertyType::STRING, false, std::nullopt},
        {0, "age", PropertyType::INT64, false, std::nullopt},
    });

    auto label_def = blockingWait(env.async_meta->getLabelDef("Person"));
    ASSERT_TRUE(label_def.has_value());

    // Insert data via Cypher
    QueryExecutor executor(*env.async_data, *env.async_meta, {});
    runQuery(executor, "CREATE (n:Person {name: 'alice', age: 25})");
    runQuery(executor, "CREATE (n:Person {name: 'bob', age: 30})");

    // Query without index — should still work via LabelScan + Filter
    auto rows = runQuery(executor, "MATCH (n:Person) WHERE n.age = 30 RETURN n");
    ASSERT_EQ(rows.size(), 1u);
}

TEST_F(IndexE2ETest, DdlParserCreateVertexIndex) {
    auto stmt = IndexDdlParser::tryParse("CREATE INDEX idx_age FOR (n:Person) ON (n.age)");
    ASSERT_TRUE(stmt.has_value());
    EXPECT_EQ(stmt->type, IndexDdlStatement::CREATE_VERTEX_INDEX);
    EXPECT_EQ(stmt->index_name, "idx_age");
    EXPECT_EQ(stmt->label_name, "Person");
    EXPECT_EQ(stmt->property_name, "age");
    EXPECT_FALSE(stmt->unique);
}

TEST_F(IndexE2ETest, DdlParserCreateUniqueIndex) {
    auto stmt = IndexDdlParser::tryParse("CREATE UNIQUE INDEX idx_name FOR (n:Person) ON (n.name)");
    ASSERT_TRUE(stmt.has_value());
    EXPECT_EQ(stmt->type, IndexDdlStatement::CREATE_VERTEX_INDEX);
    EXPECT_TRUE(stmt->unique);
    EXPECT_EQ(stmt->index_name, "idx_name");
}

TEST_F(IndexE2ETest, DdlParserCreateEdgeIndex) {
    auto stmt = IndexDdlParser::tryParse("CREATE INDEX idx_w FOR ()-[r:KNOWS]-() ON (r.weight)");
    ASSERT_TRUE(stmt.has_value());
    EXPECT_EQ(stmt->type, IndexDdlStatement::CREATE_EDGE_INDEX);
    EXPECT_EQ(stmt->label_name, "KNOWS");
    EXPECT_EQ(stmt->property_name, "weight");
}

TEST_F(IndexE2ETest, DdlParserDropIndex) {
    auto stmt = IndexDdlParser::tryParse("DROP INDEX idx_age");
    ASSERT_TRUE(stmt.has_value());
    EXPECT_EQ(stmt->type, IndexDdlStatement::DROP_INDEX);
    EXPECT_EQ(stmt->index_name, "idx_age");
}

TEST_F(IndexE2ETest, DdlParserShowIndexes) {
    auto stmt = IndexDdlParser::tryParse("SHOW INDEXES");
    ASSERT_TRUE(stmt.has_value());
    EXPECT_EQ(stmt->type, IndexDdlStatement::SHOW_INDEXES);
}

TEST_F(IndexE2ETest, DdlParserNonIndexQuery) {
    auto stmt = IndexDdlParser::tryParse("MATCH (n:Person) RETURN n");
    EXPECT_FALSE(stmt.has_value());
}

TEST_F(IndexE2ETest, MetaStoreIndexCRUD) {
    createLabel(env, "Person", {
        {0, "name", PropertyType::STRING, false, std::nullopt},
        {0, "age", PropertyType::INT64, false, std::nullopt},
    });

    // Create index
    ASSERT_TRUE(blockingWait(env.async_meta->createVertexIndex("idx_age", "Person", "age", false)));

    // List indexes
    auto indexes = blockingWait(env.async_meta->listIndexes());
    ASSERT_EQ(indexes.size(), 1u);
    EXPECT_EQ(indexes[0].name, "idx_age");
    EXPECT_EQ(indexes[0].property_name, "age");
    EXPECT_EQ(indexes[0].state, IndexState::WRITE_ONLY);

    // Update state
    ASSERT_TRUE(blockingWait(env.async_meta->updateIndexState("idx_age", IndexState::PUBLIC)));
    auto info = blockingWait(env.async_meta->getIndex("idx_age"));
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->state, IndexState::PUBLIC);

    // Drop index
    ASSERT_TRUE(blockingWait(env.async_meta->dropIndex("idx_age")));
    indexes = blockingWait(env.async_meta->listIndexes());
    EXPECT_TRUE(indexes.empty());
}

TEST_F(IndexE2ETest, IndexRangeQuery) {
    createLabel(env, "Person", {
        {0, "name", PropertyType::STRING, false, std::nullopt},
        {0, "age", PropertyType::INT64, false, std::nullopt},
    });

    auto label_def = blockingWait(env.async_meta->getLabelDef("Person"));
    ASSERT_TRUE(label_def.has_value());
    LabelId label_id = label_def->id;
    auto age_prop_id = label_def->properties[1].id;

    ASSERT_TRUE(blockingWait(env.async_meta->createVertexIndex("idx_age", "Person", "age", false)));
    ASSERT_TRUE(blockingWait(env.async_meta->updateIndexState("idx_age", IndexState::PUBLIC)));

    auto index_table = vidxTable(label_id, age_prop_id);
    ASSERT_TRUE(env.data_store->createIndex(index_table));

    auto updated_def = blockingWait(env.async_meta->getLabelDef("Person"));
    for (int i = 1; i <= 10; ++i) {
        Properties props(3);
        props[1] = std::string("p") + std::to_string(i);
        props[2] = int64_t(i * 10);
        insertVertexWithIndex(env, label_id, static_cast<VertexId>(i), props, updated_def->indexes);
    }

    // Range scan: age > 30 and age < 70 → should return vids 4,5,6
    std::vector<VertexId> results;
    auto txn = env.data_store->beginTransaction();
    env.data_store->scanIndexRange(txn, index_table, int64_t(30), int64_t(70), [&](uint64_t eid) {
        results.push_back(eid);
        return true;
    });
    env.data_store->commitTransaction(txn);

    ASSERT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0], 4u);
    EXPECT_EQ(results[1], 5u);
    EXPECT_EQ(results[2], 6u);
}

// ==================== DDL via QueryExecutor ====================

TEST_F(IndexE2ETest, DdlCreateIndexViaExecutor) {
    createLabel(env, "Person", {
        {0, "name", PropertyType::STRING, false, std::nullopt},
        {0, "age", PropertyType::INT64, false, std::nullopt},
    });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});
    auto result = blockingWait(executor.executeAsync(
        "CREATE INDEX idx_age FOR (n:Person) ON (n.age)"));

    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1u);
    // result column contains "Index created: idx_age"
    EXPECT_TRUE(std::holds_alternative<std::string>(result.rows[0][0]));
    EXPECT_NE(std::get<std::string>(result.rows[0][0]).find("idx_age"), std::string::npos);

    // Verify index is in PUBLIC state via meta store
    auto info = blockingWait(env.async_meta->getIndex("idx_age"));
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->state, IndexState::PUBLIC);
}

TEST_F(IndexE2ETest, DdlShowIndexesViaExecutor) {
    createLabel(env, "Person", {
        {0, "name", PropertyType::STRING, false, std::nullopt},
        {0, "age", PropertyType::INT64, false, std::nullopt},
    });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});

    // No indexes initially
    auto result = blockingWait(executor.executeAsync("SHOW INDEXES"));
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 0u);
    ASSERT_EQ(result.columns.size(), 5u);
    EXPECT_EQ(result.columns[0], "name");

    // Create an index
    blockingWait(executor.executeAsync("CREATE INDEX idx_age FOR (n:Person) ON (n.age)"));

    // Now SHOW INDEXES should return 1 row
    result = blockingWait(executor.executeAsync("SHOW INDEXES"));
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1u);
    EXPECT_EQ(std::get<std::string>(result.rows[0][0]), "idx_age");
    EXPECT_EQ(std::get<std::string>(result.rows[0][1]), "Person");
    EXPECT_EQ(std::get<std::string>(result.rows[0][2]), "age");
    EXPECT_EQ(std::get<std::string>(result.rows[0][4]), "PUBLIC");
}

TEST_F(IndexE2ETest, DdlDropIndexViaExecutor) {
    createLabel(env, "Person", {
        {0, "name", PropertyType::STRING, false, std::nullopt},
        {0, "age", PropertyType::INT64, false, std::nullopt},
    });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});
    blockingWait(executor.executeAsync("CREATE INDEX idx_age FOR (n:Person) ON (n.age)"));

    // Drop it
    auto result = blockingWait(executor.executeAsync("DROP INDEX idx_age"));
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_NE(std::get<std::string>(result.rows[0][0]).find("dropped"), std::string::npos);

    // Verify it's gone
    auto indexes = blockingWait(env.async_meta->listIndexes());
    EXPECT_TRUE(indexes.empty());
}

TEST_F(IndexE2ETest, DdlCreateIndexLabelNotFound) {
    QueryExecutor executor(*env.async_data, *env.async_meta, {});
    auto result = blockingWait(executor.executeAsync(
        "CREATE INDEX idx_age FOR (n:NoSuchLabel) ON (n.age)"));
    EXPECT_FALSE(result.error.empty());
    EXPECT_NE(result.error.find("Label not found"), std::string::npos);
}

TEST_F(IndexE2ETest, EndToEndCreateIndexAndQuery) {
    // Full flow: create label → insert data → create index → query via index
    createLabel(env, "Person", {
        {0, "name", PropertyType::STRING, false, std::nullopt},
        {0, "age", PropertyType::INT64, false, std::nullopt},
    });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});

    // Insert data
    runQuery(executor, "CREATE (n:Person {name: 'alice', age: 25})");
    runQuery(executor, "CREATE (n:Person {name: 'bob', age: 30})");
    runQuery(executor, "CREATE (n:Person {name: 'charlie', age: 35})");

    // Before index: query still works via LabelScan + Filter
    auto rows = runQuery(executor, "MATCH (n:Person) WHERE n.age = 30 RETURN n");
    ASSERT_EQ(rows.size(), 1u);

    // Create index (data already exists — backfill should populate index)
    auto ddl_result = blockingWait(executor.executeAsync(
        "CREATE INDEX idx_age FOR (n:Person) ON (n.age)"));
    ASSERT_TRUE(ddl_result.error.empty()) << ddl_result.error;

    // Query via IndexScan should now return correct results after backfill
    rows = runQuery(executor, "MATCH (n:Person) WHERE n.age = 35 RETURN n");
    ASSERT_EQ(rows.size(), 1u);

    // SHOW INDEXES
    auto show = blockingWait(executor.executeAsync("SHOW INDEXES"));
    ASSERT_EQ(show.rows.size(), 1u);
    EXPECT_EQ(std::get<std::string>(show.rows[0][0]), "idx_age");
}
