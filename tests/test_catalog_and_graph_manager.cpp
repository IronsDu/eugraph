#include <gtest/gtest.h>

#include "storage/catalog/catalog_store.hpp"
#include "storage/graph_manager.hpp"

#include <filesystem>
#include <fstream>

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

/// A dropped graph's directory must be out of the way as soon as dropGraph returns.
///
/// Regression: the drop path used std::filesystem::remove_all, which was observed
/// spinning inside libstdc++'s recursive_directory_iterator for hours on a graph
/// directory (thread state R, CPU climbing, directory left half deleted), hanging the
/// dropping request and the TCK run with it. The path now renames the directory first
/// (one syscall, bounded effect) and deletes the renamed tree afterwards.
TEST_F(GraphManagerTest, DropGraphRetiresItsDirectoryBeforeReturning) {
    GraphManager gm;
    ASSERT_TRUE(gm.init(db_path_, 2, 2));

    const auto entry = gm.createGraph("temp");
    ASSERT_TRUE(gm.dropGraph("temp"));

    // The live directory name is what the spin blocked on, so it must be gone...
    std::error_code ec;
    const std::string dropped_dir = db_path_ + "/graph_" + std::to_string(entry.graph_id);
    EXPECT_FALSE(std::filesystem::exists(dropped_dir, ec)) << "dropped graph directory still present: " << dropped_dir;
    // ...while the default graph's own directory must be untouched.
    EXPECT_TRUE(std::filesystem::exists(db_path_ + "/graph_0", ec));

    // Housekeeping should have finished too, so nothing is left in trash.
    const std::string trash = db_path_ + "/trash";
    if (std::filesystem::exists(trash, ec)) {
        size_t leftovers = 0;
        for (const auto& entry : std::filesystem::directory_iterator(trash, ec))
            ++leftovers;
        EXPECT_EQ(leftovers, 0u) << "dropGraph left retired directories behind";
    }

    gm.shutdown();
}

/// Leftovers from an interrupted drop are swept at the next startup, so a crash midway
/// cannot leak disk forever.
TEST_F(GraphManagerTest, InitSweepsRetiredGraphDirectories) {
    std::error_code ec;
    const std::string trash = db_path_ + "/trash";
    const std::string leftover = trash + "/graph_999_123";
    std::filesystem::create_directories(leftover + "/data", ec);
    ASSERT_FALSE(ec);
    {
        std::ofstream f(leftover + "/data/WiredTigerLog.0000000001", std::ios::binary);
        f << "leftover log";
    }
    ASSERT_TRUE(std::filesystem::exists(leftover + "/data/WiredTigerLog.0000000001"));

    GraphManager gm;
    ASSERT_TRUE(gm.init(db_path_, 2, 2));

    EXPECT_FALSE(std::filesystem::exists(leftover, ec)) << "startup sweep left the retired directory behind";
    gm.shutdown();
}

/// Repeated create/drop leaves no retired directory behind, including when several
/// graphs are dropped in a row and while the periodic checkpoint thread is running.
///
/// This is the shape the TCK exercises (a fresh graph per scenario): it is also the
/// shape that produced both the remove_all spin and the WiredTiger log-server abort,
/// so it doubles as a smoke test for the retirement path.
TEST_F(GraphManagerTest, RepeatedCreateAndDropKeepsNoRetiredDirectories) {
    GraphManager gm;
    // Shortest legal interval, so the checkpoint thread is awake during the churn.
    ASSERT_TRUE(gm.init(db_path_, 2, 2, /*checkpoint_interval_sec=*/1));

    std::vector<uint32_t> ids;
    for (int i = 0; i < 24; ++i) {
        const auto entry = gm.createGraph("churn_" + std::to_string(i));
        ids.push_back(entry.graph_id);
        ASSERT_TRUE(gm.dropGraph("churn_" + std::to_string(i)));
    }

    std::error_code ec;
    for (uint32_t id : ids)
        EXPECT_FALSE(std::filesystem::exists(db_path_ + "/graph_" + std::to_string(id), ec))
            << "graph_" << id << " survived its drop";

    size_t leftovers = 0;
    const std::string trash = db_path_ + "/trash";
    if (std::filesystem::exists(trash, ec))
        for (const auto& entry : std::filesystem::directory_iterator(trash, ec))
            ++leftovers;
    EXPECT_EQ(leftovers, 0u) << "retired directories accumulated in trash";

    gm.shutdown();
}
