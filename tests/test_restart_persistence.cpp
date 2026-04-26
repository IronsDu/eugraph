/**
 * Test: Data persistence across simulated server restarts.
 *
 * Simulates a full restart cycle:
 *   1. Open stores, create schema, write data
 *   2. Close all stores (simulate shutdown)
 *   3. Re-open stores on the same path (simulate restart)
 *   4. Verify all data is still accessible
 */
#include <gtest/gtest.h>

#include "storage/data/async_graph_data_store.hpp"
#include "storage/data/sync_graph_data_store.hpp"
#include "storage/io_scheduler.hpp"
#include "storage/meta/async_graph_meta_store.hpp"
#include "storage/meta/sync_graph_meta_store.hpp"

#include <filesystem>
#include <folly/coro/BlockingWait.h>
#include <folly/init/Init.h>

using namespace eugraph;
using namespace folly::coro;

namespace {

std::string getTestDbPath() {
    return "/tmp/eugraph_restart_test_" + std::to_string(getpid());
}

// Re-usable helper to bootstrap a full storage stack from a db_path.
struct StorageStack {
    std::unique_ptr<SyncGraphDataStore> sync_data;
    std::unique_ptr<SyncGraphMetaStore> sync_meta;
    std::unique_ptr<IoScheduler> io;
    std::unique_ptr<AsyncGraphMetaStore> async_meta;
    std::unique_ptr<AsyncGraphDataStore> async_data;

    static std::unique_ptr<StorageStack> open(const std::string& db_path) {
        auto s = std::make_unique<StorageStack>();

        s->sync_data = std::make_unique<SyncGraphDataStore>();
        if (!s->sync_data->open(db_path + "/data"))
            return nullptr;

        s->sync_meta = std::make_unique<SyncGraphMetaStore>();
        if (!s->sync_meta->open(db_path + "/meta"))
            return nullptr;

        s->io = std::make_unique<IoScheduler>(2);
        s->async_data = std::make_unique<AsyncGraphDataStore>(*s->sync_data, *s->io);

        s->async_meta = std::make_unique<AsyncGraphMetaStore>();
        if (!blockingWait(s->async_meta->open(*s->sync_meta, *s->io)))
            return nullptr;

        return s;
    }

    void close() {
        async_data.reset();
        io.reset();
        blockingWait(async_meta->close());
        sync_data->close();
        sync_meta->close();
    }
};

class RestartPersistenceTest : public ::testing::Test {
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

// ==================== Test: Meta persistence (labels, edge labels, ID counters) ====================

TEST_F(RestartPersistenceTest, MetaDataPersistsAcrossRestart) {
    // --- Phase 1: Write ---
    {
        auto s = StorageStack::open(db_path_);
        ASSERT_TRUE(s);

        auto lid = blockingWait(s->async_meta->createLabel("Person"));
        ASSERT_NE(lid, INVALID_LABEL_ID);
        auto eid = blockingWait(s->async_meta->createEdgeLabel("KNOWS"));
        ASSERT_NE(eid, INVALID_EDGE_LABEL_ID);

        // Allocate some IDs
        auto vid1 = blockingWait(s->async_meta->nextVertexId());
        auto vid2 = blockingWait(s->async_meta->nextVertexId());
        EXPECT_EQ(vid1, 1);
        EXPECT_EQ(vid2, 2);

        s->close();
    }

    // --- Phase 2: Re-open and verify ---
    {
        auto s = StorageStack::open(db_path_);
        ASSERT_TRUE(s);

        // Labels should still be there
        auto lid = blockingWait(s->async_meta->getLabelId("Person"));
        ASSERT_TRUE(lid.has_value());
        EXPECT_EQ(*lid, 1);

        auto labels = blockingWait(s->async_meta->listLabels());
        ASSERT_EQ(labels.size(), 1u);
        EXPECT_EQ(labels[0].name, "Person");

        // Edge labels should still be there
        auto eid = blockingWait(s->async_meta->getEdgeLabelId("KNOWS"));
        ASSERT_TRUE(eid.has_value());
        EXPECT_EQ(*eid, 1);

        auto edge_labels = blockingWait(s->async_meta->listEdgeLabels());
        ASSERT_EQ(edge_labels.size(), 1u);
        EXPECT_EQ(edge_labels[0].name, "KNOWS");

        // ID counter should have advanced past the ones we allocated
        auto vid3 = blockingWait(s->async_meta->nextVertexId());
        EXPECT_GT(vid3, 2);

        s->close();
    }
}

// ==================== Test: Vertex data persistence ====================

TEST_F(RestartPersistenceTest, VertexDataPersistsAcrossRestart) {
    LabelId person_label = INVALID_LABEL_ID;

    // --- Phase 1: Create label + insert vertices ---
    {
        auto s = StorageStack::open(db_path_);
        ASSERT_TRUE(s);

        person_label = blockingWait(s->async_meta->createLabel("Person"));
        ASSERT_NE(person_label, INVALID_LABEL_ID);
        blockingWait(s->async_data->createLabel(person_label));

        // Insert vertices via sync store directly
        auto txn = s->sync_data->beginTransaction();
        for (VertexId vid = 1; vid <= 3; ++vid) {
            Properties props;
            props.resize(2);
            props[0] = PropertyValue(std::string("name_") + std::to_string(vid));
            props[1] = PropertyValue(static_cast<int64_t>(vid * 10));

            std::vector<std::pair<LabelId, Properties>> lp = {{person_label, std::move(props)}};
            ASSERT_TRUE(s->sync_data->insertVertex(txn, vid, lp, nullptr));
        }
        ASSERT_TRUE(s->sync_data->commitTransaction(txn));

        // Verify immediately
        EXPECT_EQ(s->sync_data->countVerticesByLabel(INVALID_GRAPH_TXN, person_label), 3u);

        s->close();
    }

    // --- Phase 2: Re-open and verify ---
    {
        auto s = StorageStack::open(db_path_);
        ASSERT_TRUE(s);

        // Label should still be known (loaded from meta)
        auto lid = blockingWait(s->async_meta->getLabelId("Person"));
        ASSERT_TRUE(lid.has_value());
        EXPECT_EQ(*lid, person_label);

        // Data tables are per-label; after restart we need to ensure the tables
        // still exist. WiredTiger tables persist on disk, so they should be usable
        // without re-creating them.
        uint64_t count = 0;
        s->sync_data->scanVerticesByLabel(INVALID_GRAPH_TXN, person_label, [&](VertexId vid) {
            EXPECT_GE(vid, 1u);
            EXPECT_LE(vid, 3u);
            ++count;
            return true;
        });
        EXPECT_EQ(count, 3u);

        // Verify properties survive
        auto props = s->sync_data->getVertexProperties(INVALID_GRAPH_TXN, 1, person_label);
        ASSERT_TRUE(props.has_value());
        ASSERT_GE(props->size(), 2u);
        ASSERT_TRUE((*props)[0].has_value());
        EXPECT_EQ(std::get<std::string>((*props)[0].value()), "name_1");
        ASSERT_TRUE((*props)[1].has_value());
        EXPECT_EQ(std::get<int64_t>((*props)[1].value()), 10);

        s->close();
    }
}

// ==================== Test: Edge data persistence ====================

TEST_F(RestartPersistenceTest, EdgeDataPersistsAcrossRestart) {
    LabelId person_label = INVALID_LABEL_ID;
    EdgeLabelId knows_label = INVALID_EDGE_LABEL_ID;

    // --- Phase 1: Create schema + insert vertices + edges ---
    {
        auto s = StorageStack::open(db_path_);
        ASSERT_TRUE(s);

        person_label = blockingWait(s->async_meta->createLabel("Person"));
        knows_label = blockingWait(s->async_meta->createEdgeLabel("KNOWS"));
        ASSERT_NE(person_label, INVALID_LABEL_ID);
        ASSERT_NE(knows_label, INVALID_EDGE_LABEL_ID);

        blockingWait(s->async_data->createLabel(person_label));
        blockingWait(s->async_data->createEdgeLabel(knows_label));

        auto txn = s->sync_data->beginTransaction();

        // Insert 2 vertices
        for (VertexId vid = 1; vid <= 2; ++vid) {
            std::vector<std::pair<LabelId, Properties>> lp = {{person_label, Properties{}}};
            ASSERT_TRUE(s->sync_data->insertVertex(txn, vid, lp, nullptr));
        }

        // Insert edge: 1 -[KNOWS]-> 2
        Properties edge_props;
        edge_props.resize(1);
        edge_props[0] = PropertyValue(static_cast<int64_t>(2024));
        ASSERT_TRUE(s->sync_data->insertEdge(txn, /*eid=*/1, /*src=*/1, /*dst=*/2, knows_label, /*seq=*/0, edge_props));

        ASSERT_TRUE(s->sync_data->commitTransaction(txn));

        s->close();
    }

    // --- Phase 2: Re-open and verify edges ---
    {
        auto s = StorageStack::open(db_path_);
        ASSERT_TRUE(s);

        // Verify edge traversal
        std::vector<ISyncGraphDataStore::EdgeIndexEntry> edges;
        s->sync_data->scanEdges(INVALID_GRAPH_TXN, /*vid=*/1, Direction::OUT, std::nullopt,
                                [&](const ISyncGraphDataStore::EdgeIndexEntry& e) {
                                    edges.push_back(e);
                                    return true;
                                });
        ASSERT_EQ(edges.size(), 1u);
        EXPECT_EQ(edges[0].neighbor_id, 2u);
        EXPECT_EQ(edges[0].edge_label_id, knows_label);

        // Verify edge properties
        auto eprops = s->sync_data->getEdgeProperties(INVALID_GRAPH_TXN, knows_label, edges[0].edge_id);
        ASSERT_TRUE(eprops.has_value());
        ASSERT_GE(eprops->size(), 1u);
        ASSERT_TRUE((*eprops)[0].has_value());
        EXPECT_EQ(std::get<int64_t>((*eprops)[0].value()), 2024);

        s->close();
    }
}

} // anonymous namespace

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    folly::Init init(&argc, &argv);
    return RUN_ALL_TESTS();
}
