#include <gtest/gtest.h>

#include "common/config/env_vars.hpp"
#include "storage/stats/stats_journal.hpp"
#include "common/types/constants.hpp"
#include "query/executor/query_executor.hpp"

#include "query/parser/index_ddl_parser.hpp"
#include "storage/data/async_graph_data_store.hpp"
#include "storage/data/sync_graph_data_store.hpp"
#include "storage/index/index_admission.hpp"
#include "storage/index/index_builder.hpp"
#include "storage/index/index_runtime_registry.hpp"
#include "storage/index/index_test_hooks.hpp"
#include "storage/io_scheduler.hpp"
#include "storage/kv/value_codec.hpp"
#include "storage/meta/async_graph_meta_store.hpp"
#include "storage/meta/sync_graph_meta_store.hpp"
#include "storage/session/schema_generation_tracker.hpp"
#include "storage/session/schema_pin.hpp"
#include "storage/wt_table_ops.hpp"
#include "test_chunk_helpers.hpp"
#include "test_index_env.hpp"
#include "test_sync_store.hpp"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <folly/coro/BlockingWait.h>
#include <future>
#include <set>
#include <spdlog/spdlog.h>
#include <string>
#include <thread>

using namespace eugraph;
using namespace eugraph::compute;
using namespace folly::coro;

using eugraph::test::ScopedEnvVar;
using eugraph::test::TestEnv;

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

// 把 caller-owned-buffer 的扫描流全部消费掉并计数
template <typename T> size_t drain_batches(folly::coro::AsyncGenerator<std::span<const T>> gen) {
    size_t count = 0;
    while (auto batch = blockingWait(gen.next()))
        count += batch->size();
    return count;
}

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
            auto rows = eugraph::test::chunkToRows(*chunk);
            for (auto& row : rows) {
                result.rows.push_back(std::move(row));
            }
        }
        if (ctx->should_commit) {
            co_await ctx->commitAsync();
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
            auto rows = eugraph::test::chunkToRows(*chunk);
            for (auto& row : rows) {
                result.rows.push_back(std::move(row));
            }
        }
        if (ctx->should_commit) {
            co_await ctx->commitAsync();
        }
    }));
    return result.rows;
}

static std::string explainQuery(QueryExecutor& executor, const std::string& query) {
    auto rows = runQuery(executor, "EXPLAIN " + query);
    std::string plan;
    for (const auto& row : rows) {
        if (!row.empty() && std::holds_alternative<std::string>(row[0]))
            plan += std::get<std::string>(row[0]) + "\n";
    }
    return plan;
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
            env.data_store->insertIndexEntry(txn, vidxTableById(idx.index_id), values, vid);
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
    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    auto rows = runQuery(executor, "MATCH (n:Person) WHERE n.age = 30 RETURN n");

    ASSERT_EQ(rows.size(), 1u);
    // The vertex with age=30 is vid=3
    auto& val = rows[0][0];
    ASSERT_TRUE(std::holds_alternative<VertexValuePtr>(val));
    EXPECT_EQ((*std::get<VertexValuePtr>(val)).id, 3u);
}

TEST_F(IndexE2ETest, StringIndexExactAndPrefixScan) {
    createLabel(env, "Person",
                {
                    {0, "name", PropertyType::STRING, false, std::nullopt},
                });

    auto label_def = blockingWait(env.async_meta->getLabelDef("Person"));
    ASSERT_TRUE(label_def.has_value());
    LabelId label_id = label_def->id;
    auto name_prop_id = label_def->properties[0].id;

    ASSERT_TRUE(blockingWait(env.async_meta->createVertexIndex("idx_name", "Person", {"name"}, false)));
    ASSERT_TRUE(blockingWait(env.async_meta->updateIndexState("idx_name", IndexState::PUBLIC)));
    auto updated_def = blockingWait(env.async_meta->getLabelDef("Person"));
    ASSERT_TRUE(updated_def.has_value());
    ASSERT_TRUE(env.data_store->createIndex(vidxTableById(updated_def->indexes[0].index_id)));

    const std::vector<std::string> names = {"Jose", "Joseph", "Josef", "Jose Trinidad", "Alice"};
    for (size_t i = 0; i < names.size(); ++i) {
        Properties props(1);
        props[name_prop_id] = names[i];
        insertVertexWithIndex(env, label_id, static_cast<VertexId>(i + 1), props, *updated_def);
    }

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    auto exact_plan = explainQuery(executor, "MATCH (n:Person) WHERE n.name = 'Jose' RETURN n.name");
    EXPECT_NE(exact_plan.find("IndexScan"), std::string::npos) << exact_plan;
    EXPECT_NE(exact_plan.find("mode=eq"), std::string::npos) << exact_plan;
    auto prefix_plan = explainQuery(executor, "MATCH (n:Person) WHERE n.name STARTS WITH 'Jose' RETURN n.name");
    EXPECT_NE(prefix_plan.find("IndexScan"), std::string::npos) << prefix_plan;
    EXPECT_NE(prefix_plan.find("mode=prefix"), std::string::npos) << prefix_plan;

    auto exact = runQuery(executor, "MATCH (n:Person) WHERE n.name = 'Jose' RETURN n.name");
    ASSERT_EQ(exact.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<std::string>(exact[0][0]));
    EXPECT_EQ(std::get<std::string>(exact[0][0]), "Jose");

    auto prefix = runQuery(executor, "MATCH (n:Person) WHERE n.name STARTS WITH 'Jose' RETURN n.name");
    EXPECT_EQ(prefix.size(), 4u);
    for (const auto& row : prefix) {
        ASSERT_TRUE(std::holds_alternative<std::string>(row[0]));
        EXPECT_EQ(std::get<std::string>(row[0]).rfind("Jose", 0), 0u);
    }

    auto no_match = runQuery(executor, "MATCH (n:Person) WHERE n.name = 'Jos' RETURN n.name");
    EXPECT_TRUE(no_match.empty());
}

TEST_F(IndexE2ETest, CompositeStringIndexExactAndPrefix) {
    createLabel(env, "Person",
                {
                    {0, "firstName", PropertyType::STRING, false, std::nullopt},
                    {0, "lastName", PropertyType::STRING, false, std::nullopt},
                });

    auto label_def = blockingWait(env.async_meta->getLabelDef("Person"));
    ASSERT_TRUE(label_def.has_value());
    LabelId label_id = label_def->id;
    auto first_id = label_def->properties[0].id;
    auto last_id = label_def->properties[1].id;

    ASSERT_TRUE(
        blockingWait(env.async_meta->createVertexIndex("idx_name", "Person", {"firstName", "lastName"}, false)));
    ASSERT_TRUE(blockingWait(env.async_meta->updateIndexState("idx_name", IndexState::PUBLIC)));
    auto updated_def = blockingWait(env.async_meta->getLabelDef("Person"));
    ASSERT_TRUE(updated_def.has_value());
    ASSERT_TRUE(env.data_store->createIndex(vidxTableById(updated_def->indexes[0].index_id)));

    const std::vector<std::pair<std::string, std::string>> names = {
        {"Jose", "A"}, {"Joseph", "A"}, {"Jose", "B"}, {"Alice", "A"}};
    for (size_t i = 0; i < names.size(); ++i) {
        Properties props(2);
        props[first_id] = names[i].first;
        props[last_id] = names[i].second;
        insertVertexWithIndex(env, label_id, static_cast<VertexId>(i + 1), props, *updated_def);
    }

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    auto exact_plan =
        explainQuery(executor, "MATCH (n:Person) WHERE n.firstName = 'Jose' AND n.lastName = 'A' RETURN n.firstName");
    EXPECT_NE(exact_plan.find("IndexScan"), std::string::npos) << exact_plan;
    EXPECT_NE(exact_plan.find("mode=eq"), std::string::npos) << exact_plan;
    auto prefix_plan = explainQuery(
        executor, "MATCH (n:Person) WHERE n.firstName = 'Jose' AND n.lastName STARTS WITH '' RETURN n.firstName");
    EXPECT_NE(prefix_plan.find("IndexScan"), std::string::npos) << prefix_plan;
    EXPECT_NE(prefix_plan.find("mode=prefix"), std::string::npos) << prefix_plan;

    auto exact = runQuery(executor, "MATCH (n:Person) WHERE n.firstName = 'Jose' AND n.lastName = 'A' "
                                    "RETURN n.firstName, n.lastName");
    ASSERT_EQ(exact.size(), 1u);
    ASSERT_EQ(exact[0].size(), 2u);
    ASSERT_TRUE(std::holds_alternative<std::string>(exact[0][0]));
    ASSERT_TRUE(std::holds_alternative<std::string>(exact[0][1]));
    EXPECT_EQ(std::get<std::string>(exact[0][0]), "Jose");
    EXPECT_EQ(std::get<std::string>(exact[0][1]), "A");

    // Force the composite prefix scan (EQ first column, PREFIX last column)
    // and return *both indexed string columns*: the index only yields the
    // vertex id, so both values must still come back through PE correctly.
    auto prefix = runQuery(executor, "MATCH (n:Person) WHERE n.firstName = 'Jose' AND n.lastName STARTS WITH '' "
                                     "RETURN n.firstName, n.lastName");
    ASSERT_EQ(prefix.size(), 2u);
    std::set<std::pair<std::string, std::string>> got;
    for (const auto& row : prefix) {
        ASSERT_EQ(row.size(), 2u);
        ASSERT_TRUE(std::holds_alternative<std::string>(row[0]));
        ASSERT_TRUE(std::holds_alternative<std::string>(row[1]));
        got.emplace(std::get<std::string>(row[0]), std::get<std::string>(row[1]));
    }
    EXPECT_EQ(got, (std::set<std::pair<std::string, std::string>>{{"Jose", "A"}, {"Jose", "B"}}));

    auto prefix_a = runQuery(executor, "MATCH (n:Person) WHERE n.firstName = 'Jose' AND n.lastName STARTS WITH 'A' "
                                       "RETURN n.firstName, n.lastName");
    ASSERT_EQ(prefix_a.size(), 1u);
    EXPECT_EQ(std::get<std::string>(prefix_a[0][0]), "Jose");
    EXPECT_EQ(std::get<std::string>(prefix_a[0][1]), "A");

    // Leading-column prefix on a composite index: no later accessor is
    // constrained, so the prefix scan may drop the filter. Both returned
    // string columns must still be materialized correctly.
    auto leading_plan =
        explainQuery(executor, "MATCH (n:Person) WHERE n.firstName STARTS WITH 'Jose' RETURN n.firstName, n.lastName");
    EXPECT_NE(leading_plan.find("IndexScan"), std::string::npos) << leading_plan;
    EXPECT_NE(leading_plan.find("mode=prefix"), std::string::npos) << leading_plan;
    auto leading =
        runQuery(executor, "MATCH (n:Person) WHERE n.firstName STARTS WITH 'Jose' RETURN n.firstName, n.lastName");
    ASSERT_EQ(leading.size(), 3u);
    std::set<std::pair<std::string, std::string>> leading_got;
    for (const auto& row : leading) {
        ASSERT_EQ(row.size(), 2u);
        leading_got.emplace(std::get<std::string>(row[0]), std::get<std::string>(row[1]));
    }
    EXPECT_EQ(leading_got,
              (std::set<std::pair<std::string, std::string>>{{"Jose", "A"}, {"Jose", "B"}, {"Joseph", "A"}}));
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
    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
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

TEST_F(IndexE2ETest, DdlVertexIndexBackfillCommitsInBatches) {
    createLabel(env, "Person",
                {
                    {0, "age", PropertyType::INT64, false, std::nullopt},
                });
    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    for (int i = 0; i < 25; ++i) {
        auto created = execSync(executor, "CREATE (n:Person {age: " + std::to_string(i) + "})");
        ASSERT_TRUE(created.error.empty()) << created.error;
    }

    // Force one row per transaction so the test exercises the multi-batch
    // path (the old implementation used a single transaction for all rows).
    ScopedEnvVar batch_size("EUGRAPH_INDEX_BACKFILL_BATCH", "1");
    auto ddl = execSync(executor, "CREATE INDEX idx_age FOR (n:Person) ON (n.age)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;

    auto info = blockingWait(env.async_meta->getIndex("idx_age"));
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->state, IndexState::PUBLIC);

    auto rows = runQuery(executor, "MATCH (n:Person) WHERE n.age = 7 RETURN n");
    ASSERT_EQ(rows.size(), 1u);
}

TEST_F(IndexE2ETest, DdlEdgeIndexBackfillCommitsInBatches) {
    createLabel(env, "Person",
                {
                    {0, "name", PropertyType::STRING, false, std::nullopt},
                });
    createEdgeLabel(env, "KNOWS",
                    {
                        {0, "since", PropertyType::INT64, false, std::nullopt},
                    });
    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    for (int i = 0; i < 5; ++i) {
        auto created =
            execSync(executor, "CREATE (a:Person {name: 'a" + std::to_string(i) + "'}), (b:Person {name: 'b" +
                                   std::to_string(i) + "'}), (a)-[:KNOWS {since: " + std::to_string(i) + "}]->(b)");
        ASSERT_TRUE(created.error.empty()) << created.error;
    }

    ScopedEnvVar batch_size("EUGRAPH_INDEX_BACKFILL_BATCH", "1");
    auto ddl = execSync(executor, "CREATE INDEX idx_knows_since FOR ()-[r:KNOWS]-() ON (r.since)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;

    auto info = blockingWait(env.async_meta->getIndex("idx_knows_since"));
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->state, IndexState::PUBLIC);
}

TEST_F(IndexE2ETest, DdlShowIndexesViaExecutor) {
    createLabel(env, "Person",
                {
                    {0, "name", PropertyType::STRING, false, std::nullopt},
                    {0, "age", PropertyType::INT64, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);

    // No indexes initially
    auto result = execSync(executor, "SHOW INDEXES");
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.rows.size(), 0u);
    ASSERT_EQ(result.columns.size(), 10u); // 追加 phase/progress/backlog/started_at/last_error（P3）
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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
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
    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    auto create = execSync(executor, "CREATE (n:Post:Message {creationDate: 123})");
    ASSERT_TRUE(create.error.empty()) << create.error;

    auto ddl = execSync(executor, "CREATE INDEX idx_msg_date FOR (n:Message) ON (n.creationDate)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;

    auto idx = env.async_meta->schemaSnapshot().findIndexByName("idx_msg_date");
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
    ASSERT_TRUE(std::holds_alternative<VertexValuePtr>(rows[0][0]));
    EXPECT_EQ((*std::get<VertexValuePtr>(rows[0][0])).id, 1u);
}

TEST_F(IndexE2ETest, WeakIndexMaintainsOnLabelAddRemove) {
    createLabel(env, "Message", {});
    createLabel(env, "Post",
                {
                    {0, "creationDate", PropertyType::INT64, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    auto create = execSync(executor, "CREATE (n:Post:Message {creationDate: 123})");
    ASSERT_TRUE(create.error.empty()) << create.error;
    auto ddl = execSync(executor, "CREATE INDEX idx_msg_date FOR (n:Message) ON (n.creationDate)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;

    auto idx = env.async_meta->schemaSnapshot().findIndexByName("idx_msg_date");
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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    auto create = execSync(executor, "CREATE (n:Post:Message {creationDate: 123})");
    ASSERT_TRUE(create.error.empty()) << create.error;
    auto ddl = execSync(executor, "CREATE INDEX idx_msg_date FOR (n:Message) ON (n.creationDate)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;

    auto idx = env.async_meta->schemaSnapshot().findIndexByName("idx_msg_date");
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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    auto create = execSync(executor, "CREATE (n:Post:Message {creationDate: 123})");
    ASSERT_TRUE(create.error.empty()) << create.error;
    auto ddl = execSync(executor, "CREATE INDEX idx_msg_date FOR (n:Message) ON (n.creationDate)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;

    auto idx = env.async_meta->schemaSnapshot().findIndexByName("idx_msg_date");
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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    auto ddl = execSync(executor, "CREATE INDEX idx_msg_id FOR (n:Message) ON (n.id)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;

    auto create = execSync(executor, "CREATE (n:Post:Message {id: 123})");
    ASSERT_TRUE(create.error.empty()) << create.error;

    auto rows = runQuery(executor, "MATCH (n:Message) WHERE n.id = 123 RETURN n");
    ASSERT_EQ(rows.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<VertexValuePtr>(rows[0][0]));
    EXPECT_EQ((*std::get<VertexValuePtr>(rows[0][0])).id, 1u);
}

TEST_F(IndexE2ETest, StrongIndexPointLookup) {
    createLabel(env, "Message", {});
    createLabel(env, "Post",
                {
                    {0, "creationDate", PropertyType::INT64, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    auto create = execSync(executor, "CREATE (n:Post:Message {creationDate: 123})");
    ASSERT_TRUE(create.error.empty()) << create.error;
    auto ddl = execSync(executor, "CREATE INDEX idx_msg_pdate FOR (n:Message) ON (n::Post.creationDate)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;

    auto rows = runQuery(executor, "MATCH (n:Message) WHERE n::Post.creationDate = 123 RETURN n");
    ASSERT_EQ(rows.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<VertexValuePtr>(rows[0][0]));
    EXPECT_EQ((*std::get<VertexValuePtr>(rows[0][0])).id, 1u);
}

TEST_F(IndexE2ETest, MixedStrongWeakCompositeIndexQuery) {
    createLabel(env, "Message", {});
    createLabel(env, "Post",
                {
                    {0, "creationDate", PropertyType::INT64, false, std::nullopt},
                    {1, "browserUsed", PropertyType::STRING, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    auto create = execSync(executor, "CREATE (n:Post:Message {creationDate: 123, browserUsed: 'Firefox'})");
    ASSERT_TRUE(create.error.empty()) << create.error;
    auto ddl = execSync(executor, "CREATE INDEX idx_msg_mix FOR (n:Message) ON "
                                  "(n::Post.creationDate, n.browserUsed)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;

    auto rows = runQuery(executor, "MATCH (n:Message) WHERE n::Post.creationDate = 123 AND n.browserUsed = 'Firefox' "
                                   "RETURN n");
    ASSERT_EQ(rows.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<VertexValuePtr>(rows[0][0]));
    EXPECT_EQ((*std::get<VertexValuePtr>(rows[0][0])).id, 1u);
}

TEST_F(IndexE2ETest, StrongIndexRejectsMissingSourceProperty) {
    createLabel(env, "Message", {});
    createLabel(env, "Post",
                {
                    {0, "creationDate", PropertyType::INT64, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    auto ddl = execSync(executor, "CREATE INDEX idx_msg_date FOR (n:Message) ON (n.creationDate)");
    EXPECT_FALSE(ddl.error.empty());
    EXPECT_NE(ddl.error.find("conflicting"), std::string::npos);

    auto idx = env.async_meta->schemaSnapshot().findIndexByName("idx_msg_date");
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(idx->state, IndexState::ERROR);
}

TEST_F(IndexE2ETest, DdlCreateStrongIndexBackfill) {
    createLabel(env, "Message", {});
    createLabel(env, "Post",
                {
                    {0, "creationDate", PropertyType::INT64, false, std::nullopt},
                });

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    auto create = execSync(executor, "CREATE (n:Post:Message {creationDate: 123})");
    ASSERT_TRUE(create.error.empty()) << create.error;

    auto ddl = execSync(executor, "CREATE INDEX idx_msg_pdate FOR (n:Message) ON (n::Post.creationDate)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;

    auto idx = env.async_meta->schemaSnapshot().findIndexByName("idx_msg_pdate");
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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);

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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);

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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);

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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);

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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);

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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);

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
    ASSERT_TRUE(std::holds_alternative<VertexValuePtr>(val));
    auto& vv = (*std::get<VertexValuePtr>(val));
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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);

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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);

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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);

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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);

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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);

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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);

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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);

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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);

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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);

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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);

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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);

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

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);

    execSync(executor, "CREATE INDEX idx_knows_since FOR ()-[r:KNOWS]-() ON (r.since)");
    auto result = execSync(executor, "CREATE INDEX idx_knows_since FOR ()-[r:KNOWS]-() ON (r.since)");
    EXPECT_FALSE(result.error.empty());
    EXPECT_NE(result.error.find("duplicate"), std::string::npos);
}
// ==================== 弱 accessor 索引：顶点由多个来源标签供给 ====================

// 回归防护（**已确认这条路径是好的**）：LDBC sf0.1 的 Message 由 Comment/Post
// 两个文件供给，排查 "Message 点查返回 0" 时第一嫌疑是"两个来源标签给 id 分配的
// prop_id 不同"。本用例把该假设钉住：即使 prop_id 不一致（0 vs 2），
// 弱 accessor 回填与点查都正确 —— 所以那个缺陷**不在**这里，别再往这个方向查。
//
// （真正的原因见 index-e2e 之外：在大标签上用 CREATE INDEX 建派生标签索引时，
//   回填写进索引的值不是该属性的真实值 —— 见 docs/query/known-defects-todo.md）
TEST_F(IndexE2ETest, WeakIndexPointLookupWithDisagreeingSourcePropIds) {
    // Comment: id -> prop_id 0
    createLabel(env, "Comment",
                {{0, "id", PropertyType::INT64, false, std::nullopt},
                 {1, "content", PropertyType::STRING, false, std::nullopt}});
    // Post: id -> prop_id 2（与 Comment 不一致）
    createLabel(env, "Post",
                {{0, "imageFile", PropertyType::STRING, false, std::nullopt},
                 {1, "language", PropertyType::STRING, false, std::nullopt},
                 {2, "id", PropertyType::INT64, false, std::nullopt}});
    createLabel(env, "Message", {});

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    ASSERT_TRUE(execSync(executor, "CREATE (n:Comment:Message {id: 7, content: 'hi'})").error.empty());
    ASSERT_TRUE(
        execSync(executor, "CREATE (n:Post:Message {imageFile: 'p.jpg', language: 'en', id: 7})").error.empty());

    auto ddl = execSync(executor, "CREATE INDEX idx_msg_id FOR (n:Message) ON (n.id)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;
    auto idx = env.async_meta->schemaSnapshot().findIndexByName("idx_msg_id");
    ASSERT_TRUE(idx.has_value());

    // 判据 1：索引表里两个顶点都要有条目（回填不能漏）
    std::vector<VertexId> in_index;
    {
        auto txn = env.data_store->beginTransaction();
        env.data_store->scanIndexEquality(txn, vidxTableById(idx->index_id), int64_t(7), [&](uint64_t eid) {
            in_index.push_back(static_cast<VertexId>(eid));
            return true;
        });
        env.data_store->commitTransaction(txn);
    }
    EXPECT_EQ(in_index.size(), 2u) << "id=7 的索引条目数不对（回填漏了顶点）";

    // 判据 2：点查必须命中两个顶点（用户可见的行为）
    auto rows = runQuery(executor, "MATCH (n:Message {id: 7}) RETURN n");
    EXPECT_EQ(rows.size(), 2u) << "Message{id:7} 点查漏结果";
}

// 同上，打印 accessor 解析结果，便于怀疑"弱 accessor 取错属性"时先看这里：
// 期望 is_strong=0、prop='id'，且两个来源顶点都进索引
TEST_F(IndexE2ETest, WeakIndexBackfillAcrossTwoSourceLabels) {
    createLabel(env, "Comment",
                {{0, "id", PropertyType::INT64, false, std::nullopt},
                 {1, "content", PropertyType::STRING, false, std::nullopt}});
    createLabel(env, "Post",
                {{0, "imageFile", PropertyType::STRING, false, std::nullopt},
                 {1, "language", PropertyType::STRING, false, std::nullopt},
                 {2, "id", PropertyType::INT64, false, std::nullopt}});
    createLabel(env, "Message", {});
    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    ASSERT_TRUE(execSync(executor, "CREATE (n:Comment:Message {id: 7, content: 'hi'})").error.empty());
    ASSERT_TRUE(
        execSync(executor, "CREATE (n:Post:Message {imageFile: 'p.jpg', language: 'en', id: 7})").error.empty());
    ASSERT_TRUE(execSync(executor, "CREATE INDEX idx_m_id FOR (n:Message) ON (n.id)").error.empty());

    auto idx = env.async_meta->schemaSnapshot().findIndexByName("idx_m_id");
    ASSERT_TRUE(idx.has_value());
    std::vector<VertexId> got;
    auto txn = env.data_store->beginTransaction();
    env.data_store->scanIndexEquality(txn, vidxTableById(idx->index_id), int64_t(7), [&](uint64_t eid) {
        got.push_back(static_cast<VertexId>(eid));
        return true;
    });
    env.data_store->commitTransaction(txn);
    // 打印 accessor 与命中数，判断是"没回填"还是"回填值不对"
    for (const auto& a : idx->accessors)
        std::printf("[probe] accessor is_strong=%d source_label=%u prop='%s'\n", (int)a.is_strong, a.source_label_id,
                    a.property_name.c_str());
    std::printf("[probe] Message index_id=%u 命中=%zu\n", idx->index_id, got.size());

    // 再查"任意值"：若按 null 前缀能扫出条目，说明条目存在但值不对
    size_t any_entries = 0;
    auto txn2 = env.data_store->beginTransaction();
    env.data_store->scanIndexEquality(txn2, vidxTableById(idx->index_id), int64_t(999999), [&](uint64_t) {
        ++any_entries;
        return true;
    });
    env.data_store->commitTransaction(txn2);
    std::printf("[probe] 用不存在的值扫描命中=%zu（>0 说明 scan 有返回）\n", any_entries);
}

// 弱 accessor 索引的回填**在 3000 顶点规模上完整**（本用例守住这一点）。
//
// 历史：同一条代码路径曾在 sf0.1 的 28.6 万顶点 `Message` 标签上"回填不落地"（索引表 0 条、
// 点查返回 0）。该问题已随 main 的 session 修复（#241/#244：session 永不共享）消除，
// 并在 sf0.1 上按判据验证（索引被规划器选中、60/60 真实 id 命中）——见
// 与端点/索引相关的设计约束见 [loader-design.md](../../docs/program/design/loader-design.md) §4、§12。
// 本用例仍作为小规模基线保留：规模相关的回填回归应先在这里失败。
TEST_F(IndexE2ETest, WeakIndexBackfillScalesWithVertexCount) {
    createLabel(env, "Comment",
                {{0, "id", PropertyType::INT64, false, std::nullopt},
                 {1, "content", PropertyType::STRING, false, std::nullopt}});
    createLabel(env, "Message", {});
    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);

    const int N = 3000;
    for (int i = 0; i < N; ++i) {
        auto r = execSync(executor, "CREATE (n:Comment:Message {id: " + std::to_string(i) + ", content: 'x'})");
        ASSERT_TRUE(r.error.empty()) << r.error;
    }

    auto ddl = execSync(executor, "CREATE INDEX idx_scale FOR (n:Message) ON (n.id)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;
    auto idx = env.async_meta->schemaSnapshot().findIndexByName("idx_scale");
    ASSERT_TRUE(idx.has_value());

    size_t hits = 0;
    auto txn = env.data_store->beginTransaction();
    for (int i = 0; i < N; ++i)
        env.data_store->scanIndexEquality(txn, vidxTableById(idx->index_id), int64_t(i), [&](uint64_t) {
            ++hits;
            return true;
        });
    env.data_store->commitTransaction(txn);
    std::printf("[scale] N=%d 索引命中=%zu（期望 %d）\n", N, hits, N);
    EXPECT_EQ(hits, static_cast<size_t>(N)) << "回填不完整";
}

// ==================== 索引扫描流式回归（结果数 > BATCH 1024）====================

TEST_F(IndexE2ETest, VertexIndexEqualityScanStreamsBeyondBatch) {
    constexpr size_t N = 3000; // > 1024
    auto lid =
        blockingWait(env.async_meta->createLabel("Bulk", {PropertyDef{0, "k", PropertyType::STRING, false, {}}}));
    ASSERT_NE(lid, INVALID_LABEL_ID);
    ASSERT_TRUE(blockingWait(env.async_data->createLabel(lid)));
    const std::string table = vidxTable(lid, 0);
    ASSERT_TRUE(env.data_store->createIndex(table));

    auto lease = env.session_pool->acquire();
    ASSERT_TRUE(lease.has_value());
    GraphSession& session = **lease;
    ASSERT_TRUE(session.begin_write());
    for (size_t i = 0; i < N; ++i)
        ASSERT_TRUE(env.data_store->insertIndexEntry(session, table, std::string("dup"), i));
    ASSERT_TRUE(session.commit().committed());

    ASSERT_TRUE(session.begin_read());
    auto bound = env.async_data->for_session(session);
    std::vector<VertexId> scan_batch;
    EXPECT_EQ(drain_batches(bound.scanVerticesByIndex(lid, 0, std::string("dup"), scan_batch)), N)
        << "索引等值扫描必须流式返回全部命中，不得截断或 OOM";
    EXPECT_TRUE(session.commit().committed());
}

TEST_F(IndexE2ETest, VertexIndexRangeScanStreamsBeyondBatch) {
    constexpr size_t N = 3000; // > 1024
    auto lid =
        blockingWait(env.async_meta->createLabel("BulkRange", {PropertyDef{0, "k", PropertyType::INT64, false, {}}}));
    ASSERT_NE(lid, INVALID_LABEL_ID);
    ASSERT_TRUE(blockingWait(env.async_data->createLabel(lid)));
    const std::string table = vidxTable(lid, 0);
    ASSERT_TRUE(env.data_store->createIndex(table));

    auto lease = env.session_pool->acquire();
    ASSERT_TRUE(lease.has_value());
    GraphSession& session = **lease;
    ASSERT_TRUE(session.begin_write());
    for (size_t i = 0; i < N; ++i)
        ASSERT_TRUE(env.data_store->insertIndexEntry(session, table, static_cast<int64_t>(i), i));
    ASSERT_TRUE(session.commit().committed());

    ASSERT_TRUE(session.begin_read());
    auto bound = env.async_data->for_session(session);
    // range 下界是**排他**的（与既有 scanIndexRange 语义一致）⇒ 用 -1 覆盖 0..N-1
    const std::optional<PropertyValue> start = PropertyValue{static_cast<int64_t>(-1)};
    const std::optional<PropertyValue> end = PropertyValue{static_cast<int64_t>(N)};
    std::vector<VertexId> scan_batch;
    EXPECT_EQ(drain_batches(bound.scanVerticesByIndexRange(lid, 0, start, end, scan_batch)), N)
        << "索引 range 扫描必须流式返回全部命中";
    EXPECT_TRUE(session.commit().committed());
}

TEST_F(IndexE2ETest, EdgeIndexScanStreamsBeyondBatch) {
    constexpr size_t N = 3000; // > 1024
    auto elid = blockingWait(
        env.async_meta->createEdgeLabel("BulkRel", {PropertyDef{0, "k", PropertyType::STRING, false, {}}}));
    ASSERT_NE(elid, INVALID_EDGE_LABEL_ID);
    ASSERT_TRUE(blockingWait(env.async_data->createEdgeLabel(elid)));
    const std::string table = eidxTable(elid, 0);
    ASSERT_TRUE(env.data_store->createIndex(table));

    auto lease = env.session_pool->acquire();
    ASSERT_TRUE(lease.has_value());
    GraphSession& session = **lease;
    ASSERT_TRUE(session.begin_write());
    for (size_t i = 0; i < N; ++i) {
        auto payload = ValueCodec::encodeEdgeAdjacency(static_cast<VertexId>(i), static_cast<VertexId>(i + 1), elid);
        ASSERT_TRUE(env.data_store->insertIndexEntry(session, table, std::string("dup"), i, payload));
    }
    ASSERT_TRUE(session.commit().committed());

    ASSERT_TRUE(session.begin_read());
    auto bound = env.async_data->for_session(session);
    std::vector<EdgeIndexScanEntry> scan_batch;
    EXPECT_EQ(drain_batches(bound.scanEdgesByIndex(elid, 0, std::string("dup"), scan_batch)), N)
        << "边索引扫描必须流式返回全部命中（旧实现会截断）";
    EXPECT_TRUE(session.commit().committed());
}

// ==================== v2 运行时注册表：DDL 生命周期 ====================

TEST_F(IndexE2ETest, IndexRuntimeRegistryTracksDdlLifecycle) {
    createLabel(env, "Person", {{0, "name", PropertyType::STRING, false, std::nullopt}});

    IndexRuntimeRegistry registry; // 每图一份（测试里挂在 executor 上）
    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    executor.setIndexRegistry(&registry);

    ASSERT_TRUE(execSync(executor, "CREATE INDEX idx_person_name FOR (n:Person) ON (n.name)").error.empty());

    auto label_def = blockingWait(env.async_meta->getLabelDef("Person"));
    ASSERT_TRUE(label_def.has_value());
    ASSERT_EQ(label_def->indexes.size(), 1u);
    const uint32_t index_id = label_def->indexes[0].index_id;
    ASSERT_NE(index_id, 0u);

    auto runtime = registry.find(index_id);
    ASSERT_TRUE(runtime);
    EXPECT_EQ(runtime->phase(), IndexPhase::kPublic);
    EXPECT_EQ(runtime->lifecycle(), IndexLifecycle::kActive);
    EXPECT_EQ(runtime->indexTable(), vidxTableById(index_id));
    EXPECT_EQ(runtime->deltaTable(), indexDeltaTable(index_id));
    EXPECT_TRUE(registry.plannerSelectable(index_id, env.async_meta->schemaGeneration()));

    ASSERT_TRUE(execSync(executor, "DROP INDEX idx_person_name").error.empty());
    EXPECT_EQ(registry.size(), 0u);
    EXPECT_FALSE(registry.plannerSelectable(index_id, env.async_meta->schemaGeneration()));
}

TEST_F(IndexE2ETest, IndexRuntimeRegistryMarksFailedUniqueBuildAsError) {
    createLabel(env, "Person", {{0, "name", PropertyType::STRING, false, std::nullopt}});
    QueryExecutor writer(*env.async_data, *env.async_meta, *env.session_pool, {});
    writer.setSchemaGenerationTracker(env.schema_generations);
    ASSERT_TRUE(execSync(writer, "CREATE (a:Person {name:'dup'}), (b:Person {name:'dup'})").error.empty());

    IndexRuntimeRegistry registry;
    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    executor.setIndexRegistry(&registry);

    // 回填发现重复 ⇒ 语句报错、索引置 ERROR、运行时 phase=ERROR（绝不 PUBLIC）。
    const auto result = execSync(executor, "CREATE UNIQUE INDEX idx_person_name FOR (n:Person) ON (n.name)");
    EXPECT_FALSE(result.error.empty());

    auto label_def = blockingWait(env.async_meta->getLabelDef("Person"));
    ASSERT_TRUE(label_def.has_value());
    ASSERT_EQ(label_def->indexes.size(), 1u);
    EXPECT_EQ(label_def->indexes[0].state, IndexState::ERROR);

    auto runtime = registry.find(label_def->indexes[0].index_id);
    ASSERT_TRUE(runtime);
    EXPECT_EQ(runtime->phase(), IndexPhase::kError);
    EXPECT_FALSE(runtime->lastError().empty()); // SHOW INDEXES.last_error 有内容
    EXPECT_FALSE(registry.plannerSelectable(label_def->indexes[0].index_id, env.async_meta->schemaGeneration()));
}

// ==================== P1a-04：构建期并发 UPDATE 不得留下过期条目 ====================

TEST_F(IndexE2ETest, BuildAllowsConcurrentUpdateWithoutStaleEntry) {
    createLabel(
        env, "Person",
        {{0, "id", PropertyType::INT64, false, std::nullopt}, {1, "email", PropertyType::STRING, false, std::nullopt}});

    IndexRuntimeRegistry registry;
    env.async_data->setIndexRegistry(&registry); // for_session 会继承它
    QueryExecutor writer(*env.async_data, *env.async_meta, *env.session_pool, {});
    writer.setSchemaGenerationTracker(env.schema_generations);
    writer.setIndexRegistry(&registry);
    QueryExecutor ddl(*env.async_data, *env.async_meta, *env.session_pool, {});
    ddl.setSchemaGenerationTracker(env.schema_generations);
    ddl.setIndexRegistry(&registry);

    // 300 行（6 条多行 CREATE）；回填批 64 ⇒ 第一批覆盖 id 0..63。
    for (int base = 0; base < 300; base += 50) {
        std::string query = "CREATE ";
        for (int i = base; i < base + 50; ++i) {
            if (i > base)
                query += ", ";
            query += "(p" + std::to_string(i) + ":Person {id:" + std::to_string(i) + ", email:'e" + std::to_string(i) +
                     "'})";
        }
        ASSERT_TRUE(execSync(writer, query).error.empty());
    }

    ScopedEnvVar batch_env(config::kEnvIndexBackfillBatch, "64");
    std::promise<void> batch_done;
    auto batch_done_future = batch_done.get_future();
    std::promise<void> release_scan;
    auto release_future = release_scan.get_future().share();
    std::atomic<bool> hit_once{false};

    index_hooks::install([&](std::string_view point, uint64_t /*index_id*/) {
        if (point == "scan.batch_committed" && !hit_once.exchange(true)) {
            batch_done.set_value();
            release_future.wait(); // 暂停回填，制造"构建窗口"
        }
    });

    ExecutionResult build_result;
    std::thread build_thread(
        [&] { build_result = execSync(ddl, "CREATE INDEX idx_person_email FOR (n:Person) ON (n.email)"); });
    batch_done_future.wait();

    // 修改一个**已被回填**的行的键：BUILDING ⇒ 写路径必须进变更表。
    const auto update = execSync(writer, "MATCH (n:Person) WHERE n.id = 0 SET n.email = 'changed'");
    ASSERT_TRUE(update.error.empty()) << update.error;

    release_scan.set_value();
    build_thread.join();
    index_hooks::clear();
    ASSERT_TRUE(build_result.error.empty()) << build_result.error;

    auto countWhere = [&](const std::string& email) -> int64_t {
        auto rows = runQuery(writer, "MATCH (n:Person) WHERE n.email = '" + email + "' RETURN count(*)");
        if (rows.empty() || rows[0].empty())
            return -1;
        return std::get<int64_t>(rows[0][0]);
    };

    // 先确认查询确实走索引（否则用例没有覆盖索引路径）。
    const auto explain = explainQuery(writer, "MATCH (n:Person) WHERE n.email = 'e0' RETURN n");
    EXPECT_NE(explain.find("IndexScan"), std::string::npos) << explain;
    EXPECT_EQ(countWhere("e0"), 0) << "构建期 UPDATE 留下了过期条目";
    EXPECT_EQ(countWhere("changed"), 1);

    // 独立 oracle：全表扫（不经过索引）逐行统计，结果必须一致。
    auto all_emails = runQuery(writer, "MATCH (n:Person) RETURN n.email");
    int64_t full_scan_e0 = 0;
    int64_t full_scan_changed = 0;
    for (const auto& row : all_emails) {
        if (row.empty() || !std::holds_alternative<std::string>(row[0]))
            continue;
        const auto& value = std::get<std::string>(row[0]);
        if (value == "e0")
            ++full_scan_e0;
        else if (value == "changed")
            ++full_scan_changed;
    }
    EXPECT_EQ(full_scan_e0, 0);
    EXPECT_EQ(full_scan_changed, 1);
}

// ==================== P1a：构建期并发 INSERT 不丢行 ====================

TEST_F(IndexE2ETest, BuildAllowsConcurrentInsertWithoutLostRows) {
    createLabel(
        env, "Person",
        {{0, "id", PropertyType::INT64, false, std::nullopt}, {1, "email", PropertyType::STRING, false, std::nullopt}});

    IndexRuntimeRegistry registry;
    env.async_data->setIndexRegistry(&registry);
    QueryExecutor writer(*env.async_data, *env.async_meta, *env.session_pool, {});
    writer.setSchemaGenerationTracker(env.schema_generations);
    writer.setIndexRegistry(&registry);
    QueryExecutor ddl(*env.async_data, *env.async_meta, *env.session_pool, {});
    ddl.setSchemaGenerationTracker(env.schema_generations);
    ddl.setIndexRegistry(&registry);

    for (int base = 0; base < 200; base += 50) {
        std::string query = "CREATE ";
        for (int i = base; i < base + 50; ++i) {
            if (i > base)
                query += ", ";
            query += "(p" + std::to_string(i) + ":Person {id:" + std::to_string(i) + ", email:'e" + std::to_string(i) +
                     "'})";
        }
        ASSERT_TRUE(execSync(writer, query).error.empty());
    }

    ScopedEnvVar batch_env(config::kEnvIndexBackfillBatch, "64");
    std::promise<void> batch_done;
    auto batch_done_future = batch_done.get_future();
    std::promise<void> release_scan;
    auto release_future = release_scan.get_future().share();
    std::atomic<bool> hit_once{false};
    index_hooks::install([&](std::string_view point, uint64_t) {
        if (point == "scan.batch_committed" && !hit_once.exchange(true)) {
            batch_done.set_value();
            release_future.wait();
        }
    });

    ExecutionResult build_result;
    std::thread build_thread(
        [&] { build_result = execSync(ddl, "CREATE INDEX idx_person_email FOR (n:Person) ON (n.email)"); });
    batch_done_future.wait();

    ASSERT_TRUE(execSync(writer, "CREATE (a:Person {id:900, email:'new-a'}), (b:Person {id:901, email:'new-b'}), "
                                 "(c:Person {id:902, email:'new-c'})")
                    .error.empty());

    release_scan.set_value();
    build_thread.join();
    index_hooks::clear();
    ASSERT_TRUE(build_result.error.empty()) << build_result.error;

    auto countWhere = [&](const std::string& email) -> int64_t {
        auto rows = runQuery(writer, "MATCH (n:Person) WHERE n.email = '" + email + "' RETURN count(*)");
        if (rows.empty() || rows[0].empty())
            return -1;
        return std::get<int64_t>(rows[0][0]);
    };
    EXPECT_EQ(countWhere("new-a"), 1);
    EXPECT_EQ(countWhere("new-b"), 1);
    EXPECT_EQ(countWhere("new-c"), 1);

    // 独立 oracle：全表扫计数。
    int64_t full_scan_new = 0;
    for (const auto& row : runQuery(writer, "MATCH (n:Person) RETURN n.email")) {
        if (!row.empty() && std::holds_alternative<std::string>(row[0]) &&
            std::get<std::string>(row[0]).rfind("new-", 0) == 0)
            ++full_scan_new;
    }
    EXPECT_EQ(full_scan_new, 3);
}

// ==================== P1a：FINALIZE 关闸后的晚到写入不丢 ====================

TEST_F(IndexE2ETest, BuildFinalizeGateDoesNotLoseLateWrites) {
    createLabel(
        env, "Person",
        {{0, "id", PropertyType::INT64, false, std::nullopt}, {1, "email", PropertyType::STRING, false, std::nullopt}});

    IndexRuntimeRegistry registry;
    env.async_data->setIndexRegistry(&registry);
    QueryExecutor writer(*env.async_data, *env.async_meta, *env.session_pool, {});
    writer.setSchemaGenerationTracker(env.schema_generations);
    writer.setIndexRegistry(&registry);
    QueryExecutor ddl(*env.async_data, *env.async_meta, *env.session_pool, {});
    ddl.setSchemaGenerationTracker(env.schema_generations);
    ddl.setIndexRegistry(&registry);

    for (int i = 0; i < 100; ++i) {
        ASSERT_TRUE(
            execSync(writer, "CREATE (p:Person {id:" + std::to_string(i) + ", email:'e" + std::to_string(i) + "'})")
                .error.empty());
    }

    std::promise<void> gate_hit;
    auto gate_hit_future = gate_hit.get_future();
    std::promise<void> release_gate;
    auto release_future = release_gate.get_future().share();
    std::atomic<bool> hit_once{false};
    index_hooks::install([&](std::string_view point, uint64_t) {
        if (point == "finalize.before_public" && !hit_once.exchange(true)) {
            gate_hit.set_value();
            release_future.wait();
        }
    });

    ExecutionResult build_result;
    std::thread build_thread(
        [&] { build_result = execSync(ddl, "CREATE INDEX idx_person_email FOR (n:Person) ON (n.email)"); });
    gate_hit_future.wait(); // 此时闸门已关、delta 已排空

    ExecutionResult late_result;
    std::thread late_writer([&] { late_result = execSync(writer, "CREATE (late:Person {id:999, email:'late'})"); });
    release_gate.set_value();
    late_writer.join();
    build_thread.join();
    index_hooks::clear();

    ASSERT_TRUE(build_result.error.empty()) << build_result.error;
    ASSERT_TRUE(late_result.error.empty()) << late_result.error;

    auto rows = runQuery(writer, "MATCH (n:Person) WHERE n.email = 'late' RETURN count(*)");
    ASSERT_FALSE(rows.empty());
    EXPECT_EQ(std::get<int64_t>(rows[0][0]), 1) << "关闸后的写入必须等 PUBLIC 后直写索引，不能丢";
}

// ==================== P1a：唯一索引最终校验（构建期并发重复） ====================

TEST_F(IndexE2ETest, UniqueBuildFailsOnConcurrentDuplicate) {
    createLabel(
        env, "Person",
        {{0, "id", PropertyType::INT64, false, std::nullopt}, {1, "email", PropertyType::STRING, false, std::nullopt}});

    IndexRuntimeRegistry registry;
    env.async_data->setIndexRegistry(&registry);
    QueryExecutor writer(*env.async_data, *env.async_meta, *env.session_pool, {});
    writer.setSchemaGenerationTracker(env.schema_generations);
    writer.setIndexRegistry(&registry);
    QueryExecutor ddl(*env.async_data, *env.async_meta, *env.session_pool, {});
    ddl.setSchemaGenerationTracker(env.schema_generations);
    ddl.setIndexRegistry(&registry);

    for (int base = 0; base < 100; base += 50) {
        std::string query = "CREATE ";
        for (int i = base; i < base + 50; ++i) {
            if (i > base)
                query += ", ";
            query += "(p" + std::to_string(i) + ":Person {id:" + std::to_string(i) + ", email:'e" + std::to_string(i) +
                     "'})";
        }
        ASSERT_TRUE(execSync(writer, query).error.empty());
    }

    ScopedEnvVar batch_env(config::kEnvIndexBackfillBatch, "32");
    std::promise<void> batch_done;
    auto batch_done_future = batch_done.get_future();
    std::promise<void> release_scan;
    auto release_future = release_scan.get_future().share();
    std::atomic<bool> hit_once{false};
    index_hooks::install([&](std::string_view point, uint64_t) {
        if (point == "scan.batch_committed" && !hit_once.exchange(true)) {
            batch_done.set_value();
            release_future.wait();
        }
    });

    ExecutionResult build_result;
    std::thread build_thread(
        [&] { build_result = execSync(ddl, "CREATE UNIQUE INDEX idx_person_email FOR (n:Person) ON (n.email)"); });
    batch_done_future.wait();

    // 构建期并发写重复键：两个事务都成功（唯一约束尚未生效），必须由最终校验拦截。
    ASSERT_TRUE(execSync(writer, "CREATE (a:Person {id:900, email:'dup-x'}), (b:Person {id:901, email:'dup-x'})")
                    .error.empty());

    release_scan.set_value();
    build_thread.join();
    index_hooks::clear();

    EXPECT_FALSE(build_result.error.empty()) << "构建期重复必须让构建失败，绝不能带重复上线";
    auto label_def = blockingWait(env.async_meta->getLabelDef("Person"));
    ASSERT_TRUE(label_def.has_value());
    ASSERT_EQ(label_def->indexes.size(), 1u);
    EXPECT_EQ(label_def->indexes[0].state, IndexState::ERROR);

    const auto explain = explainQuery(writer, "MATCH (n:Person) WHERE n.email = 'dup-x' RETURN n");
    EXPECT_EQ(explain.find("IndexScan"), std::string::npos) << "ERROR 索引不得被规划器使用";

    auto rows = runQuery(writer, "MATCH (n:Person) WHERE n.email = 'dup-x' RETURN count(*)");
    ASSERT_FALSE(rows.empty());
    EXPECT_EQ(std::get<int64_t>(rows[0][0]), 2); // 全表扫仍能看到两条（约束未上线）
}

// ==================== P1b：索引可见性冻结在事务开始 ====================

TEST_F(IndexE2ETest, IndexVisibilityFrozenAtTransactionStart) {
    createLabel(
        env, "Person",
        {{0, "id", PropertyType::INT64, false, std::nullopt}, {1, "email", PropertyType::STRING, false, std::nullopt}});

    IndexRuntimeRegistry registry;
    env.async_data->setIndexRegistry(&registry);
    QueryExecutor writer(*env.async_data, *env.async_meta, *env.session_pool, {});
    writer.setSchemaGenerationTracker(env.schema_generations);
    writer.setIndexRegistry(&registry);
    QueryExecutor ddl(*env.async_data, *env.async_meta, *env.session_pool, {});
    ddl.setSchemaGenerationTracker(env.schema_generations);
    ddl.setIndexRegistry(&registry);

    for (int i = 0; i < 100; ++i) {
        ASSERT_TRUE(
            execSync(writer, "CREATE (p:Person {id:" + std::to_string(i) + ", email:'e" + std::to_string(i) + "'})")
                .error.empty());
    }

    std::promise<void> gate_hit;
    auto gate_hit_future = gate_hit.get_future();
    std::promise<void> release_gate;
    auto release_future = release_gate.get_future().share();
    std::atomic<bool> hit_once{false};
    index_hooks::install([&](std::string_view point, uint64_t) {
        if (point == "finalize.before_public" && !hit_once.exchange(true)) {
            gate_hit.set_value();
            release_future.wait();
        }
    });

    ExecutionResult build_result;
    std::thread build_thread(
        [&] { build_result = execSync(ddl, "CREATE INDEX idx_person_email FOR (n:Person) ON (n.email)"); });
    gate_hit_future.wait(); // 索引已建好但还未 PUBLIC

    // 在构建窗口内开启显式事务：它的 pin 早于 PUBLIC。
    auto lease = env.session_pool->acquire();
    ASSERT_TRUE(lease.has_value());
    GraphSession& session = **lease;
    ASSERT_TRUE(session.begin_read());

    auto run_in_session = [&](const std::string& query) -> std::vector<Row> {
        auto ctx = blockingWait(ddl.prepareStream(query, {}, nullptr, std::ref(session)));
        std::vector<Row> rows;
        if (!ctx->error.empty())
            return rows;
        auto gen = std::move(ctx->gen);
        blockingWait(folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
            while (auto chunk = co_await gen.next()) {
                auto chunk_rows = eugraph::test::chunkToRows(*chunk);
                for (auto& row : chunk_rows)
                    rows.push_back(std::move(row));
            }
        }));
        return rows;
    };

    const auto plan_before = run_in_session("EXPLAIN MATCH (n:Person) WHERE n.email = 'e0' RETURN n");
    std::string plan_before_text;
    for (const auto& row : plan_before) {
        if (!row.empty() && std::holds_alternative<std::string>(row[0]))
            plan_before_text += std::get<std::string>(row[0]) + "\n";
    }

    release_gate.set_value();
    build_thread.join();
    index_hooks::clear();
    ASSERT_TRUE(build_result.error.empty()) << build_result.error;

    // 同一事务的后续语句仍然看不到这个索引（pin 早于 public_generation）。
    const auto plan_after = run_in_session("EXPLAIN MATCH (n:Person) WHERE n.email = 'e0' RETURN n");
    std::string plan_after_text;
    for (const auto& row : plan_after) {
        if (!row.empty() && std::holds_alternative<std::string>(row[0]))
            plan_after_text += std::get<std::string>(row[0]) + "\n";
    }
    EXPECT_EQ(plan_before_text.find("IndexScan"), std::string::npos) << plan_before_text;
    EXPECT_EQ(plan_after_text.find("IndexScan"), std::string::npos) << plan_after_text;

    auto in_txn = run_in_session("MATCH (n:Person) WHERE n.email = 'e0' RETURN count(*)");
    ASSERT_FALSE(in_txn.empty());
    EXPECT_EQ(std::get<int64_t>(in_txn[0][0]), 1); // 结果正确（走全表扫）

    session.rollback();
    lease.reset();

    // 新事务：索引可见，EXPLAIN 必须出现 IndexScan。
    const auto plan_new = explainQuery(writer, "MATCH (n:Person) WHERE n.email = 'e0' RETURN n");
    EXPECT_NE(plan_new.find("IndexScan"), std::string::npos) << plan_new;
}

// ==================== P1b：异步 CREATE INDEX（投递即返回） ====================

TEST_F(IndexE2ETest, AsyncCreateIndexReturnsBeforeBuildCompletes) {
    createLabel(
        env, "Person",
        {{0, "id", PropertyType::INT64, false, std::nullopt}, {1, "email", PropertyType::STRING, false, std::nullopt}});

    IndexRuntimeRegistry registry;
    env.async_data->setIndexRegistry(&registry);

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    executor.setIndexRegistry(&registry);
    IndexBuildService service; // 声明在 executor 之后 ⇒ 先析构/shutdown（job 捕获 executor）
    executor.setIndexBuildService(&service);

    for (int i = 0; i < 100; ++i) {
        ASSERT_TRUE(
            execSync(executor, "CREATE (p:Person {id:" + std::to_string(i) + ", email:'e" + std::to_string(i) + "'})")
                .error.empty());
    }

    std::promise<void> batch_done;
    auto batch_done_future = batch_done.get_future();
    std::promise<void> release_scan;
    auto release_future = release_scan.get_future().share();
    std::atomic<bool> hit_once{false};
    index_hooks::install([&](std::string_view point, uint64_t) {
        if (point == "scan.batch_committed" && !hit_once.exchange(true)) {
            batch_done.set_value();
            release_future.wait();
        }
    });

    auto index_state = [&](const std::string& name) -> std::string {
        auto rows = execSync(executor, "SHOW INDEXES");
        for (const auto& row : rows.rows) {
            if (row.size() >= 5 && std::holds_alternative<std::string>(row[0]) && std::get<std::string>(row[0]) == name)
                return std::get<std::string>(row[4]);
        }
        return {};
    };

    const auto created = execSync(executor, "CREATE INDEX idx_person_email FOR (n:Person) ON (n.email)");
    ASSERT_TRUE(created.error.empty()) << created.error;
    ASSERT_FALSE(created.rows.empty());
    EXPECT_NE(std::get<std::string>(created.rows[0][0]).find("building"), std::string::npos);

    batch_done_future.wait(); // 后台构建仍在进行（被钩子暂停）
    EXPECT_EQ(index_state("idx_person_email"), "BUILDING");

    release_scan.set_value();
    ASSERT_TRUE(service.waitUntilIdle(5000));
    EXPECT_EQ(index_state("idx_person_email"), "PUBLIC");

    auto rows = runQuery(executor, "MATCH (n:Person) WHERE n.email = 'e0' RETURN count(*)");
    ASSERT_FALSE(rows.empty());
    EXPECT_EQ(std::get<int64_t>(rows[0][0]), 1);

    index_hooks::clear();
    service.shutdown();
}

// ==================== P2：异步 DROP INDEX（返回即删定义，表后台清理） ====================

TEST_F(IndexE2ETest, AsyncDropIndexReturnsImmediatelyAndCleansUp) {
    createLabel(
        env, "Person",
        {{0, "id", PropertyType::INT64, false, std::nullopt}, {1, "email", PropertyType::STRING, false, std::nullopt}});

    IndexRuntimeRegistry registry;
    env.async_data->setIndexRegistry(&registry);
    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    executor.setIndexRegistry(&registry);
    IndexBuildService service;
    executor.setIndexBuildService(&service);

    ASSERT_TRUE(execSync(executor, "CREATE (p:Person {id:0, email:'e0'})").error.empty());
    auto created = execSync(executor, "CREATE INDEX idx_person_email FOR (n:Person) ON (n.email)");
    ASSERT_TRUE(created.error.empty());
    ASSERT_TRUE(service.waitUntilIdle(5000));

    auto find_index = [&](const std::string& name) -> bool {
        auto rows = execSync(executor, "SHOW INDEXES");
        for (const auto& row : rows.rows)
            if (row.size() >= 1 && std::holds_alternative<std::string>(row[0]) && std::get<std::string>(row[0]) == name)
                return true;
        return false;
    };
    ASSERT_TRUE(find_index("idx_person_email"));

    const auto dropped = execSync(executor, "DROP INDEX idx_person_email");
    ASSERT_TRUE(dropped.error.empty()) << dropped.error;
    ASSERT_FALSE(dropped.rows.empty());
    EXPECT_NE(std::get<std::string>(dropped.rows[0][0]).find("dropping"), std::string::npos);
    EXPECT_FALSE(find_index("idx_person_email")); // 定义立即消失

    ASSERT_TRUE(service.waitUntilIdle(5000));
    // 清理任务跑完：pending_drop 必须销账（删除成功或转 orphan，都不能留在 pending）。
    auto records = blockingWait(env.async_meta->listCleanupRecords());
    for (const auto& record : records)
        EXPECT_NE(record.kind, "pending_drop") << "index " << record.index_id;

    // 同名索引可以立即重建（证明定义/属性集没有被残留阻塞）。
    auto recreated = execSync(executor, "CREATE INDEX idx_person_email FOR (n:Person) ON (n.email)");
    ASSERT_TRUE(recreated.error.empty()) << "recreate error: " << recreated.error;
    ASSERT_TRUE(service.waitUntilIdle(5000));

    service.shutdown();
}

// ==================== P2：DROP 等在飞读者归零 ====================

TEST_F(IndexE2ETest, AsyncDropWaitsForActiveReaders) {
    createLabel(
        env, "Person",
        {{0, "id", PropertyType::INT64, false, std::nullopt}, {1, "email", PropertyType::STRING, false, std::nullopt}});

    IndexRuntimeRegistry registry;
    env.async_data->setIndexRegistry(&registry);
    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    executor.setIndexRegistry(&registry);
    IndexBuildService service;
    executor.setIndexBuildService(&service);

    ASSERT_TRUE(execSync(executor, "CREATE (p:Person {id:0, email:'e0'})").error.empty());
    ASSERT_TRUE(execSync(executor, "CREATE INDEX idx_person_email FOR (n:Person) ON (n.email)").error.empty());
    ASSERT_TRUE(service.waitUntilIdle(5000));

    auto label_def = blockingWait(env.async_meta->getLabelDef("Person"));
    ASSERT_TRUE(label_def.has_value());
    ASSERT_EQ(label_def->indexes.size(), 1u);
    const uint32_t index_id = label_def->indexes[0].index_id;

    // 模拟"在飞查询"：直接持一个读句柄。
    auto reader = registry.acquireRead(index_id);
    ASSERT_TRUE(reader.has_value());

    ASSERT_TRUE(execSync(executor, "DROP INDEX idx_person_email").error.empty());
    // 清理任务应阻塞在 readers>0（不销账）。
    EXPECT_FALSE(service.waitUntilIdle(300));

    reader->reset();
    ASSERT_TRUE(service.waitUntilIdle(5000));

    auto records = blockingWait(env.async_meta->listCleanupRecords());
    for (const auto& record : records)
        EXPECT_NE(record.kind, "pending_drop") << "index " << record.index_id;

    service.shutdown();
}

// ==================== P2：DROP 等"旧 pin 事务"结束 ====================

TEST_F(IndexE2ETest, AsyncDropWaitsForOldSchemaPins) {
    createLabel(
        env, "Person",
        {{0, "id", PropertyType::INT64, false, std::nullopt}, {1, "email", PropertyType::STRING, false, std::nullopt}});

    IndexRuntimeRegistry registry;
    env.async_data->setIndexRegistry(&registry);
    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    executor.setIndexRegistry(&registry);
    IndexBuildService service;
    executor.setIndexBuildService(&service);
    auto tracker = std::make_shared<SchemaGenerationTracker>();
    executor.setSchemaGenerationTracker(tracker);

    ASSERT_TRUE(execSync(executor, "CREATE (p:Person {id:0, email:'e0'})").error.empty());
    ASSERT_TRUE(execSync(executor, "CREATE INDEX idx_person_email FOR (n:Person) ON (n.email)").error.empty());
    ASSERT_TRUE(service.waitUntilIdle(5000));

    // 显式事务在 DROP 之前 pin 住旧代数（计划可能稍后才扫该索引）。
    auto lease = env.session_pool->acquire();
    ASSERT_TRUE(lease.has_value());
    GraphSession& session = **lease;
    ASSERT_TRUE(session.begin_read());
    auto ctx = blockingWait(executor.prepareStream("MATCH (n:Person) RETURN count(*)", {}, nullptr, std::ref(session)));
    ASSERT_TRUE(ctx->error.empty()) << ctx->error;
    auto gen = std::move(ctx->gen);
    blockingWait(folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
        while (auto chunk = co_await gen.next()) {}
    }));
    EXPECT_EQ(tracker->pinCount(), 1u);

    ASSERT_TRUE(execSync(executor, "DROP INDEX idx_person_email").error.empty());
    EXPECT_FALSE(service.waitUntilIdle(300)); // 清理阻塞在旧 pin 上

    session.rollback(); // 事务结束 ⇒ unpin
    lease.reset();
    ASSERT_TRUE(service.waitUntilIdle(5000));
    EXPECT_EQ(tracker->pinCount(), 0u);

    service.shutdown();
}

// ==================== P2：PENDING_PURGE 进程内重试 ====================

TEST_F(IndexE2ETest, PendingPurgeIsRetriedInProcess) {
    createLabel(
        env, "Person",
        {{0, "id", PropertyType::INT64, false, std::nullopt}, {1, "email", PropertyType::STRING, false, std::nullopt}});

    IndexRuntimeRegistry registry;
    env.async_data->setIndexRegistry(&registry);
    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    executor.setIndexRegistry(&registry);
    IndexBuildService service;
    executor.setIndexBuildService(&service);

    ASSERT_TRUE(execSync(executor, "CREATE (p:Person {id:0, email:'e0'})").error.empty());
    ASSERT_TRUE(execSync(executor, "CREATE INDEX idx_person_email FOR (n:Person) ON (n.email)").error.empty());
    ASSERT_TRUE(service.waitUntilIdle(5000));

    auto label_def = blockingWait(env.async_meta->getLabelDef("Person"));
    ASSERT_TRUE(label_def.has_value());
    const uint32_t index_id = label_def->indexes[0].index_id;

    executor.setIndexPurgeWaitMs(200); // 测试用短等待，制造"读者未归零"
    auto reader = registry.acquireRead(index_id);
    ASSERT_TRUE(reader.has_value());
    ASSERT_TRUE(execSync(executor, "DROP INDEX idx_person_email").error.empty());
    ASSERT_TRUE(service.waitUntilIdle(5000)); // 清理等 200ms 后放弃

    auto runtime = registry.find(index_id);
    ASSERT_TRUE(runtime != nullptr);
    EXPECT_EQ(runtime->lifecycle(), IndexLifecycle::kPendingPurge);

    auto pending_count = [&]() {
        size_t count = 0;
        for (const auto& record : blockingWait(env.async_meta->listCleanupRecords()))
            if (record.kind == "pending_drop" && record.index_id == index_id)
                ++count;
        return count;
    };
    EXPECT_EQ(pending_count(), 1u);

    reader->reset();
    blockingWait(executor.retryPendingIndexCleanups());
    EXPECT_EQ(pending_count(), 0u); // 成功销账，或转 orphan（都不能留 pending）

    service.shutdown();
}

// ==================== P3：SHOW INDEXES 的 phase / progress ====================

TEST_F(IndexE2ETest, AsyncBuildExposesPhaseAndProgress) {
    createLabel(
        env, "Person",
        {{0, "id", PropertyType::INT64, false, std::nullopt}, {1, "email", PropertyType::STRING, false, std::nullopt}});

    IndexRuntimeRegistry registry;
    env.async_data->setIndexRegistry(&registry);
    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    executor.setIndexRegistry(&registry);
    IndexBuildService service;
    executor.setIndexBuildService(&service);

    for (int i = 0; i < 100; ++i) {
        ASSERT_TRUE(
            execSync(executor, "CREATE (p:Person {id:" + std::to_string(i) + ", email:'e" + std::to_string(i) + "'})")
                .error.empty());
    }

    // 进度百分比依赖"已知总数"（图打开时由 countVerticesByLabel 填充；本用例在线建标签，
    // 手动补一次，模拟正常图打开后的计数）。
    auto person_def = blockingWait(env.async_meta->getLabelDef("Person"));
    ASSERT_TRUE(person_def.has_value());
    env.async_meta->setLabelVertexCount(person_def->id, 100);

    ScopedEnvVar batch_env(config::kEnvIndexBackfillBatch, "16");
    std::promise<void> first_batch;
    auto first_batch_future = first_batch.get_future();
    std::promise<void> release_scan;
    auto release_future = release_scan.get_future().share();
    std::atomic<bool> hit_once{false};
    index_hooks::install([&](std::string_view point, uint64_t) {
        if (point == "scan.batch_committed" && !hit_once.exchange(true)) {
            first_batch.set_value();
            release_future.wait();
        }
    });

    ASSERT_TRUE(execSync(executor, "CREATE INDEX idx_person_email FOR (n:Person) ON (n.email)").error.empty());
    first_batch_future.wait();

    auto row_for = [&](const std::string& name) -> std::vector<Row> {
        auto rows = execSync(executor, "SHOW INDEXES");
        std::vector<Row> matched;
        for (const auto& row : rows.rows)
            if (!row.empty() && std::holds_alternative<std::string>(row[0]) && std::get<std::string>(row[0]) == name)
                matched.push_back(row);
        return matched;
    };

    auto building = row_for("idx_person_email");
    ASSERT_EQ(building.size(), 1u);
    ASSERT_GE(building[0].size(), 10u);
    EXPECT_EQ(std::get<std::string>(building[0][4]), "BUILDING");
    EXPECT_EQ(std::get<std::string>(building[0][5]), "SCAN");
    EXPECT_GT(std::stoi(std::get<std::string>(building[0][6])), 0);
    EXPECT_LT(std::stoi(std::get<std::string>(building[0][6])), 100);
    EXPECT_GT(std::stoll(std::get<std::string>(building[0][8])), 0); // started_at

    release_scan.set_value();
    ASSERT_TRUE(service.waitUntilIdle(5000));
    auto published = row_for("idx_person_email");
    ASSERT_EQ(published.size(), 1u);
    EXPECT_EQ(std::get<std::string>(published[0][4]), "PUBLIC");
    EXPECT_EQ(std::get<std::string>(published[0][5]), "PUBLIC");
    EXPECT_EQ(std::get<std::string>(published[0][6]), "100");

    index_hooks::clear();
    service.shutdown();
}

// ==================== P3：构建期 delta_backlog 可观测 ====================

TEST_F(IndexE2ETest, BuildDeltaBacklogVisibleDuringBuild) {
    createLabel(
        env, "Person",
        {{0, "id", PropertyType::INT64, false, std::nullopt}, {1, "email", PropertyType::STRING, false, std::nullopt}});

    IndexRuntimeRegistry registry;
    env.async_data->setIndexRegistry(&registry);
    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    executor.setIndexRegistry(&registry);
    IndexBuildService service;
    executor.setIndexBuildService(&service);

    for (int i = 0; i < 100; ++i) {
        ASSERT_TRUE(
            execSync(executor, "CREATE (p:Person {id:" + std::to_string(i) + ", email:'e" + std::to_string(i) + "'})")
                .error.empty());
    }

    ScopedEnvVar batch_env(config::kEnvIndexBackfillBatch, "16");
    std::promise<void> first_batch;
    auto first_batch_future = first_batch.get_future();
    std::promise<void> release_scan;
    auto release_future = release_scan.get_future().share();
    std::atomic<bool> hit_once{false};
    index_hooks::install([&](std::string_view point, uint64_t) {
        if (point == "scan.batch_committed" && !hit_once.exchange(true)) {
            first_batch.set_value();
            release_future.wait();
        }
    });

    ASSERT_TRUE(execSync(executor, "CREATE INDEX idx_person_email FOR (n:Person) ON (n.email)").error.empty());
    first_batch_future.wait();

    auto backlog = [&]() -> int64_t {
        auto rows = execSync(executor, "SHOW INDEXES");
        for (const auto& row : rows.rows) {
            if (!row.empty() && std::holds_alternative<std::string>(row[0]) &&
                std::get<std::string>(row[0]) == "idx_person_email")
                return std::stoll(std::get<std::string>(row[7]));
        }
        return -1;
    };
    EXPECT_EQ(backlog(), 0);

    ASSERT_TRUE(execSync(executor, "MATCH (n:Person) WHERE n.id = 0 SET n.email = 'changed'").error.empty());
    EXPECT_EQ(backlog(), 2); // 改索引属性 = DEL 旧键 + PUT 新键 ⇒ 2 行 delta

    release_scan.set_value();
    ASSERT_TRUE(service.waitUntilIdle(5000));
    EXPECT_EQ(backlog(), 0); // 追平后归零

    index_hooks::clear();
    service.shutdown();
}

// ==================== P4：并发构建压力（需显式开启） ====================
// 运行：EUGRAPH_INDEX_STRESS=1 ./build/release/index_e2e_tests --gtest_filter='*StressConcurrentBuild*'

TEST_F(IndexE2ETest, StressConcurrentBuildAndWrites) {
    if (std::getenv("EUGRAPH_INDEX_STRESS") == nullptr)
        GTEST_SKIP() << "set EUGRAPH_INDEX_STRESS=1 to run the stress case";

    int base_rows = 4000;
    if (const char* rows_env = std::getenv("EUGRAPH_INDEX_STRESS_ROWS")) {
        const int parsed = std::atoi(rows_env);
        if (parsed >= 100)
            base_rows = parsed;
    }
    constexpr int kThreads = 4;
    constexpr int kUpdatesPerThread = 50;
    constexpr int kInsertsPerThread = 10;

    createLabel(
        env, "Person",
        {{0, "id", PropertyType::INT64, false, std::nullopt}, {1, "email", PropertyType::STRING, false, std::nullopt}});

    IndexRuntimeRegistry registry;
    env.async_data->setIndexRegistry(&registry);
    QueryExecutor ddl(*env.async_data, *env.async_meta, *env.session_pool, {});
    ddl.setSchemaGenerationTracker(env.schema_generations);
    ddl.setIndexRegistry(&registry);
    IndexBuildService service;
    ddl.setIndexBuildService(&service);

    QueryExecutor bulk(*env.async_data, *env.async_meta, *env.session_pool, {});
    bulk.setSchemaGenerationTracker(env.schema_generations);
    bulk.setIndexRegistry(&registry);
    for (int i = 0; i < base_rows; ++i) {
        ASSERT_TRUE(
            execSync(bulk, "CREATE (p:Person {id:" + std::to_string(i) + ", email:'e" + std::to_string(i) + "'})")
                .error.empty());
    }

    std::promise<void> first_batch;
    auto first_batch_future = first_batch.get_future();
    std::promise<void> release_scan;
    auto release_future = release_scan.get_future().share();
    std::atomic<bool> hit_once{false};
    index_hooks::install([&](std::string_view point, uint64_t) {
        if (point == "scan.batch_committed" && !hit_once.exchange(true)) {
            first_batch.set_value();
            release_future.wait();
        }
    });

    ASSERT_TRUE(execSync(ddl, "CREATE INDEX idx_person_email FOR (n:Person) ON (n.email)").error.empty());
    first_batch_future.wait();

    std::atomic<int> writer_errors{0};
    std::vector<std::thread> writers;
    writers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        writers.emplace_back([&, t] {
            QueryExecutor writer(*env.async_data, *env.async_meta, *env.session_pool, {});
            writer.setSchemaGenerationTracker(env.schema_generations);
            writer.setIndexRegistry(&registry);
            for (int i = 0; i < kUpdatesPerThread; ++i) {
                const int id = t * kUpdatesPerThread + i;
                const auto result = execSync(writer, "MATCH (n:Person) WHERE n.id = " + std::to_string(id) +
                                                         " SET n.email = 'u-" + std::to_string(id) + "'");
                if (!result.error.empty())
                    ++writer_errors;
            }
            for (int i = 0; i < kInsertsPerThread; ++i) {
                const int id = base_rows + t * kInsertsPerThread + i;
                const auto result = execSync(writer, "CREATE (:Person {id:" + std::to_string(id) + ", email:'n-" +
                                                         std::to_string(id) + "'})");
                if (!result.error.empty())
                    ++writer_errors;
            }
        });
    }
    for (auto& thread : writers)
        thread.join();
    release_scan.set_value();

    ASSERT_TRUE(service.waitUntilIdle(60000));
    index_hooks::clear();
    EXPECT_EQ(writer_errors.load(), 0);

    for (int t = 0; t < kThreads; ++t) {
        for (int i = 0; i < kUpdatesPerThread; ++i) {
            const int id = t * kUpdatesPerThread + i;
            auto old_rows =
                runQuery(ddl, "MATCH (n:Person) WHERE n.email = 'e" + std::to_string(id) + "' RETURN count(*)");
            auto new_rows =
                runQuery(ddl, "MATCH (n:Person) WHERE n.email = 'u-" + std::to_string(id) + "' RETURN count(*)");
            ASSERT_FALSE(old_rows.empty());
            ASSERT_FALSE(new_rows.empty());
            EXPECT_EQ(std::get<int64_t>(old_rows[0][0]), 0) << "id=" << id;
            EXPECT_EQ(std::get<int64_t>(new_rows[0][0]), 1) << "id=" << id;
        }
    }
    for (int t = 0; t < kThreads; ++t) {
        for (int i = 0; i < kInsertsPerThread; ++i) {
            const int id = base_rows + t * kInsertsPerThread + i;
            auto rows =
                runQuery(ddl, "MATCH (n:Person) WHERE n.email = 'n-" + std::to_string(id) + "' RETURN count(*)");
            ASSERT_FALSE(rows.empty());
            EXPECT_EQ(std::get<int64_t>(rows[0][0]), 1) << "id=" << id;
        }
    }

    auto total = runQuery(ddl, "MATCH (n:Person) RETURN count(*)");
    ASSERT_FALSE(total.empty());
    EXPECT_EQ(std::get<int64_t>(total[0][0]), base_rows + kThreads * kInsertsPerThread);

    service.shutdown();
}

// ==================== 可测试性：builder 脱离 QueryExecutor 直接运行 ====================

TEST_F(IndexE2ETest, BuilderRunsWithoutQueryExecutor) {
    createLabel(
        env, "Person",
        {{0, "id", PropertyType::INT64, false, std::nullopt}, {1, "email", PropertyType::STRING, false, std::nullopt}});
    QueryExecutor writer(*env.async_data, *env.async_meta, *env.session_pool, {});
    writer.setSchemaGenerationTracker(env.schema_generations);
    for (int i = 0; i < 50; ++i) {
        ASSERT_TRUE(
            execSync(writer, "CREATE (p:Person {id:" + std::to_string(i) + ", email:'e" + std::to_string(i) + "'})")
                .error.empty());
    }

    // 手工建定义/表/变更表，不经过 DDL/Executor。
    ASSERT_TRUE(blockingWait(env.async_meta->createVertexIndex("idx_builder", "Person", {"email"}, false)));
    const GraphSchema schema = env.async_meta->schemaSnapshot();
    auto idx = schema.findIndexByName("idx_builder");
    ASSERT_TRUE(idx.has_value());
    ASSERT_NE(idx->index_id, 0u);
    ASSERT_TRUE(env.data_store->createIndex(vidxTableById(idx->index_id)));
    ASSERT_TRUE(env.data_store->createIndex(indexDeltaTable(idx->index_id)));

    IndexBuildSpec spec;
    spec.index_id = idx->index_id;
    spec.index_name = "idx_builder";
    spec.label_id = 1;
    spec.index_table = vidxTableById(idx->index_id);
    spec.delta_table = indexDeltaTable(idx->index_id);
    spec.labels = schema.labels;
    for (const auto& [lid, ldef] : schema.labels) {
        if (ldef.name == kAnonLabelName) {
            spec.anon_label_id = lid;
            break;
        }
    }
    for (const auto& acc : idx->accessors) {
        IndexBuildAccessor accessor;
        accessor.is_strong = acc.is_strong;
        accessor.source_label_id = acc.source_label_id;
        accessor.property_name = acc.property_name;
        spec.accessors.push_back(std::move(accessor));
    }

    IndexBuildContext context{*env.async_data, *env.session_pool, /*registry=*/nullptr, {}};
    const auto outcome = blockingWait(runIndexBuild(context, spec));
    ASSERT_TRUE(outcome.ok) << outcome.error;

    ASSERT_TRUE(blockingWait(env.async_meta->updateIndexState("idx_builder", IndexState::PUBLIC)));
    auto rows = runQuery(writer, "MATCH (n:Person) WHERE n.email = 'e1' RETURN count(*)");
    ASSERT_FALSE(rows.empty());
    EXPECT_EQ(std::get<int64_t>(rows[0][0]), 1);
    EXPECT_NE(explainQuery(writer, "MATCH (n:Person) WHERE n.email = 'e1' RETURN n").find("IndexScan"),
              std::string::npos);
}

// ==================== schema 快照：独立拷贝 + 并发读不崩 ====================

TEST_F(IndexE2ETest, SchemaSnapshotIsIndependentCopy) {
    createLabel(env, "Person", {{0, "id", PropertyType::INT64, false, std::nullopt}});
    const GraphSchema before = env.async_meta->schemaSnapshot();
    ASSERT_EQ(before.labels.at(1).properties.size(), 1u);

    ASSERT_TRUE(blockingWait(env.async_meta->addVertexLabelProperties("Person", {{"email", PropertyType::STRING}})));
    EXPECT_EQ(before.labels.at(1).properties.size(), 1u); // 旧快照不受影响
    EXPECT_EQ(env.async_meta->schemaSnapshot().labels.at(1).properties.size(), 2u);
}

TEST_F(IndexE2ETest, SchemaSnapshotsSurviveConcurrentPropertyAdds) {
    createLabel(env, "Person", {{0, "id", PropertyType::INT64, false, std::nullopt}});
    std::atomic<bool> stop{false};
    std::atomic<int> snapshots{0};
    std::thread reader([&] {
        while (!stop.load(std::memory_order_acquire)) {
            const GraphSchema snapshot = env.async_meta->schemaSnapshot();
            auto it = snapshot.labels.find(1);
            if (it != snapshot.labels.end())
                EXPECT_GE(it->second.properties.size(), 1u);
            snapshots.fetch_add(1, std::memory_order_relaxed);
        }
    });
    for (int i = 0; i < 50; ++i)
        ASSERT_TRUE(blockingWait(
            env.async_meta->addVertexLabelProperties("Person", {{"p" + std::to_string(i), PropertyType::INT64}})));
    stop.store(true, std::memory_order_release);
    reader.join();
    EXPECT_GT(snapshots.load(), 0);
}

// ==================== 事务级 schema 版本（shared_ptr pin） ====================

TEST_F(IndexE2ETest, TransactionSchemaPinKeepsOldVersionAlive) {
    createLabel(env, "Person", {{0, "id", PropertyType::INT64, false, std::nullopt}});
    auto lease = env.session_pool->acquire();
    ASSERT_TRUE(lease.has_value());
    GraphSession& session = **lease;
    ASSERT_TRUE(session.begin_read());

    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    auto ctx = blockingWait(executor.prepareStream("RETURN 1", {}, nullptr, std::ref(session)));
    ASSERT_TRUE(ctx->error.empty()) << ctx->error;
    auto gen = std::move(ctx->gen);
    blockingWait(folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
        while (auto chunk = co_await gen.next()) {}
    }));

    const auto pinned = session.schemaPin().schema;
    ASSERT_TRUE(pinned != nullptr);
    ASSERT_EQ(pinned->labels.at(1).properties.size(), 1u);

    // 另一个"事务"发布新版本；本事务的 pin 必须继续看到旧版本。
    ASSERT_TRUE(blockingWait(env.async_meta->addVertexLabelProperties("Person", {{"email", PropertyType::STRING}})));
    EXPECT_EQ(pinned->labels.at(1).properties.size(), 1u);
    EXPECT_EQ(env.async_meta->schemaPtr()->labels.at(1).properties.size(), 2u);
    EXPECT_EQ(session.schemaPin().schema->labels.at(1).properties.size(), 1u);

    session.rollback();
    lease.reset();
}

// ==================== 创建索引之前开始的事务：构建必须等它结束 ====================

TEST_F(IndexE2ETest, BuildWaitsForTransactionsStartedBeforeCreate) {
    createLabel(
        env, "Person",
        {{0, "id", PropertyType::INT64, false, std::nullopt}, {1, "email", PropertyType::STRING, false, std::nullopt}});
    IndexRuntimeRegistry registry;
    env.async_data->setIndexRegistry(&registry);
    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    executor.setIndexRegistry(&registry);
    IndexBuildService service;
    executor.setIndexBuildService(&service);
    auto tracker = std::make_shared<SchemaGenerationTracker>();
    executor.setSchemaGenerationTracker(tracker);

    for (int i = 0; i < 100; ++i) {
        ASSERT_TRUE(
            execSync(executor, "CREATE (p:Person {id:" + std::to_string(i) + ", email:'e" + std::to_string(i) + "'})")
                .error.empty());
    }

    // 旧事务：CREATE INDEX 之前 BEGIN，并固定旧 schema 版本。
    auto lease = env.session_pool->acquire();
    ASSERT_TRUE(lease.has_value());
    GraphSession& session = **lease;
    ASSERT_TRUE(session.begin_write());
    auto pin_ctx = blockingWait(executor.prepareStream("RETURN 1", {}, nullptr, std::ref(session)));
    ASSERT_TRUE(pin_ctx->error.empty()) << pin_ctx->error;
    auto pin_gen = std::move(pin_ctx->gen);
    blockingWait(folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
        while (auto chunk = co_await pin_gen.next()) {}
    }));
    EXPECT_GE(tracker->pinCount(), 1u);

    auto created = execSync(executor, "CREATE INDEX idx_person_email FOR (n:Person) ON (n.email)");
    ASSERT_TRUE(created.error.empty()) << created.error;
    ASSERT_FALSE(created.rows.empty());

    // 旧事务在索引创建后、PUBLIC 前写入；构建必须等它提交后再 SCAN。
    auto write_ctx = blockingWait(executor.prepareStream("MATCH (n:Person) WHERE n.id = 0 SET n.email = 'from-old-txn'",
                                                         {}, nullptr, std::ref(session)));
    ASSERT_TRUE(write_ctx->error.empty()) << write_ctx->error;
    auto write_gen = std::move(write_ctx->gen);
    blockingWait(folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
        while (auto chunk = co_await write_gen.next()) {}
    }));
    ASSERT_TRUE(session.commit().committed());
    lease.reset();

    ASSERT_TRUE(service.waitUntilIdle(5000));
    auto old_rows = runQuery(executor, "MATCH (n:Person) WHERE n.email = 'e0' RETURN count(*)");
    auto new_rows = runQuery(executor, "MATCH (n:Person) WHERE n.email = 'from-old-txn' RETURN count(*)");
    ASSERT_FALSE(old_rows.empty());
    ASSERT_FALSE(new_rows.empty());
    EXPECT_EQ(std::get<int64_t>(old_rows[0][0]), 0);
    EXPECT_EQ(std::get<int64_t>(new_rows[0][0]), 1);

    service.shutdown();
}

TEST_F(IndexE2ETest, BuildWaitsForBatchTransactionStartedBeforeCreate) {
    createLabel(
        env, "Person",
        {{0, "id", PropertyType::INT64, false, std::nullopt}, {1, "email", PropertyType::STRING, false, std::nullopt}});
    IndexRuntimeRegistry registry;
    env.async_data->setIndexRegistry(&registry);
    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    executor.setIndexRegistry(&registry);
    IndexBuildService service;
    executor.setIndexBuildService(&service);

    // 旧批量事务：先 pin 旧 schema，再提交 DDL；构建必须等它结束。
    auto lease = env.session_pool->acquire();
    ASSERT_TRUE(lease.has_value());
    GraphSession& session = **lease;
    ASSERT_TRUE(session.begin_write());
    const auto old_version = env.async_meta->schemaVersion();
    ensureSchemaPin(session, old_version, env.schema_generations.get());

    auto ddl = execSync(executor, "CREATE INDEX idx_person_email FOR (n:Person) ON (n.email)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;

    constexpr int kRows = 100;
    VertexId start = blockingWait(env.async_meta->nextVertexIdRange(kRows));
    std::vector<IAsyncGraphDataStore::BatchVertexEntry> entries;
    entries.reserve(kRows);
    for (int i = 0; i < kRows; ++i) {
        IAsyncGraphDataStore::BatchVertexEntry entry;
        entry.vid = start + static_cast<VertexId>(i);
        Properties props;
        props.push_back(PropertyValue{int64_t(i)});
        props.push_back(PropertyValue{std::string("batch-") + std::to_string(i)});
        entry.label_props.emplace_back(/*Person*/ 1, std::move(props));
        entries.push_back(std::move(entry));
    }
    auto batch_store = env.async_data->for_session(session);
    blockingWait(batch_store.batchInsertVertices(std::move(entries), old_version.schema->labels));
    ASSERT_TRUE(session.commit().committed());
    lease.reset();

    ASSERT_TRUE(service.waitUntilIdle(5000));
    auto rows = runQuery(executor, "MATCH (n:Person) WHERE n.email = 'batch-0' RETURN count(*)");
    ASSERT_FALSE(rows.empty());
    EXPECT_EQ(std::get<int64_t>(rows[0][0]), 1);
    service.shutdown();
}

TEST_F(IndexE2ETest, BatchTransactionDuringScanWritesDelta) {
    createLabel(
        env, "Person",
        {{0, "id", PropertyType::INT64, false, std::nullopt}, {1, "email", PropertyType::STRING, false, std::nullopt}});
    IndexRuntimeRegistry registry;
    env.async_data->setIndexRegistry(&registry);
    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);
    executor.setIndexRegistry(&registry);
    IndexBuildService service;
    executor.setIndexBuildService(&service);

    for (int i = 0; i < 100; ++i) {
        ASSERT_TRUE(
            execSync(executor, "CREATE (p:Person {id:" + std::to_string(i) + ", email:'e" + std::to_string(i) + "'})")
                .error.empty());
    }

    std::promise<void> scan_paused;
    auto scan_paused_future = scan_paused.get_future();
    std::promise<void> release_scan;
    auto release_future = release_scan.get_future().share();
    std::atomic<bool> hit_once{false};
    index_hooks::install([&](std::string_view point, uint64_t) {
        if (point == "scan.batch_committed" && !hit_once.exchange(true)) {
            scan_paused.set_value();
            release_future.wait();
        }
    });

    auto ddl = execSync(executor, "CREATE INDEX idx_person_email FOR (n:Person) ON (n.email)");
    ASSERT_TRUE(ddl.error.empty()) << ddl.error;
    scan_paused_future.wait();

    // 构建中开始一个批量事务：pin 当前 schema（含新索引），预取准入，写 delta。
    auto lease = env.session_pool->acquire();
    ASSERT_TRUE(lease.has_value());
    GraphSession& session = **lease;
    ASSERT_TRUE(session.begin_write());
    const auto version = env.async_meta->schemaVersion();
    ensureSchemaPin(session, version, env.schema_generations.get());
    auto batch_store = env.async_data->for_session(session);
    blockingWait(acquireLabelIndexAdmissions(session, batch_store.indexRegistry(), {1}, /*is_edge=*/false));

    IAsyncGraphDataStore::BatchVertexEntry entry;
    entry.vid = blockingWait(env.async_meta->nextVertexIdRange(1));
    Properties props;
    props.push_back(PropertyValue{int64_t(999)});
    props.push_back(PropertyValue{std::string("during-scan")});
    entry.label_props.emplace_back(/*Person*/ 1, std::move(props));
    std::vector<IAsyncGraphDataStore::BatchVertexEntry> entries;
    entries.push_back(std::move(entry));
    blockingWait(batch_store.batchInsertVertices(std::move(entries), version.schema->labels));
    ASSERT_TRUE(session.commit().committed());
    lease.reset();

    release_scan.set_value();
    ASSERT_TRUE(service.waitUntilIdle(5000));
    index_hooks::clear();

    auto rows = runQuery(executor, "MATCH (n:Person) WHERE n.email = 'during-scan' RETURN count(*)");
    ASSERT_FALSE(rows.empty());
    EXPECT_EQ(std::get<int64_t>(rows[0][0]), 1);
    service.shutdown();
}

TEST_F(IndexE2ETest, IndexDdlWithoutTrackerFailsBeforeBuilding) {
    createLabel(env, "Person", {{0, "id", PropertyType::INT64, false, std::nullopt}});
    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    // 故意不注入 tracker：DDL 必须 fail fast，而不是静默构建出一个无法 drain 旧事务的索引。
    auto result = execSync(executor, "CREATE INDEX idx_person_id FOR (n:Person) ON (n.id)");
    EXPECT_FALSE(result.error.empty());
    EXPECT_NE(result.error.find("SchemaGenerationTracker"), std::string::npos) << result.error;
}

TEST_F(IndexE2ETest, StatsJournalAppliesAndRollbackDiscards) {
    createLabel(env, "Person", {{0, "id", PropertyType::INT64, false, std::nullopt}});
    QueryExecutor executor(*env.async_data, *env.async_meta, *env.session_pool, {});
    executor.setSchemaGenerationTracker(env.schema_generations);

    ASSERT_TRUE(execSync(executor, "CREATE (p:Person {id:1})").error.empty());
    auto snap = env.stats_store->snapshot();
    ASSERT_EQ(snap->labels.count(1), 1u);
    EXPECT_EQ(snap->labels.at(1).vertices, 1);
    EXPECT_GE(snap->applied_journal_seq, 1u);

    // snapshot 可持久化/恢复（journal offset 一并保留）。
    ASSERT_TRUE(env.data_store->saveStatsSnapshot(*snap));
    GraphStatsSnapshot loaded;
    ASSERT_TRUE(env.data_store->loadStatsSnapshot(loaded));
    EXPECT_EQ(loaded.labels.at(1).vertices, 1);
    EXPECT_EQ(loaded.applied_journal_seq, snap->applied_journal_seq);

    // journal 已与数据同事务写入；至少有一条可解码记录。
    size_t journal_rows = 0;
    auto session = env.data_store->conn().make_session();
    wt::table_scan(session.get(), TABLE_STATS_JOURNAL, {}, [&](std::string_view, std::string_view value) {
        StatsDelta delta;
        EXPECT_TRUE(decodeStatsDelta(value, delta));
        ++journal_rows;
        return true;
    });
    EXPECT_GE(journal_rows, 1u);

    // rollback 不应改变统计。
    auto lease = env.session_pool->acquire();
    ASSERT_TRUE(lease.has_value());
    ASSERT_TRUE((**lease).begin_write());
    auto store = env.async_data->for_session(**lease);
    std::vector<std::pair<LabelId, Properties>> label_props;
    label_props.emplace_back(1, Properties{PropertyValue{int64_t(2)}});
    ASSERT_TRUE(blockingWait(store.insertVertex(100, std::span<const std::pair<LabelId, Properties>>{label_props})));
    (*lease)->rollback();
    lease.reset();

    snap = env.stats_store->snapshot();
    ASSERT_EQ(snap->labels.count(1), 1u);
    EXPECT_EQ(snap->labels.at(1).vertices, 1);
}
