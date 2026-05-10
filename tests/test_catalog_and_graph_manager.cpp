#include <gtest/gtest.h>

#include "storage/catalog/catalog_store.hpp"
#include "storage/graph_manager.hpp"

#include <filesystem>

using namespace eugraph;

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

    auto* inst = gm.getGraph("default");
    ASSERT_NE(inst, nullptr);

    gm.shutdown();
}

TEST_F(GraphManagerTest, CreateGraph) {
    GraphManager gm;
    ASSERT_TRUE(gm.init(db_path_, 2, 2));

    auto entry = gm.createGraph("social");
    EXPECT_EQ(entry.name, "social");
    EXPECT_GT(entry.graph_id, 0u);

    auto* inst = gm.getGraph("social");
    ASSERT_NE(inst, nullptr);
    EXPECT_EQ(inst->name, "social");

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
    EXPECT_EQ(gm.getGraph("temp"), nullptr);

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

    auto* a = gm.getGraph("graph_a");
    auto* b = gm.getGraph("graph_b");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_NE(a->async_meta.get(), b->async_meta.get());
    EXPECT_NE(a->async_data.get(), b->async_data.get());

    gm.shutdown();
}

TEST_F(GraphManagerTest, GetNonExistentGraph) {
    GraphManager gm;
    ASSERT_TRUE(gm.init(db_path_, 2, 2));

    EXPECT_EQ(gm.getGraph("nonexistent"), nullptr);

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

    auto* inst = gm2.getGraph("persistent_graph");
    ASSERT_NE(inst, nullptr);
    EXPECT_EQ(inst->name, "persistent_graph");

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
