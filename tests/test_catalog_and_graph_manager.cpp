#include <gtest/gtest.h>

#include "common/config/env_vars.hpp"
#include "common/types/constants.hpp"
#include "graph/graph_manager.hpp"
#include "query/executor/query_executor.hpp"
#include "storage/catalog/catalog_store.hpp"
#include "storage/index/index_test_hooks.hpp"
#include "test_chunk_helpers.hpp"

#include <folly/coro/BlockingWait.h>

#include <atomic>
#include <filesystem>
#include <future>
#include <thread>

using namespace eugraph;
using folly::coro::blockingWait;

namespace {

std::string getTestDbPath() {
    return "/tmp/eugraph_catalog_test_" + std::to_string(getpid());
}

class CatalogStoreTest : public ::testing::Test {
protected:
    std::string db_path_;
    CatalogStore store_;

    void SetUp() override {
        db_path_ = getTestDbPath();
        std::filesystem::remove_all(db_path_);
    }

    void TearDown() override {
        store_.close();
        std::filesystem::remove_all(db_path_);
    }
};

TEST_F(CatalogStoreTest, OpenAndClose) {
    EXPECT_TRUE(store_.open(db_path_ + "/catalog"));
    store_.close();
}

TEST_F(CatalogStoreTest, CreateGraph) {
    ASSERT_TRUE(store_.open(db_path_ + "/catalog"));

    auto entry = store_.createGraph("test_graph");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->graph_id, 0u);
    EXPECT_GT(entry->created_at, 0u);
}

TEST_F(CatalogStoreTest, CreateDuplicateGraph) {
    ASSERT_TRUE(store_.open(db_path_ + "/catalog"));

    auto first = store_.createGraph("test_graph");
    ASSERT_TRUE(first.has_value());

    auto dup = store_.createGraph("test_graph");
    EXPECT_FALSE(dup.has_value());
}

TEST_F(CatalogStoreTest, GetGraph) {
    ASSERT_TRUE(store_.open(db_path_ + "/catalog"));

    auto created = store_.createGraph("mygraph");
    ASSERT_TRUE(created.has_value());

    auto fetched = store_.getGraph("mygraph");
    ASSERT_TRUE(fetched.has_value());
    EXPECT_EQ(fetched->graph_id, created->graph_id);
    EXPECT_EQ(fetched->created_at, created->created_at);
}

TEST_F(CatalogStoreTest, GetNonExistentGraph) {
    ASSERT_TRUE(store_.open(db_path_ + "/catalog"));

    auto result = store_.getGraph("nonexistent");
    EXPECT_FALSE(result.has_value());
}

TEST_F(CatalogStoreTest, DropGraph) {
    ASSERT_TRUE(store_.open(db_path_ + "/catalog"));

    store_.createGraph("to_drop");
    EXPECT_TRUE(store_.dropGraph("to_drop"));
    EXPECT_FALSE(store_.getGraph("to_drop").has_value());
}

TEST_F(CatalogStoreTest, DropNonExistentGraph) {
    ASSERT_TRUE(store_.open(db_path_ + "/catalog"));

    auto result = store_.dropGraph("nonexistent");
    EXPECT_TRUE(result);
}

TEST_F(CatalogStoreTest, ListGraphs) {
    ASSERT_TRUE(store_.open(db_path_ + "/catalog"));

    store_.createGraph("alpha");
    store_.createGraph("beta");
    store_.createGraph("gamma");

    auto graphs = store_.listGraphs();
    EXPECT_EQ(graphs.size(), 3u);
}

TEST_F(CatalogStoreTest, GraphIdNotReused) {
    ASSERT_TRUE(store_.open(db_path_ + "/catalog"));

    auto first = store_.createGraph("first");
    ASSERT_TRUE(first.has_value());
    uint32_t first_id = first->graph_id;

    store_.dropGraph("first");

    auto second = store_.createGraph("second");
    ASSERT_TRUE(second.has_value());
    EXPECT_GT(second->graph_id, first_id);
}

TEST_F(CatalogStoreTest, MultipleGraphsSequentialIds) {
    ASSERT_TRUE(store_.open(db_path_ + "/catalog"));

    auto a = store_.createGraph("a");
    auto b = store_.createGraph("b");
    auto c = store_.createGraph("c");

    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    ASSERT_TRUE(c.has_value());

    EXPECT_LT(a->graph_id, b->graph_id);
    EXPECT_LT(b->graph_id, c->graph_id);
}

TEST_F(CatalogStoreTest, PersistAcrossReopen) {
    ASSERT_TRUE(store_.open(db_path_ + "/catalog"));

    store_.createGraph("persistent");
    store_.close();

    CatalogStore store2;
    ASSERT_TRUE(store2.open(db_path_ + "/catalog"));

    auto entry = store2.getGraph("persistent");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->graph_id, 0u);
    store2.close();
}

// ==================== GraphManager Tests ====================

class GraphManagerTest : public ::testing::Test {
protected:
    std::string db_path_;

    void SetUp() override {
        db_path_ = getTestDbPath();
        std::filesystem::remove_all(db_path_);
    }

    void TearDown() override {
        std::filesystem::remove_all(db_path_);
    }
};

TEST_F(GraphManagerTest, InitCreatesDefaultGraph) {
    GraphManager gm;
    ASSERT_TRUE(gm.init(db_path_, 2, 2));

    auto graphs = gm.listGraphs();
    ASSERT_EQ(graphs.size(), 1u);
    EXPECT_EQ(graphs[0].name, "default");

    auto lease = gm.acquireGraph("default");
    ASSERT_TRUE(lease);
    EXPECT_EQ(lease->inst->name, "default");
    lease.reset(); // 租约必须先于 shutdown 释放 ✓

    gm.shutdown();
}

TEST_F(GraphManagerTest, CreateGraph) {
    GraphManager gm;
    ASSERT_TRUE(gm.init(db_path_, 2, 2));

    auto entry = gm.createGraph("social");
    EXPECT_EQ(entry.name, "social");
    EXPECT_GT(entry.graph_id, 0u);

    auto lease = gm.acquireGraph("social");
    ASSERT_TRUE(lease);
    EXPECT_EQ(lease->inst->name, "social");
    lease.reset();

    gm.shutdown();
}

TEST_F(GraphManagerTest, CreateDuplicateGraph) {
    GraphManager gm;
    ASSERT_TRUE(gm.init(db_path_, 2, 2));

    auto first = gm.createGraph("social");
    auto second = gm.createGraph("social");
    EXPECT_EQ(first.graph_id, second.graph_id);

    gm.shutdown();
}

TEST_F(GraphManagerTest, DropGraph) {
    GraphManager gm;
    ASSERT_TRUE(gm.init(db_path_, 2, 2));

    gm.createGraph("temp");
    EXPECT_TRUE(gm.dropGraph("temp"));
    EXPECT_FALSE(gm.acquireGraph("temp").has_value());

    gm.shutdown();
}

TEST_F(GraphManagerTest, DropDefaultGraphFails) {
    GraphManager gm;
    ASSERT_TRUE(gm.init(db_path_, 2, 2));

    EXPECT_THROW(gm.dropGraph("default"), std::runtime_error);

    gm.shutdown();
}

TEST_F(GraphManagerTest, DropNonExistentGraphFails) {
    GraphManager gm;
    ASSERT_TRUE(gm.init(db_path_, 2, 2));

    EXPECT_THROW(gm.dropGraph("nonexistent"), std::runtime_error);

    gm.shutdown();
}

TEST_F(GraphManagerTest, ListGraphs) {
    GraphManager gm;
    ASSERT_TRUE(gm.init(db_path_, 2, 2));

    gm.createGraph("social");
    gm.createGraph("knowledge");

    auto graphs = gm.listGraphs();
    EXPECT_EQ(graphs.size(), 3u);

    gm.shutdown();
}

TEST_F(GraphManagerTest, MultiGraphIsolation) {
    GraphManager gm;
    ASSERT_TRUE(gm.init(db_path_, 2, 2));

    gm.createGraph("graph_a");
    gm.createGraph("graph_b");

    auto a = gm.acquireGraph("graph_a");
    auto b = gm.acquireGraph("graph_b");
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    EXPECT_NE(a->inst->async_meta.get(), b->inst->async_meta.get());
    EXPECT_NE(a->inst->async_data.get(), b->inst->async_data.get());
    a.reset();
    b.reset();

    gm.shutdown();
}

TEST_F(GraphManagerTest, GetNonExistentGraph) {
    GraphManager gm;
    ASSERT_TRUE(gm.init(db_path_, 2, 2));

    EXPECT_FALSE(gm.acquireGraph("nonexistent").has_value());

    gm.shutdown();
}

TEST_F(GraphManagerTest, GraphIdNotReused) {
    GraphManager gm;
    ASSERT_TRUE(gm.init(db_path_, 2, 2));

    auto first = gm.createGraph("first");
    uint32_t first_id = first.graph_id;

    gm.dropGraph("first");
    auto second = gm.createGraph("second");
    EXPECT_GT(second.graph_id, first_id);

    gm.shutdown();
}

TEST_F(GraphManagerTest, RestartLoadsGraphs) {
    {
        GraphManager gm;
        ASSERT_TRUE(gm.init(db_path_, 2, 2));
        gm.createGraph("persistent_graph");
        gm.shutdown();
    }

    GraphManager gm2;
    ASSERT_TRUE(gm2.init(db_path_, 2, 2));

    auto graphs = gm2.listGraphs();
    EXPECT_GE(graphs.size(), 2u);

    auto lease = gm2.acquireGraph("persistent_graph");
    ASSERT_TRUE(lease);
    EXPECT_EQ(lease->inst->name, "persistent_graph");
    lease.reset();

    gm2.shutdown();
}

TEST_F(GraphManagerTest, DefaultGraphIdIsZero) {
    GraphManager gm;
    ASSERT_TRUE(gm.init(db_path_, 2, 2));

    auto graphs = gm.listGraphs();
    ASSERT_FALSE(graphs.empty());
    EXPECT_EQ(graphs[0].name, "default");
    EXPECT_EQ(graphs[0].graph_id, 0u);

    gm.shutdown();
}

} // namespace

// ==================== 构建中 dropGraph（关服务取消在飞构建） ====================

TEST_F(GraphManagerTest, DropGraphDuringIndexBuildDoesNotHang) {
    GraphManager gm;
    ASSERT_TRUE(gm.init(db_path_, 2, 2));
    gm.createGraph("social");

    auto lease = gm.acquireGraph("social");
    ASSERT_TRUE(lease);
    auto* executor = lease->inst->executor.get();

    auto run = [&](const std::string& query) {
        auto ctx = blockingWait(executor->prepareStream(query, {}));
        EXPECT_TRUE(ctx->error.empty()) << ctx->error;
        auto gen = std::move(ctx->gen);
        blockingWait(folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
            while (auto chunk = co_await gen.next()) {}
        }));
        if (ctx->should_commit)
            blockingWait(ctx->commitAsync());
    };

    for (int i = 0; i < 500; ++i)
        run("CREATE (:Person {id:" + std::to_string(i) + ", email:'e" + std::to_string(i) + "'})");

    setenv("EUGRAPH_INDEX_BACKFILL_BATCH", "64", 1); // 让构建有多批，dropGraph 时有活可取消
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

    run("CREATE INDEX idx_person_email FOR (n:Person) ON (n.email)");
    first_batch_future.wait();
    release_scan.set_value();

    lease.reset();                       // dropGraph 需要独占；构建任务不持 usage 租约
    EXPECT_TRUE(gm.dropGraph("social")); // 内部 shutdown ⇒ 取消在飞构建 + join

    index_hooks::clear();
    unsetenv("EUGRAPH_INDEX_BACKFILL_BATCH");
    gm.shutdown();
}

// ==================== 启动回收：崩溃遗留 BUILDING + 半成品表 ====================

TEST_F(GraphManagerTest, StartupRecoveryCleansUnfinishedIndexTables) {
    GraphManager gm;
    ASSERT_TRUE(gm.init(db_path_, 2, 2));
    gm.createGraph("social");
    {
        auto lease = gm.acquireGraph("social");
        ASSERT_TRUE(lease);
        auto* inst = lease->inst;

        auto run = [&](const std::string& query) {
            auto ctx = blockingWait(inst->executor->prepareStream(query, {}));
            EXPECT_TRUE(ctx->error.empty()) << ctx->error;
            auto gen = std::move(ctx->gen);
            blockingWait(folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
                while (auto chunk = co_await gen.next()) {}
            }));
            if (ctx->should_commit)
                blockingWait(ctx->commitAsync());
        };
        run("CREATE (:Person {id:0, email:'e0'})");

        auto label = blockingWait(inst->async_meta->getLabelDef("Person"));
        ASSERT_TRUE(label.has_value());
        ASSERT_TRUE(blockingWait(inst->async_meta->createVertexIndex("idx_email", "Person", {"email"}, false)));
        auto with_index = blockingWait(inst->async_meta->getLabelDef("Person"));
        ASSERT_TRUE(with_index.has_value());
        ASSERT_EQ(with_index->indexes.size(), 1u);
        const uint32_t index_id = with_index->indexes[0].index_id;
        ASSERT_EQ(with_index->indexes[0].state, IndexState::WRITE_ONLY);

        // 模拟"构建中崩溃"：表已建、定义仍是 WRITE_ONLY，进程直接关闭。
        ASSERT_TRUE(inst->sync_data->createIndex(vidxTableById(index_id)));
        ASSERT_TRUE(inst->sync_data->createIndex(indexDeltaTable(index_id)));
        lease.reset();
    }
    gm.shutdown();

    GraphManager reopened;
    ASSERT_TRUE(reopened.init(db_path_, 2, 2));
    {
        auto lease = reopened.acquireGraph("social");
        ASSERT_TRUE(lease);
        auto* inst = lease->inst;

        auto ctx = blockingWait(inst->executor->prepareStream("SHOW INDEXES", {}));
        ASSERT_TRUE(ctx->error.empty()) << ctx->error;
        std::vector<Row> rows;
        auto gen = std::move(ctx->gen);
        blockingWait(folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
            while (auto chunk = co_await gen.next()) {
                for (auto& row : eugraph::test::chunkToRows(*chunk))
                    rows.push_back(std::move(row));
            }
        }));
        ASSERT_EQ(rows.size(), 1u);
        EXPECT_EQ(std::get<std::string>(rows[0][4]), "ERROR"); // 绝不恢复成 PUBLIC

        // 半成品表已由启动回收删除，记录也已销账。
        auto records = blockingWait(inst->async_meta->listCleanupRecords());
        EXPECT_TRUE(records.empty());
        lease.reset();
    }
    reopened.shutdown();
}
