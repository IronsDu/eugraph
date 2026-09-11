#include <gtest/gtest.h>

#include "common/types/constants.hpp"
#include "query/executor/query_executor.hpp"
#include "query/parser/index_ddl_parser.hpp"
#include "storage/data/async_graph_data_store.hpp"
#include "storage/data/sync_graph_data_store.hpp"
#include "storage/io_scheduler.hpp"
#include "storage/meta/async_graph_meta_store.hpp"
#include "storage/meta/sync_graph_meta_store.hpp"

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

    void SetUp() override {
        env.init(testing::UnitTest::GetInstance()->current_test_info()->name());
    }
    void TearDown() override {
        env.shutdown();
    }
};

// Helper: drain prepareStream into ExecutionResult
static ExecutionResult execSync(QueryExecutor& executor, const std::string& query) {
    auto ctx = blockingWait(executor.prepareStream(query));
    ExecutionResult result;
    if (!ctx->error.empty()) {
        result.error = std::move(ctx->error);
        return result;
    }
    result.columns = std::move(ctx->columns);
    auto gen = std::move(ctx->gen);
    blockingWait(folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
        while (auto chunk = co_await gen.next()) {
            auto rows = chunk->toRows();
            for (auto& row : rows) {
                result.rows.push_back(std::move(row));
            }
        }
        if (ctx->should_commit) {
            co_await ctx->store.commitTran(ctx->txn);
        }
    }));
    return result;
}

// Helper: drain prepareStream into rows
static std::vector<Row> runQuery(QueryExecutor& executor, const std::string& query) {
    ExecutionResult result;
    auto ctx = blockingWait(executor.prepareStream(query));
    if (!ctx->error.empty()) {
        return {};
    }
    result.columns = std::move(ctx->columns);
    auto gen = std::move(ctx->gen);
    blockingWait(folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
        while (auto chunk = co_await gen.next()) {
            auto rows = chunk->toRows();
            for (auto& row : rows) {
                result.rows.push_back(std::move(row));
            }
        }
        if (ctx->should_commit) {
            co_await ctx->store.commitTran(ctx->txn);
        }
    }));
    return result.rows;
}

// Helper: create label with properties via meta + data stores
static void createLabel(TestEnv& env, const std::string& name, const std::vector<PropertyDef>& props) {
    auto label_id = blockingWait(env.async_meta->createLabel(name, props));
    ASSERT_NE(label_id, INVALID_LABEL_ID);
    ASSERT_TRUE(blockingWait(env.async_data->createLabel(label_id)));
}

// Helper: create edge label with properties via meta + data stores
static void createEdgeLabel(TestEnv& env, const std::string& name, const std::vector<PropertyDef>& props) {
    auto edge_label_id = blockingWait(env.async_meta->createEdgeLabel(name, props));
    ASSERT_NE(edge_label_id, INVALID_EDGE_LABEL_ID);
    ASSERT_TRUE(blockingWait(env.async_data->createEdgeLabel(edge_label_id)));
}

// Helper: manually insert vertices with properties and index entries
static void insertVertexWithIndex(TestEnv& env, LabelId label_id, VertexId vid, const Properties& props,
                                  const LabelDef& label_def) {
    // Ensure new index-id tables exist.
    for (const auto& idx : label_def.indexes) {
        if (idx.index_id != 0 && (idx.state == IndexState::WRITE_ONLY || idx.state == IndexState::PUBLIC))
            env.data_store->createIndex(vidxTableById(idx.index_id));
    }

    auto txn = env.data_store->beginTransaction();
    std::pair<LabelId, Properties> lp{label_id, props};
    env.data_store->insertVertex(txn, vid, {&lp, 1});

    for (const auto& idx : label_def.indexes) {
        if (idx.index_id == 0)
            continue;
        if (idx.state != IndexState::WRITE_ONLY && idx.state != IndexState::PUBLIC)
            continue;

        std::vector<PropertyValue> values;
        bool all_present = true;
        for (const auto& acc : idx.accessors) {
            if (acc.is_strong) {
                all_present = false;
                break;
            }
            uint16_t pid = UINT16_MAX;
            for (const auto& pd : label_def.properties) {
                if (pd.name == acc.property_name) {
                    pid = pd.id;
                    break;
                }
            }
            if (pid == UINT16_MAX || pid >= props.size() || !props[pid].has_value()) {
                all_present = false;
                break;
            }
            values.push_back(props[pid].value());
        }
        if (all_present)
            env.data_store->insertIndexEntry(vidxTableById(idx.index_id), values, vid);
    }
    env.data_store->commitTransaction(txn);
}

TEST_F(IndexE2ETest, QueryByIndexEquality) {
    // Create label "Person" with properties name, age
    createLabel(env, "Person",
                {
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
    ASSERT_TRUE(blockingWait(env.async_meta->createVertexIndex("idx_age", "Person", {"age"}, false)));
    ASSERT_TRUE(blockingWait(env.async_meta->updateIndexState("idx_age", IndexState::PUBLIC)));

    // Create the index storage table
    auto updated_def = blockingWait(env.async_meta->getLabelDef("Person"));
    ASSERT_TRUE(updated_def.has_value());
    auto index_table = vidxTableById(updated_def->indexes[0].index_id);
    ASSERT_TRUE(env.data_store->createIndex(index_table));

    // Insert test data
    for (int i = 1; i <= 5; ++i) {
        Properties props(2);
        props[name_prop_id] = std::string("person_") + std::to_string(i);
        props[age_prop_id] = int64_t(i * 10);
        insertVertexWithIndex(env, label_id, static_cast<VertexId>(i), props, *updated_def);
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
    createLabel(env, "Person",
                {
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
    ASSERT_EQ(stmt->accessors.size(), 1u);
    EXPECT_EQ(stmt->accessors[0].property_name, "age");
    EXPECT_FALSE(stmt->unique);
}

TEST_F(IndexE2ETest, DdlParserCreateStrongAccessorIndex) {
    auto stmt = IndexDdlParser::tryParse("CREATE INDEX idx_name FOR (n:Message) ON (n::Post.creationDate)");
    ASSERT_TRUE(stmt.has_value());
    EXPECT_EQ(stmt->type, IndexDdlStatement::CREATE_VERTEX_INDEX);
    EXPECT_EQ(stmt->index_name, "idx_name");
    EXPECT_EQ(stmt->label_name, "Message");
    ASSERT_EQ(stmt->accessors.size(), 1u);
    EXPECT_TRUE(stmt->accessors[0].is_strong);
    EXPECT_EQ(stmt->accessors[0].source_label, "Post");
    EXPECT_EQ(stmt->accessors[0].property_name, "creationDate");
    ASSERT_EQ(stmt->accessors.size(), 1u);
    EXPECT_EQ(stmt->accessors[0].property_name, "creationDate");
}

TEST_F(IndexE2ETest, DdlParserCreateMixedCompositeIndex) {
    auto stmt = IndexDdlParser::tryParse("CREATE INDEX idx_mix FOR (n:Message) ON (n::Post.creationDate, n.name)");
    ASSERT_TRUE(stmt.has_value());
    EXPECT_EQ(stmt->type, IndexDdlStatement::CREATE_VERTEX_INDEX);
    ASSERT_EQ(stmt->accessors.size(), 2u);
    EXPECT_TRUE(stmt->accessors[0].is_strong);
    EXPECT_EQ(stmt->accessors[0].source_label, "Post");
    EXPECT_EQ(stmt->accessors[0].property_name, "creationDate");
    EXPECT_FALSE(stmt->accessors[1].is_strong);
    EXPECT_EQ(stmt->accessors[1].property_name, "name");
    ASSERT_EQ(stmt->accessors.size(), 2u);
    EXPECT_EQ(stmt->accessors[0].property_name, "creationDate");
    EXPECT_EQ(stmt->accessors[1].property_name, "name");
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
    ASSERT_EQ(stmt->accessors.size(), 1u);
    EXPECT_EQ(stmt->accessors[0].property_name, "weight");
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
    createLabel(env, "Person",
                {
                    {0, "name", PropertyType::STRING, false, std::nullopt},
                    {0, "age", PropertyType::INT64, false, std::nullopt},
                });

    // Create index
    ASSERT_TRUE(blockingWait(env.async_meta->createVertexIndex("idx_age", "Person", {"age"}, false)));

    // List indexes
    auto indexes = blockingWait(env.async_meta->listIndexes());
    ASSERT_EQ(indexes.size(), 1u);
    EXPECT_EQ(indexes[0].name, "idx_age");
    ASSERT_EQ(indexes[0].property_names.size(), 1u);
    EXPECT_EQ(indexes[0].property_names[0], "age");
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
    createLabel(env, "Person",
                {
                    {0, "name", PropertyType::STRING, false, std::nullopt},
                    {0, "age", PropertyType::INT64, false, std::nullopt},
                });

    auto label_def = blockingWait(env.async_meta->getLabelDef("Person"));
    ASSERT_TRUE(label_def.has_value());
    LabelId label_id = label_def->id;

    ASSERT_TRUE(blockingWait(env.async_meta->createVertexIndex("idx_age", "Person", {"age"}, false)));
    ASSERT_TRUE(blockingWait(env.async_meta->updateIndexState("idx_age", IndexState::PUBLIC)));

    auto updated_def = blockingWait(env.async_meta->getLabelDef("Person"));
    ASSERT_TRUE(updated_def.has_value());
    auto index_table = vidxTableById(updated_def->indexes[0].index_id);
    ASSERT_TRUE(env.data_store->createIndex(index_table));

    for (int i = 1; i <= 10; ++i) {
        Properties props(2);
        props[0] = std::string("p") + std::to_string(i);
        props[1] = int64_t(i * 10);
        insertVertexWithIndex(env, label_id, static_cast<VertexId>(i), props, *updated_def);
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
    createLabel(env, "Person",
                {
                    {0, "name", PropertyType::STRING, false, std::nullopt},
                    {0, "age", PropertyType::INT64, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});
    auto result = execSync(executor, "CREATE INDEX idx_age FOR (n:Person) ON (n.age)");

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
    createLabel(env, "Person",
                {
                    {0, "name", PropertyType::STRING, false, std::nullopt},
                    {0, "age", PropertyType::INT64, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});

    // No indexes initially
    auto result = execSync(executor, "SHOW INDEXES");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 0u);
    ASSERT_EQ(result.columns.size(), 5u);
    EXPECT_EQ(result.columns[0], "name");

    // Create an index
    execSync(executor, "CREATE INDEX idx_age FOR (n:Person) ON (n.age)");

    // Now SHOW INDEXES should return 1 row
    result = execSync(executor, "SHOW INDEXES");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1u);
    EXPECT_EQ(std::get<std::string>(result.rows[0][0]), "idx_age");
    EXPECT_EQ(std::get<std::string>(result.rows[0][1]), "Person");
    EXPECT_EQ(std::get<std::string>(result.rows[0][2]), "age");
    EXPECT_EQ(std::get<std::string>(result.rows[0][4]), "PUBLIC");
}

TEST_F(IndexE2ETest, DdlDropIndexViaExecutor) {
    createLabel(env, "Person",
                {
                    {0, "name", PropertyType::STRING, false, std::nullopt},
                    {0, "age", PropertyType::INT64, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});
    execSync(executor, "CREATE INDEX idx_age FOR (n:Person) ON (n.age)");

    // Drop it
    auto result = execSync(executor, "DROP INDEX idx_age");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_NE(std::get<std::string>(result.rows[0][0]).find("dropped"), std::string::npos);

    // Verify it's gone
    auto indexes = blockingWait(env.async_meta->listIndexes());
    EXPECT_TRUE(indexes.empty());
}

TEST_F(IndexE2ETest, DdlCreateIndexLabelNotFound) {
    QueryExecutor executor(*env.async_data, *env.async_meta, {});
    auto result = execSync(executor, "CREATE INDEX idx_age FOR (n:NoSuchLabel) ON (n.age)");
    EXPECT_FALSE(result.error.empty());
    EXPECT_NE(result.error.find("Label not found"), std::string::npos);
}

TEST_F(IndexE2ETest, DdlCreateWeakIndexBackfillAcrossLabels) {
    createLabel(env, "Message", {});
    createLabel(env, "Post",
                {
                    {0, "creationDate", PropertyType::INT64, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});
    auto create = execSync(executor, "CREATE (n:Post:Message {creationDate: 123})");
    ASSERT_TRUE(create.error.empty()) << create.error;

    auto ddl = execSync(executor, "CREATE INDEX idx_msg_date FOR (n:Message) ON (n.creationDate)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;

    auto idx = env.async_meta->schema().findIndexByName("idx_msg_date");
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(idx->state, IndexState::PUBLIC);

    auto table = vidxTableById(idx->index_id);
    std::vector<VertexId> results;
    auto txn = env.data_store->beginTransaction();
    env.data_store->scanIndexEquality(txn, table, int64_t(123), [&](uint64_t eid) {
        results.push_back(eid);
        return true;
    });
    env.data_store->commitTransaction(txn);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], 1u);

    // The weak index should be usable by the query planner.
    auto rows = runQuery(executor, "MATCH (n:Message) WHERE n.creationDate = 123 RETURN n");
    ASSERT_EQ(rows.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<VertexValue>(rows[0][0]));
    EXPECT_EQ(std::get<VertexValue>(rows[0][0]).id, 1u);
}

TEST_F(IndexE2ETest, WeakIndexMaintainsOnLabelAddRemove) {
    createLabel(env, "Message", {});
    createLabel(env, "Post",
                {
                    {0, "creationDate", PropertyType::INT64, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});
    auto create = execSync(executor, "CREATE (n:Post:Message {creationDate: 123})");
    ASSERT_TRUE(create.error.empty()) << create.error;
    auto ddl = execSync(executor, "CREATE INDEX idx_msg_date FOR (n:Message) ON (n.creationDate)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;

    auto idx = env.async_meta->schema().findIndexByName("idx_msg_date");
    ASSERT_TRUE(idx.has_value());
    auto table = vidxTableById(idx->index_id);

    // Removing the filter label must remove the index entry.
    auto remove = execSync(executor, "MATCH (n:Message) REMOVE n:Message");
    ASSERT_TRUE(remove.error.empty()) << remove.error;
    {
        std::vector<VertexId> results;
        auto txn = env.data_store->beginTransaction();
        env.data_store->scanIndexEquality(txn, table, int64_t(123), [&](uint64_t eid) {
            results.push_back(eid);
            return true;
        });
        env.data_store->commitTransaction(txn);
        EXPECT_TRUE(results.empty());
    }

    // Adding the filter label back must re-add the index entry.
    auto set_label = execSync(executor, "MATCH (n:Post) SET n:Message");
    ASSERT_TRUE(set_label.error.empty()) << set_label.error;
    {
        std::vector<VertexId> results;
        auto txn = env.data_store->beginTransaction();
        env.data_store->scanIndexEquality(txn, table, int64_t(123), [&](uint64_t eid) {
            results.push_back(eid);
            return true;
        });
        env.data_store->commitTransaction(txn);
        ASSERT_EQ(results.size(), 1u);
        EXPECT_EQ(results[0], 1u);
    }
}

TEST_F(IndexE2ETest, WeakIndexMaintainsOnPropertySetRemove) {
    createLabel(env, "Message", {});
    createLabel(env, "Post",
                {
                    {0, "creationDate", PropertyType::INT64, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});
    auto create = execSync(executor, "CREATE (n:Post:Message {creationDate: 123})");
    ASSERT_TRUE(create.error.empty()) << create.error;
    auto ddl = execSync(executor, "CREATE INDEX idx_msg_date FOR (n:Message) ON (n.creationDate)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;

    auto idx = env.async_meta->schema().findIndexByName("idx_msg_date");
    ASSERT_TRUE(idx.has_value());
    auto table = vidxTableById(idx->index_id);

    auto set_prop = execSync(executor, "MATCH (n:Post) SET n.creationDate = 456");
    ASSERT_TRUE(set_prop.error.empty()) << set_prop.error;
    {
        std::vector<VertexId> old_hits;
        std::vector<VertexId> new_hits;
        auto txn = env.data_store->beginTransaction();
        env.data_store->scanIndexEquality(txn, table, int64_t(123), [&](uint64_t eid) {
            old_hits.push_back(eid);
            return true;
        });
        env.data_store->scanIndexEquality(txn, table, int64_t(456), [&](uint64_t eid) {
            new_hits.push_back(eid);
            return true;
        });
        env.data_store->commitTransaction(txn);
        EXPECT_TRUE(old_hits.empty());
        ASSERT_EQ(new_hits.size(), 1u);
        EXPECT_EQ(new_hits[0], 1u);
    }

    auto remove_prop = execSync(executor, "MATCH (n:Post) REMOVE n.creationDate");
    ASSERT_TRUE(remove_prop.error.empty()) << remove_prop.error;
    {
        std::vector<VertexId> hits;
        auto txn = env.data_store->beginTransaction();
        env.data_store->scanIndexEquality(txn, table, int64_t(456), [&](uint64_t eid) {
            hits.push_back(eid);
            return true;
        });
        env.data_store->commitTransaction(txn);
        EXPECT_TRUE(hits.empty());
    }
}

TEST_F(IndexE2ETest, WeakIndexRemovedOnVertexDelete) {
    createLabel(env, "Message", {});
    createLabel(env, "Post",
                {
                    {0, "creationDate", PropertyType::INT64, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});
    auto create = execSync(executor, "CREATE (n:Post:Message {creationDate: 123})");
    ASSERT_TRUE(create.error.empty()) << create.error;
    auto ddl = execSync(executor, "CREATE INDEX idx_msg_date FOR (n:Message) ON (n.creationDate)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;

    auto idx = env.async_meta->schema().findIndexByName("idx_msg_date");
    ASSERT_TRUE(idx.has_value());
    auto table = vidxTableById(idx->index_id);

    auto del = execSync(executor, "MATCH (n:Post) DELETE n");
    ASSERT_TRUE(del.error.empty()) << del.error;

    std::vector<VertexId> hits;
    auto txn = env.data_store->beginTransaction();
    env.data_store->scanIndexEquality(txn, table, int64_t(123), [&](uint64_t eid) {
        hits.push_back(eid);
        return true;
    });
    env.data_store->commitTransaction(txn);
    EXPECT_TRUE(hits.empty());
}

TEST_F(IndexE2ETest, EdgeIndexMaintainsOnPropertySetRemove) {
    createLabel(env, "Person",
                {
                    {0, "name", PropertyType::STRING, false, std::nullopt},
                });
    createEdgeLabel(env, "KNOWS",
                    {
                        {0, "since", PropertyType::INT64, false, std::nullopt},
                    });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});
    auto create = execSync(executor, "CREATE (a:Person {name:'alice'}), (b:Person {name:'bob'}), "
                                     "(a)-[:KNOWS {since:123}]->(b)");
    ASSERT_TRUE(create.error.empty()) << create.error;
    auto ddl = execSync(executor, "CREATE INDEX idx_knows_since FOR ()-[r:KNOWS]-() ON (r.since)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;

    auto edge_label = blockingWait(env.async_meta->getEdgeLabelDef("KNOWS"));
    ASSERT_TRUE(edge_label.has_value());
    auto table = eidxTable(edge_label->id, edge_label->properties[0].id);

    auto set_prop = execSync(executor, "MATCH (:Person {name:'alice'})-[r:KNOWS]->(:Person) SET r.since = 456");
    ASSERT_TRUE(set_prop.error.empty()) << set_prop.error;
    {
        std::vector<EdgeId> old_hits;
        std::vector<EdgeId> new_hits;
        auto txn = env.data_store->beginTransaction();
        env.data_store->scanIndexEquality(txn, table, int64_t(123), [&](uint64_t eid) {
            old_hits.push_back(eid);
            return true;
        });
        env.data_store->scanIndexEquality(txn, table, int64_t(456), [&](uint64_t eid) {
            new_hits.push_back(eid);
            return true;
        });
        env.data_store->commitTransaction(txn);
        EXPECT_TRUE(old_hits.empty());
        ASSERT_EQ(new_hits.size(), 1u);
    }

    auto remove_prop = execSync(executor, "MATCH (:Person {name:'alice'})-[r:KNOWS]->(:Person) REMOVE r.since");
    ASSERT_TRUE(remove_prop.error.empty()) << remove_prop.error;
    {
        std::vector<EdgeId> hits;
        auto txn = env.data_store->beginTransaction();
        env.data_store->scanIndexEquality(txn, table, int64_t(456), [&](uint64_t eid) {
            hits.push_back(eid);
            return true;
        });
        env.data_store->commitTransaction(txn);
        EXPECT_TRUE(hits.empty());
    }
}

TEST_F(IndexE2ETest, UniqueIndexSetRollsBackProperty) {
    createLabel(env, "Person",
                {
                    {0, "name", PropertyType::STRING, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});
    auto ddl = execSync(executor, "CREATE UNIQUE INDEX idx_person_name FOR (n:Person) ON (n.name)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;
    auto create = execSync(executor, "CREATE (a:Person {name:'alice'}), (b:Person {name:'bob'})");
    ASSERT_TRUE(create.error.empty()) << create.error;

    bool threw = false;
    try {
        auto set = execSync(executor, "MATCH (n:Person {name:'bob'}) SET n.name = 'alice'");
        threw = !set.error.empty();
    } catch (const std::runtime_error& e) {
        threw = true;
        EXPECT_NE(std::string(e.what()).find("unique index"), std::string::npos);
    }
    EXPECT_TRUE(threw);

    auto alice = runQuery(executor, "MATCH (n:Person {name:'alice'}) RETURN n");
    auto bob = runQuery(executor, "MATCH (n:Person {name:'bob'}) RETURN n");
    EXPECT_EQ(alice.size(), 1u);
    EXPECT_EQ(bob.size(), 1u);
}

TEST_F(IndexE2ETest, WeakIndexUsedWhenNodeCreatedAfterIndex) {
    createLabel(env, "Message", {});
    createLabel(env, "Post",
                {
                    {0, "id", PropertyType::INT64, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});
    auto ddl = execSync(executor, "CREATE INDEX idx_msg_id FOR (n:Message) ON (n.id)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;

    auto create = execSync(executor, "CREATE (n:Post:Message {id: 123})");
    ASSERT_TRUE(create.error.empty()) << create.error;

    auto rows = runQuery(executor, "MATCH (n:Message) WHERE n.id = 123 RETURN n");
    ASSERT_EQ(rows.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<VertexValue>(rows[0][0]));
    EXPECT_EQ(std::get<VertexValue>(rows[0][0]).id, 1u);
}

TEST_F(IndexE2ETest, StrongIndexPointLookup) {
    createLabel(env, "Message", {});
    createLabel(env, "Post",
                {
                    {0, "creationDate", PropertyType::INT64, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});
    auto create = execSync(executor, "CREATE (n:Post:Message {creationDate: 123})");
    ASSERT_TRUE(create.error.empty()) << create.error;
    auto ddl = execSync(executor, "CREATE INDEX idx_msg_pdate FOR (n:Message) ON (n::Post.creationDate)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;

    auto rows = runQuery(executor, "MATCH (n:Message) WHERE n::Post.creationDate = 123 RETURN n");
    ASSERT_EQ(rows.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<VertexValue>(rows[0][0]));
    EXPECT_EQ(std::get<VertexValue>(rows[0][0]).id, 1u);
}

TEST_F(IndexE2ETest, MixedStrongWeakCompositeIndexQuery) {
    createLabel(env, "Message", {});
    createLabel(env, "Post",
                {
                    {0, "creationDate", PropertyType::INT64, false, std::nullopt},
                    {1, "browserUsed", PropertyType::STRING, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});
    auto create = execSync(executor, "CREATE (n:Post:Message {creationDate: 123, browserUsed: 'Firefox'})");
    ASSERT_TRUE(create.error.empty()) << create.error;
    auto ddl = execSync(executor, "CREATE INDEX idx_msg_mix FOR (n:Message) ON "
                                  "(n::Post.creationDate, n.browserUsed)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;

    auto rows = runQuery(executor, "MATCH (n:Message) WHERE n::Post.creationDate = 123 AND n.browserUsed = 'Firefox' "
                                   "RETURN n");
    ASSERT_EQ(rows.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<VertexValue>(rows[0][0]));
    EXPECT_EQ(std::get<VertexValue>(rows[0][0]).id, 1u);
}

TEST_F(IndexE2ETest, StrongIndexRejectsMissingSourceProperty) {
    createLabel(env, "Message", {});
    createLabel(env, "Post",
                {
                    {0, "creationDate", PropertyType::INT64, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});
    auto ddl = execSync(executor, "CREATE INDEX idx_msg_bad FOR (n:Message) ON (n::Post.missing)");
    EXPECT_FALSE(ddl.error.empty());
}

TEST_F(IndexE2ETest, WeakIndexBackfillConflictSetsError) {
    createLabel(env, "A",
                {
                    {0, "creationDate", PropertyType::INT64, false, std::nullopt},
                });
    createLabel(env, "B",
                {
                    {0, "creationDate", PropertyType::INT64, false, std::nullopt},
                });
    createLabel(env, "Message", {});
    auto a_opt = blockingWait(env.async_meta->getLabelId("A"));
    auto b_opt = blockingWait(env.async_meta->getLabelId("B"));
    auto m_opt = blockingWait(env.async_meta->getLabelId("Message"));
    ASSERT_TRUE(a_opt.has_value());
    ASSERT_TRUE(b_opt.has_value());
    ASSERT_TRUE(m_opt.has_value());
    LabelId aid = *a_opt;
    LabelId bid = *b_opt;
    LabelId mid = *m_opt;

    Properties props_a(1);
    props_a[0] = int64_t(123);
    Properties props_b(1);
    props_b[0] = int64_t(456);
    Properties props_m;
    std::vector<std::pair<LabelId, Properties>> label_props;
    label_props.emplace_back(aid, std::move(props_a));
    label_props.emplace_back(bid, std::move(props_b));
    label_props.emplace_back(mid, std::move(props_m));
    auto txn = env.data_store->beginTransaction();
    ASSERT_TRUE(env.data_store->insertVertex(txn, 1, label_props));
    env.data_store->commitTransaction(txn);

    QueryExecutor executor(*env.async_data, *env.async_meta, {});
    auto ddl = execSync(executor, "CREATE INDEX idx_msg_date FOR (n:Message) ON (n.creationDate)");
    EXPECT_FALSE(ddl.error.empty());
    EXPECT_NE(ddl.error.find("conflicting"), std::string::npos);

    auto idx = env.async_meta->schema().findIndexByName("idx_msg_date");
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(idx->state, IndexState::ERROR);
}

TEST_F(IndexE2ETest, DdlCreateStrongIndexBackfill) {
    createLabel(env, "Message", {});
    createLabel(env, "Post",
                {
                    {0, "creationDate", PropertyType::INT64, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});
    auto create = execSync(executor, "CREATE (n:Post:Message {creationDate: 123})");
    ASSERT_TRUE(create.error.empty()) << create.error;

    auto ddl = execSync(executor, "CREATE INDEX idx_msg_pdate FOR (n:Message) ON (n::Post.creationDate)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;

    auto idx = env.async_meta->schema().findIndexByName("idx_msg_pdate");
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(idx->state, IndexState::PUBLIC);

    auto table = vidxTableById(idx->index_id);
    std::vector<VertexId> results;
    auto txn = env.data_store->beginTransaction();
    env.data_store->scanIndexEquality(txn, table, int64_t(123), [&](uint64_t eid) {
        results.push_back(eid);
        return true;
    });
    env.data_store->commitTransaction(txn);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], 1u);
}

TEST_F(IndexE2ETest, EndToEndCreateIndexAndQuery) {
    // Full flow: create label → insert data → create index → query via index
    createLabel(env, "Person",
                {
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
    auto ddl_result = execSync(executor, "CREATE INDEX idx_age FOR (n:Person) ON (n.age)");
    ASSERT_TRUE(ddl_result.error.empty()) << ddl_result.error;

    // Query via IndexScan should now return correct results after backfill
    rows = runQuery(executor, "MATCH (n:Person) WHERE n.age = 35 RETURN n");
    ASSERT_EQ(rows.size(), 1u);

    // SHOW INDEXES
    auto show = execSync(executor, "SHOW INDEXES");
    ASSERT_EQ(show.rows.size(), 1u);
    EXPECT_EQ(std::get<std::string>(show.rows[0][0]), "idx_age");
}

// ==================== Duplicate Index Detection ====================

TEST_F(IndexE2ETest, DdlCreateDuplicateIndex) {
    createLabel(env, "Person",
                {
                    {0, "name", PropertyType::STRING, false, std::nullopt},
                    {0, "age", PropertyType::INT64, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});

    // First index on name should succeed
    auto result = execSync(executor, "CREATE INDEX idx_name FOR (n:Person) ON (n.name)");
    ASSERT_TRUE(result.error.empty()) << result.error;

    // Same name should fail
    result = execSync(executor, "CREATE INDEX idx_name FOR (n:Person) ON (n.name)");
    EXPECT_FALSE(result.error.empty());

    // Different name but same property should fail (duplicate prop set)
    result = execSync(executor, "CREATE INDEX idx_name2 FOR (n:Person) ON (n.name)");
    EXPECT_FALSE(result.error.empty());

    // Different property on same label should succeed
    result = execSync(executor, "CREATE INDEX idx_age FOR (n:Person) ON (n.age)");
    ASSERT_TRUE(result.error.empty()) << result.error;
}

// ==================== Unique Index Backfill ====================

TEST_F(IndexE2ETest, DdlCreateUniqueIndexWithDuplicateData) {
    createLabel(env, "Person",
                {
                    {0, "name", PropertyType::STRING, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});

    // Insert two vertices with the same name
    runQuery(executor, "CREATE (n:Person {name: 'alice'})");
    runQuery(executor, "CREATE (n:Person {name: 'alice'})");

    // Create unique index — should fail due to duplicate values
    auto result = execSync(executor, "CREATE UNIQUE INDEX idx_name FOR (n:Person) ON (n.name)");
    EXPECT_FALSE(result.error.empty());
    EXPECT_NE(result.error.find("duplicate"), std::string::npos);

    // Verify index is in ERROR state
    auto info = blockingWait(env.async_meta->getIndex("idx_name"));
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->state, IndexState::ERROR);
}

TEST_F(IndexE2ETest, DdlCreateUniqueIndexWithUniqueData) {
    createLabel(env, "Person",
                {
                    {0, "name", PropertyType::STRING, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});

    // Insert two vertices with different names
    runQuery(executor, "CREATE (n:Person {name: 'alice'})");
    runQuery(executor, "CREATE (n:Person {name: 'bob'})");

    // Create unique index — should succeed
    auto result = execSync(executor, "CREATE UNIQUE INDEX idx_name FOR (n:Person) ON (n.name)");
    ASSERT_TRUE(result.error.empty()) << result.error;

    // Verify index is in PUBLIC state
    auto info = blockingWait(env.async_meta->getIndex("idx_name"));
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->state, IndexState::PUBLIC);
    EXPECT_TRUE(info->unique);
}

// ==================== Write Path Unique Constraint ====================

TEST_F(IndexE2ETest, InsertDuplicateValueOnUniqueIndex) {
    createLabel(env, "Person",
                {
                    {0, "name", PropertyType::STRING, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});

    // Create unique index on clean data
    runQuery(executor, "CREATE (n:Person {name: 'alice'})");
    auto ddl = execSync(executor, "CREATE UNIQUE INDEX idx_name FOR (n:Person) ON (n.name)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;

    // Verify it's PUBLIC
    auto info = blockingWait(env.async_meta->getIndex("idx_name"));
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->state, IndexState::PUBLIC);

    // Insert a different name — should succeed
    auto result = execSync(executor, "CREATE (n:Person {name: 'bob'})");
    EXPECT_TRUE(result.error.empty()) << result.error;

    // Insert duplicate name — should fail (unique constraint), vertex not created
    result = execSync(executor, "CREATE (n:Person {name: 'alice'})");
    EXPECT_TRUE(result.rows.empty());
    // Verify only 1 'alice' vertex exists
    auto scan_result = execSync(executor, "MATCH (n:Person) WHERE n.name = 'alice' RETURN n");
    EXPECT_EQ(scan_result.rows.size(), 1u);
}

// ==================== Composite Index Tests ====================

TEST_F(IndexE2ETest, DdlParserCreateCompositeIndex) {
    auto stmt = IndexDdlParser::tryParse("CREATE INDEX idx_age_name FOR (n:Person) ON (n.age, n.name)");
    ASSERT_TRUE(stmt.has_value());
    EXPECT_EQ(stmt->type, IndexDdlStatement::CREATE_VERTEX_INDEX);
    EXPECT_EQ(stmt->index_name, "idx_age_name");
    EXPECT_EQ(stmt->label_name, "Person");
    ASSERT_EQ(stmt->accessors.size(), 2u);
    EXPECT_EQ(stmt->accessors[0].property_name, "age");
    EXPECT_EQ(stmt->accessors[1].property_name, "name");
    EXPECT_FALSE(stmt->unique);
}

TEST_F(IndexE2ETest, DdlParserCreateCompositeUniqueIndex) {
    auto stmt = IndexDdlParser::tryParse("CREATE UNIQUE INDEX idx_id_email FOR (n:User) ON (n.id, n.email)");
    ASSERT_TRUE(stmt.has_value());
    EXPECT_TRUE(stmt->unique);
    ASSERT_EQ(stmt->accessors.size(), 2u);
    EXPECT_EQ(stmt->accessors[0].property_name, "id");
    EXPECT_EQ(stmt->accessors[1].property_name, "email");
}

TEST_F(IndexE2ETest, DdlCreateCompositeIndexViaExecutor) {
    createLabel(env, "Person",
                {
                    {0, "name", PropertyType::STRING, false, std::nullopt},
                    {0, "age", PropertyType::INT64, false, std::nullopt},
                    {0, "city", PropertyType::STRING, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});

    // Insert some data first
    runQuery(executor, "CREATE (n:Person {name: 'alice', age: 30, city: 'NYC'})");
    runQuery(executor, "CREATE (n:Person {name: 'bob', age: 25, city: 'LA'})");
    runQuery(executor, "CREATE (n:Person {name: 'charlie', age: 30, city: 'SF'})");

    // Create composite index on (age, city)
    auto result = execSync(executor, "CREATE INDEX idx_age_city FOR (n:Person) ON (n.age, n.city)");
    ASSERT_TRUE(result.error.empty()) << result.error;

    // Verify index is PUBLIC
    auto info = blockingWait(env.async_meta->getIndex("idx_age_city"));
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->state, IndexState::PUBLIC);
    ASSERT_EQ(info->property_names.size(), 2u);
    EXPECT_EQ(info->property_names[0], "age");
    EXPECT_EQ(info->property_names[1], "city");

    // Query using both indexed columns should use index scan
    auto rows = runQuery(executor, "MATCH (n:Person) WHERE n.age = 30 AND n.city = 'NYC' RETURN n");
    ASSERT_EQ(rows.size(), 1u);
    auto& val = rows[0][0];
    ASSERT_TRUE(std::holds_alternative<VertexValue>(val));
    auto& vv = std::get<VertexValue>(val);
    // Verify the correct vertex was returned
    auto props = vv.properties;
    // Should be vertex with age=30 and city='NYC' (alice)
    EXPECT_EQ(vv.id, 1u);
}

TEST_F(IndexE2ETest, DdlCreateCompositeUniqueIndexNoConflict) {
    createLabel(env, "User",
                {
                    {0, "id", PropertyType::INT64, false, std::nullopt},
                    {0, "email", PropertyType::STRING, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});

    // Insert data with unique (id, email) pairs
    runQuery(executor, "CREATE (n:User {id: 1, email: 'a@b.com'})");
    runQuery(executor, "CREATE (n:User {id: 1, email: 'c@d.com'})"); // same id, different email → OK
    runQuery(executor, "CREATE (n:User {id: 2, email: 'a@b.com'})"); // different id, same email → OK

    // Create composite unique index on (id, email)
    auto result = execSync(executor, "CREATE UNIQUE INDEX idx_user_id_email FOR (n:User) ON (n.id, n.email)");
    ASSERT_TRUE(result.error.empty()) << result.error;

    auto info = blockingWait(env.async_meta->getIndex("idx_user_id_email"));
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->state, IndexState::PUBLIC);
    EXPECT_TRUE(info->unique);
}

TEST_F(IndexE2ETest, DdlCreateCompositeUniqueIndexDuplicate) {
    createLabel(env, "User",
                {
                    {0, "id", PropertyType::INT64, false, std::nullopt},
                    {0, "email", PropertyType::STRING, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});

    // Insert two vertices with the same (id, email) pair
    runQuery(executor, "CREATE (n:User {id: 1, email: 'dup@test.com'})");
    runQuery(executor, "CREATE (n:User {id: 1, email: 'dup@test.com'})");

    // Create composite unique index — should fail due to duplicate composite values
    auto result = execSync(executor, "CREATE UNIQUE INDEX idx_user_id_email FOR (n:User) ON (n.id, n.email)");
    EXPECT_FALSE(result.error.empty());
    EXPECT_NE(result.error.find("duplicate"), std::string::npos);

    // Verify index is in ERROR state
    auto info = blockingWait(env.async_meta->getIndex("idx_user_id_email"));
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->state, IndexState::ERROR);
}

TEST_F(IndexE2ETest, InsertViolatesCompositeUniqueConstraint) {
    createLabel(env, "User",
                {
                    {0, "id", PropertyType::INT64, false, std::nullopt},
                    {0, "email", PropertyType::STRING, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});

    // Create composite unique index on clean data
    runQuery(executor, "CREATE (n:User {id: 1, email: 'a@b.com'})");
    auto ddl = execSync(executor, "CREATE UNIQUE INDEX idx_user_id_email FOR (n:User) ON (n.id, n.email)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;

    // Insert a different pair — should succeed
    auto result = execSync(executor, "CREATE (n:User {id: 1, email: 'c@d.com'})");
    EXPECT_TRUE(result.error.empty()) << result.error;

    // Insert duplicate composite pair — should fail
    result = execSync(executor, "CREATE (n:User {id: 1, email: 'a@b.com'})");
    EXPECT_TRUE(result.rows.empty());
}

TEST_F(IndexE2ETest, ShowCompositeIndex) {
    createLabel(env, "Person",
                {
                    {0, "name", PropertyType::STRING, false, std::nullopt},
                    {0, "age", PropertyType::INT64, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});

    execSync(executor, "CREATE INDEX idx_age_name FOR (n:Person) ON (n.age, n.name)");

    auto result = execSync(executor, "SHOW INDEXES");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1u);
    EXPECT_EQ(std::get<std::string>(result.rows[0][0]), "idx_age_name");
    EXPECT_EQ(std::get<std::string>(result.rows[0][1]), "Person");
    // property column should show comma-separated list
    EXPECT_EQ(std::get<std::string>(result.rows[0][2]), "age, name");
    EXPECT_EQ(std::get<std::string>(result.rows[0][4]), "PUBLIC");
}

// ==================== Edge Index E2E Tests ====================

TEST_F(IndexE2ETest, DdlCreateEdgeIndexViaExecutor) {
    createLabel(env, "Person",
                {
                    {0, "name", PropertyType::STRING, false, std::nullopt},
                });
    createEdgeLabel(env, "KNOWS",
                    {
                        {0, "since", PropertyType::INT64, false, std::nullopt},
                    });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});

    auto result = execSync(executor, "CREATE INDEX idx_knows_since FOR ()-[r:KNOWS]-() ON (r.since)");
    EXPECT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1u);
    EXPECT_NE(std::get<std::string>(result.rows[0][0]).find("Edge index created"), std::string::npos);

    // Verify state is PUBLIC
    auto info = blockingWait(env.async_meta->getIndex("idx_knows_since"));
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->state, IndexState::PUBLIC);
}

TEST_F(IndexE2ETest, DdlCreateEdgeIndexLabelNotFound) {
    createLabel(env, "Person",
                {
                    {0, "name", PropertyType::STRING, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});

    auto result = execSync(executor, "CREATE INDEX idx_bad FOR ()-[r:NONEXISTENT]-() ON (r.prop)");
    EXPECT_FALSE(result.error.empty());
    EXPECT_NE(result.error.find("Edge label not found"), std::string::npos);
}

TEST_F(IndexE2ETest, DdlCreateEdgeIndexPropertyNotFound) {
    createLabel(env, "Person",
                {
                    {0, "name", PropertyType::STRING, false, std::nullopt},
                });
    createEdgeLabel(env, "KNOWS",
                    {
                        {0, "since", PropertyType::INT64, false, std::nullopt},
                    });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});

    auto result = execSync(executor, "CREATE INDEX idx_bad FOR ()-[r:KNOWS]-() ON (r.nonexistent)");
    EXPECT_FALSE(result.error.empty());
    EXPECT_NE(result.error.find("Property not found"), std::string::npos);
}

TEST_F(IndexE2ETest, DdlCreateEdgeIndexBackfill) {
    createLabel(env, "Person",
                {
                    {0, "name", PropertyType::STRING, false, std::nullopt},
                });
    createEdgeLabel(env, "KNOWS",
                    {
                        {0, "since", PropertyType::INT64, false, std::nullopt},
                    });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});

    runQuery(executor, "CREATE (n:Person {name: 'Alice'})");
    runQuery(executor, "CREATE (n:Person {name: 'Bob'})");
    // Note: edges currently created without properties, so backfill won't find any

    auto result = execSync(executor, "CREATE INDEX idx_knows_since FOR ()-[r:KNOWS]-() ON (r.since)");
    EXPECT_TRUE(result.error.empty()) << result.error;

    auto info = blockingWait(env.async_meta->getIndex("idx_knows_since"));
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->state, IndexState::PUBLIC);
}

TEST_F(IndexE2ETest, DdlCreateUniqueEdgeIndexNoConflict) {
    createLabel(env, "Person",
                {
                    {0, "name", PropertyType::STRING, false, std::nullopt},
                });
    createEdgeLabel(env, "KNOWS",
                    {
                        {0, "since", PropertyType::INT64, false, std::nullopt},
                    });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});

    auto result = execSync(executor, "CREATE UNIQUE INDEX idx_uknows_since FOR ()-[r:KNOWS]-() ON (r.since)");
    EXPECT_TRUE(result.error.empty()) << result.error;

    auto info = blockingWait(env.async_meta->getIndex("idx_uknows_since"));
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->state, IndexState::PUBLIC);
    EXPECT_TRUE(info->unique);
}

TEST_F(IndexE2ETest, ShowEdgeIndexes) {
    createLabel(env, "Person",
                {
                    {0, "name", PropertyType::STRING, false, std::nullopt},
                });
    createEdgeLabel(env, "KNOWS",
                    {
                        {0, "since", PropertyType::INT64, false, std::nullopt},
                    });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});
    execSync(executor, "CREATE INDEX idx_knows_since FOR ()-[r:KNOWS]-() ON (r.since)");

    auto result = execSync(executor, "SHOW INDEXES");
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.rows.size(), 1u);
    EXPECT_EQ(std::get<std::string>(result.rows[0][0]), "idx_knows_since");
    EXPECT_EQ(std::get<std::string>(result.rows[0][1]), "KNOWS");
    EXPECT_EQ(std::get<std::string>(result.rows[0][2]), "since");
    EXPECT_EQ(std::get<std::string>(result.rows[0][3]), "false");
    EXPECT_EQ(std::get<std::string>(result.rows[0][4]), "PUBLIC");
}

TEST_F(IndexE2ETest, DropEdgeIndex) {
    createLabel(env, "Person",
                {
                    {0, "name", PropertyType::STRING, false, std::nullopt},
                });
    createEdgeLabel(env, "KNOWS",
                    {
                        {0, "since", PropertyType::INT64, false, std::nullopt},
                    });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});
    execSync(executor, "CREATE INDEX idx_knows_since FOR ()-[r:KNOWS]-() ON (r.since)");

    auto drop_result = execSync(executor, "DROP INDEX idx_knows_since");
    EXPECT_TRUE(drop_result.error.empty()) << drop_result.error;

    auto show_result = execSync(executor, "SHOW INDEXES");
    EXPECT_EQ(show_result.rows.size(), 0u);
}

TEST_F(IndexE2ETest, DdlCreateCompositeEdgeIndex) {
    createLabel(env, "Person",
                {
                    {0, "name", PropertyType::STRING, false, std::nullopt},
                });
    createEdgeLabel(env, "KNOWS",
                    {
                        {0, "since", PropertyType::INT64, false, std::nullopt},
                        {0, "weight", PropertyType::DOUBLE, false, std::nullopt},
                    });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});

    auto result = execSync(executor, "CREATE INDEX idx_knows_sw FOR ()-[r:KNOWS]-() ON (r.since, r.weight)");
    EXPECT_TRUE(result.error.empty()) << result.error;

    auto info = blockingWait(env.async_meta->getIndex("idx_knows_sw"));
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->state, IndexState::PUBLIC);
    EXPECT_EQ(info->property_names.size(), 2u);

    auto show = execSync(executor, "SHOW INDEXES");
    ASSERT_EQ(show.rows.size(), 1u);
    EXPECT_EQ(std::get<std::string>(show.rows[0][2]), "since, weight");
}

TEST_F(IndexE2ETest, DdlCreateCompositeUniqueEdgeIndex) {
    createLabel(env, "Person",
                {
                    {0, "name", PropertyType::STRING, false, std::nullopt},
                });
    createEdgeLabel(env, "KNOWS",
                    {
                        {0, "since", PropertyType::INT64, false, std::nullopt},
                        {0, "weight", PropertyType::DOUBLE, false, std::nullopt},
                    });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});

    auto result = execSync(executor, "CREATE UNIQUE INDEX idx_uknows_sw FOR ()-[r:KNOWS]-() ON (r.since, r.weight)");
    EXPECT_TRUE(result.error.empty()) << result.error;

    auto info = blockingWait(env.async_meta->getIndex("idx_uknows_sw"));
    ASSERT_TRUE(info.has_value());
    EXPECT_TRUE(info->unique);
    EXPECT_EQ(info->state, IndexState::PUBLIC);
}

TEST_F(IndexE2ETest, DdlCreateDuplicateEdgeIndex) {
    createLabel(env, "Person",
                {
                    {0, "name", PropertyType::STRING, false, std::nullopt},
                });
    createEdgeLabel(env, "KNOWS",
                    {
                        {0, "since", PropertyType::INT64, false, std::nullopt},
                    });

    QueryExecutor executor(*env.async_data, *env.async_meta, {});

    execSync(executor, "CREATE INDEX idx_knows_since FOR ()-[r:KNOWS]-() ON (r.since)");
    auto result = execSync(executor, "CREATE INDEX idx_knows_since FOR ()-[r:KNOWS]-() ON (r.since)");
    EXPECT_FALSE(result.error.empty());
    EXPECT_NE(result.error.find("duplicate"), std::string::npos);
}