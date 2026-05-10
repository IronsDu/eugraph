#include <gtest/gtest.h>

#include <folly/init/Init.h>

#include "compute_service/executor/query_executor.hpp"
#include "gen-cpp2/eugraph_types.h"
#include "program/server/eugraph_handler.hpp"
#include "program/shell/rpc_client.hpp"
#include "storage/data/async_graph_data_store.hpp"
#include "storage/data/sync_graph_data_store.hpp"
#include "storage/graph_manager.hpp"
#include "storage/io_scheduler.hpp"
#include "storage/meta/async_graph_meta_store.hpp"
#include "storage/meta/sync_graph_meta_store.hpp"

#include <chrono>
#include <filesystem>
#include <folly/SocketAddress.h>
#include <folly/coro/BlockingWait.h>
#if defined(__SANITIZE_ADDRESS__)
#define EUGRAPH_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define EUGRAPH_ASAN 1
#endif
#endif

#if EUGRAPH_ASAN
#include <sanitizer/lsan_interface.h>
#endif

#include <thrift/lib/cpp2/server/ThriftServer.h>
#include <thrift/lib/cpp2/util/ScopedServerInterfaceThread.h>

using Clock = std::chrono::steady_clock;

using namespace eugraph;
using namespace eugraph::thrift;
using namespace folly::coro;

namespace {

std::string getTestDbPath() {
    return "/tmp/eugraph_rpc_test_" + std::to_string(getpid());
}

// Helper to create a PropertyDefThrift
PropertyDefThrift makePropDef(const std::string& name, eugraph::thrift::PropertyType type) {
    PropertyDefThrift pd;
    pd.name() = name;
    pd.type() = type;
    pd.is_required() = false;
    return pd;
}

class RpcIntegrationTest : public ::testing::Test {
protected:
    std::string db_path_;
    std::shared_ptr<GraphManager> graph_manager_;
    std::shared_ptr<server::EuGraphHandler> handler_;

    // Real RPC server and client
    std::unique_ptr<apache::thrift::ScopedServerInterfaceThread> server_;
    std::unique_ptr<shell::EuGraphRpcClient> client_;

    void SetUp() override {
        db_path_ = getTestDbPath();
        std::filesystem::remove_all(db_path_);

        graph_manager_ = std::make_shared<GraphManager>();
        ASSERT_TRUE(graph_manager_->init(db_path_, 2, 2));

        handler_ = std::make_shared<server::EuGraphHandler>(*graph_manager_);

        // Start real fbthrift server via ScopedServerInterfaceThread
        // Use a config callback to set the IO thread pool as handler executor
        // to avoid the PriorityThreadManager's ReplyQueue notification delay.
        auto ts = std::make_shared<apache::thrift::ThriftServer>();
        ts->setAddress(folly::SocketAddress("::1", 0));
        ts->setAllowPlaintextOnLoopback(true);
        ts->setInterface(handler_);
        ts->setNumIOWorkerThreads(1);
        ts->setNumCPUWorkerThreads(1);
        ts->setThreadManagerType(apache::thrift::ThriftServer::ThreadManagerType::SIMPLE);
        ts->setMaxFinishedDebugPayloadsPerWorker(0);
        ts->setThreadManagerFromExecutor(ts->getIOThreadPool().get());

        // Suppress LSan for this allocation: the server must be leaked in
        // TearDown to avoid a deadlock when IO pool is shared with ThreadManager.
#if EUGRAPH_ASAN
        __lsan_disable();
#endif
        server_ = std::make_unique<apache::thrift::ScopedServerInterfaceThread>(ts);
#if EUGRAPH_ASAN
        __lsan_enable();
#endif

        // Connect via real TCP socket, same path as the shell client.
        client_ = std::make_unique<shell::EuGraphRpcClient>("::1", server_->getPort());
        ASSERT_TRUE(client_->connect());
    }

    void TearDown() override {
        client_.reset();
        // Server teardown can deadlock when IO pool is shared with ThreadManager.
        // Release the pointer without blocking; OS will reclaim resources.
        (void)server_.release();
        handler_.reset();
        graph_manager_->shutdown();
        graph_manager_.reset();
        std::filesystem::remove_all(db_path_);
    }

    struct CypherResult {
        std::string error;
        std::vector<ResultRow> rows;
        std::vector<std::string> columns;
    };

    CypherResult execCypher(const std::string& query) {
        return execCypherOnGraph(query, "default");
    }

    CypherResult execCypherOnGraph(const std::string& query, const std::string& graph_name) {
        CypherResult result;
        try {
            auto [meta, stream] = client_->executeCypher(query, graph_name);
            result.columns = *meta.columns();
            std::move(stream).subscribeInline([&](folly::Try<ResultRowBatch> batch) {
                if (batch.hasValue()) {
                    for (auto& row : *batch->rows()) {
                        result.rows.push_back(std::move(row));
                    }
                } else if (batch.hasException()) {
                    result.error = batch.exception().what();
                }
            });
        } catch (const std::exception& e) {
            result.error = e.what();
        }
        return result;
    }

    LabelInfo createLabel(const std::string& name, std::vector<PropertyDefThrift> props = {}) {
        return client_->createLabel(name, props, "default");
    }

    EdgeLabelInfo createEdgeLabel(const std::string& name, std::vector<PropertyDefThrift> props = {}) {
        return client_->createEdgeLabel(name, props, "default");
    }
};

// ==================== DDL Tests ====================

TEST_F(RpcIntegrationTest, CreateAndListLabels) {
    auto info = createLabel("Person", {makePropDef("name", eugraph::thrift::PropertyType::STRING),
                                       makePropDef("age", eugraph::thrift::PropertyType::INT64)});
    EXPECT_EQ(info.id().value(), 1);
    EXPECT_EQ(info.name().value(), "Person");

    auto labels = client_->listLabels("default");
    EXPECT_EQ(labels.size(), 1);
}

TEST_F(RpcIntegrationTest, CreateAndListEdgeLabels) {
    auto info = createEdgeLabel("KNOWS");
    EXPECT_EQ(info.id().value(), 1);
    EXPECT_EQ(info.name().value(), "KNOWS");

    auto labels = client_->listEdgeLabels("default");
    EXPECT_EQ(labels.size(), 1);
}

// ==================== Index DDL via RPC ====================

TEST_F(RpcIntegrationTest, CreateAndShowIndexes) {
    createLabel("Person", {makePropDef("name", eugraph::thrift::PropertyType::STRING),
                           makePropDef("age", eugraph::thrift::PropertyType::INT64),
                           makePropDef("city", eugraph::thrift::PropertyType::STRING)});

    // CREATE INDEX (non-unique)
    auto r1 = execCypher("CREATE INDEX idx_name FOR (n:Person) ON (n.name)");
    EXPECT_TRUE(r1.error.empty()) << "CREATE INDEX error: " << r1.error;
    ASSERT_EQ(r1.rows.size(), 1);
    EXPECT_NE(r1.rows[0].values()->at(0).get_string_val().find("Index created"), std::string::npos);

    // CREATE UNIQUE INDEX
    auto r2 = execCypher("CREATE UNIQUE INDEX idx_city FOR (n:Person) ON (n.city)");
    EXPECT_TRUE(r2.error.empty()) << "CREATE UNIQUE INDEX error: " << r2.error;
    ASSERT_EQ(r2.rows.size(), 1);
    EXPECT_NE(r2.rows[0].values()->at(0).get_string_val().find("Index created"), std::string::npos);

    // SHOW INDEXES (list all)
    auto r3 = execCypher("SHOW INDEXES");
    EXPECT_TRUE(r3.error.empty()) << "SHOW INDEXES error: " << r3.error;
    ASSERT_EQ(r3.rows.size(), 2);
    // Verify idx_name (non-unique)
    EXPECT_EQ(r3.rows[0].values()->at(0).get_string_val(), "idx_name");
    EXPECT_EQ(r3.rows[0].values()->at(1).get_string_val(), "Person");
    EXPECT_EQ(r3.rows[0].values()->at(2).get_string_val(), "name");
    EXPECT_EQ(r3.rows[0].values()->at(3).get_string_val(), "false");
    EXPECT_EQ(r3.rows[0].values()->at(4).get_string_val(), "PUBLIC");
    // Verify idx_city (unique)
    EXPECT_EQ(r3.rows[1].values()->at(0).get_string_val(), "idx_city");
    EXPECT_EQ(r3.rows[1].values()->at(1).get_string_val(), "Person");
    EXPECT_EQ(r3.rows[1].values()->at(2).get_string_val(), "city");
    EXPECT_EQ(r3.rows[1].values()->at(3).get_string_val(), "true");
    EXPECT_EQ(r3.rows[1].values()->at(4).get_string_val(), "PUBLIC");
}

TEST_F(RpcIntegrationTest, DropIndex) {
    createLabel("Person", {makePropDef("name", eugraph::thrift::PropertyType::STRING)});
    execCypher("CREATE INDEX idx_name FOR (n:Person) ON (n.name)");

    auto r1 = execCypher("DROP INDEX idx_name");
    EXPECT_TRUE(r1.error.empty()) << "DROP INDEX error: " << r1.error;
    ASSERT_EQ(r1.rows.size(), 1);
    EXPECT_NE(r1.rows[0].values()->at(0).get_string_val().find("Index dropped"), std::string::npos);

    // Verify it's gone
    auto r2 = execCypher("SHOW INDEXES");
    EXPECT_TRUE(r2.error.empty());
    ASSERT_EQ(r2.rows.size(), 0);
}

TEST_F(RpcIntegrationTest, CreateIndexLabelNotFound) {
    auto r = execCypher("CREATE INDEX idx_x FOR (n:NoSuchLabel) ON (n.prop)");
    EXPECT_FALSE(r.error.empty());
    EXPECT_NE(r.error.find("Label not found"), std::string::npos);
}

TEST_F(RpcIntegrationTest, UniqueIndexConstraintViaRpc) {
    createLabel("Person", {makePropDef("name", eugraph::thrift::PropertyType::STRING)});

    // Insert a vertex first so backfill has data to check
    auto r0 = execCypher("CREATE (n:Person {name: \"alice\"})");
    EXPECT_TRUE(r0.error.empty()) << "CREATE error: " << r0.error;

    // Create unique index after data exists — backfill should succeed
    auto r1 = execCypher("CREATE UNIQUE INDEX idx_name FOR (n:Person) ON (n.name)");
    EXPECT_TRUE(r1.error.empty()) << "CREATE UNIQUE INDEX error: " << r1.error;
    ASSERT_EQ(r1.rows.size(), 1);

    // Verify PUBLIC state
    auto r2 = execCypher("SHOW INDEXES");
    EXPECT_TRUE(r2.error.empty());
    ASSERT_EQ(r2.rows.size(), 1);
    EXPECT_EQ(r2.rows[0].values()->at(3).get_string_val(), "true");
    EXPECT_EQ(r2.rows[0].values()->at(4).get_string_val(), "PUBLIC");

    // Insert different name — should succeed
    auto r3 = execCypher("CREATE (n:Person {name: \"bob\"})");
    EXPECT_TRUE(r3.error.empty()) << "Insert different name should succeed: " << r3.error;

    // Insert duplicate name — should be rejected by unique constraint
    // (currently enforced silently: 0 rows returned, no error string)
    auto r4 = execCypher("CREATE (n:Person {name: \"alice\"})");
    EXPECT_TRUE(r4.error.empty());
    EXPECT_TRUE(r4.rows.empty()) << "Duplicate insert should return 0 rows (rejected)";

    // Verify only 1 'alice' vertex
    auto r5 = execCypher("MATCH (n:Person) WHERE n.name = 'alice' RETURN n.name");
    EXPECT_TRUE(r5.error.empty()) << "Scan error: " << r5.error;
    EXPECT_EQ(r5.rows.size(), 1u) << "Should have exactly 1 'alice' vertex";
}

// ==================== Vertex CRUD ====================

TEST_F(RpcIntegrationTest, CreateVerticesWithPropertiesAndQuery) {
    createLabel("Person", {makePropDef("name", eugraph::thrift::PropertyType::STRING),
                           makePropDef("age", eugraph::thrift::PropertyType::INT64)});

    auto r1 = execCypher("CREATE (alice:Person {name: \"Alice\", age: 30})");
    EXPECT_TRUE(r1.error.empty()) << "CREATE error: " << r1.error;

    auto r2 = execCypher("MATCH (n:Person) RETURN n.name, n.age");
    EXPECT_TRUE(r2.error.empty()) << "MATCH error: " << r2.error;
    ASSERT_EQ(r2.rows.size(), 1);

    const auto& row = r2.rows.at(0);
    ASSERT_EQ(row.values()->size(), 2);
    EXPECT_EQ(row.values()->at(0).getType(), ResultValue::Type::string_val);
    EXPECT_EQ(row.values()->at(0).get_string_val(), "Alice");
    EXPECT_EQ(row.values()->at(1).getType(), ResultValue::Type::int_val);
    EXPECT_EQ(row.values()->at(1).get_int_val(), 30);
}

// ==================== Adjacency Queries ====================

TEST_F(RpcIntegrationTest, CreateEdgeInSingleStatementAndExpand) {
    createLabel("Person", {makePropDef("name", eugraph::thrift::PropertyType::STRING),
                           makePropDef("age", eugraph::thrift::PropertyType::INT64)});
    createEdgeLabel("KNOWS");

    auto r1 = execCypher("CREATE (alice:Person {name: \"Alice\"})-[:KNOWS]->(bob:Person {name: \"Bob\"})");
    EXPECT_TRUE(r1.error.empty()) << "CREATE edge: " << r1.error;

    auto r2 = execCypher("MATCH (n:Person) RETURN n.name");
    ASSERT_EQ(r2.rows.size(), 2) << "Should have 2 Person vertices";

    auto r3 = execCypher("MATCH (a:Person {name: \"Alice\"})-[:KNOWS]->(b) RETURN b.name");
    EXPECT_TRUE(r3.error.empty()) << "Expand error: " << r3.error;
    ASSERT_EQ(r3.rows.size(), 1) << "Alice should have 1 KNOWS neighbor";
    EXPECT_EQ(r3.rows.at(0).values()->at(0).get_string_val(), "Bob");
}

TEST_F(RpcIntegrationTest, FullAdjacencyScenario) {
    createLabel("Person", {makePropDef("name", eugraph::thrift::PropertyType::STRING),
                           makePropDef("age", eugraph::thrift::PropertyType::INT64)});
    createEdgeLabel("KNOWS");

    execCypher("CREATE (alice:Person {name: \"Alice\", age: 30})-[:KNOWS]->(bob:Person {name: \"Bob\", age: 25})");
    execCypher("CREATE (bob:Person {name: \"Bob\", age: 25})-[:KNOWS]->(carol:Person {name: \"Carol\", age: 28})");
    execCypher("CREATE (alice:Person {name: \"Alice\", age: 30})-[:KNOWS]->(carol:Person {name: \"Carol\", age: 28})");

    auto r1 = execCypher("MATCH (n:Person) RETURN n.name");
    EXPECT_TRUE(r1.error.empty()) << "Scan error: " << r1.error;
    EXPECT_GE(r1.rows.size(), 3);

    auto r2 = execCypher("MATCH (a:Person {name: \"Alice\"})-[:KNOWS]->(f) RETURN f.name");
    EXPECT_TRUE(r2.error.empty()) << "Alice expand error: " << r2.error;
    EXPECT_GE(r2.rows.size(), 1) << "Alice should have at least 1 friend";

    auto r3 = execCypher("MATCH (a:Person)-[:KNOWS]->(b:Person) RETURN a.name, b.name");
    EXPECT_TRUE(r3.error.empty()) << "All edges error: " << r3.error;
    EXPECT_GE(r3.rows.size(), 1) << "Should have at least 1 edge";
}

// ==================== Performance Benchmark ====================

TEST_F(RpcIntegrationTest, PerformanceBenchmark) {
    using ms = std::chrono::milliseconds;

    auto t0 = Clock::now();
    createLabel("Person", {makePropDef("name", eugraph::thrift::PropertyType::STRING),
                           makePropDef("age", eugraph::thrift::PropertyType::INT64)});
    createEdgeLabel("KNOWS");
    auto t1 = Clock::now();
    std::cout << "[PERF] DDL (createLabel + createEdgeLabel): " << std::chrono::duration_cast<ms>(t1 - t0).count()
              << " ms\n";

    t0 = Clock::now();
    auto r1 =
        execCypher("CREATE (alice:Person {name: \"Alice\", age: 30})-[:KNOWS]->(bob:Person {name: \"Bob\", age: 25})");
    t1 = Clock::now();
    std::cout << "[PERF] CREATE (vertex+edge): " << std::chrono::duration_cast<ms>(t1 - t0).count() << " ms"
              << " error=" << r1.error << "\n";

    t0 = Clock::now();
    auto r2 = execCypher("MATCH (n:Person) RETURN n.name, n.age");
    t1 = Clock::now();
    std::cout << "[PERF] MATCH scan (2 vertices): " << std::chrono::duration_cast<ms>(t1 - t0).count() << " ms"
              << " rows=" << r2.rows.size() << "\n";

    t0 = Clock::now();
    auto r3 = execCypher("MATCH (n:Person {name: \"Alice\"}) RETURN n.name, n.age");
    t1 = Clock::now();
    std::cout << "[PERF] MATCH filter: " << std::chrono::duration_cast<ms>(t1 - t0).count() << " ms"
              << " rows=" << r3.rows.size() << "\n";

    t0 = Clock::now();
    auto r4 = execCypher("MATCH (a:Person {name: \"Alice\"})-[:KNOWS]->(b) RETURN b.name, b.age");
    t1 = Clock::now();
    std::cout << "[PERF] MATCH expand: " << std::chrono::duration_cast<ms>(t1 - t0).count() << " ms"
              << " rows=" << r4.rows.size() << " error=" << r4.error << "\n";

    t0 = Clock::now();
    auto r5 = execCypher("MATCH (a:Person {name: \"Alice\"})-[:KNOWS]->(b) RETURN b.name");
    t1 = Clock::now();
    std::cout << "[PERF] MATCH expand (repeat): " << std::chrono::duration_cast<ms>(t1 - t0).count() << " ms\n";

    t0 = Clock::now();
    auto r6 = execCypher("MATCH (n:Person) RETURN n.name");
    t1 = Clock::now();
    std::cout << "[PERF] MATCH scan (repeat): " << std::chrono::duration_cast<ms>(t1 - t0).count() << " ms\n";
}

TEST_F(RpcIntegrationTest, RPCDetailedTiming) {
    using us = std::chrono::microseconds;

    createLabel("Person", {makePropDef("name", eugraph::thrift::PropertyType::STRING),
                           makePropDef("age", eugraph::thrift::PropertyType::INT64)});
    createEdgeLabel("KNOWS");

    execCypher("CREATE (a:Person {name: \"Alice\", age: 30})-[:KNOWS]->(b:Person {name: \"Bob\", age: 25})");

    constexpr int N = 20;
    std::vector<long> create_times, scan_times, filter_times, expand_times;

    for (int i = 0; i < N; i++) {
        auto t0 = Clock::now();
        execCypher("CREATE (x:Person {name: \"X\", age: " + std::to_string(i) + "})");
        auto t1 = Clock::now();
        create_times.push_back(std::chrono::duration_cast<us>(t1 - t0).count());

        t0 = Clock::now();
        execCypher("MATCH (n:Person) RETURN n.name");
        t1 = Clock::now();
        scan_times.push_back(std::chrono::duration_cast<us>(t1 - t0).count());

        t0 = Clock::now();
        execCypher("MATCH (n:Person {name: \"Alice\"}) RETURN n.name");
        t1 = Clock::now();
        filter_times.push_back(std::chrono::duration_cast<us>(t1 - t0).count());

        t0 = Clock::now();
        execCypher("MATCH (a:Person {name: \"Alice\"})-[:KNOWS]->(b) RETURN b.name");
        t1 = Clock::now();
        expand_times.push_back(std::chrono::duration_cast<us>(t1 - t0).count());
    }

    auto avg = [](const std::vector<long>& v) -> long {
        long sum = 0;
        for (auto x : v)
            sum += x;
        return sum / (long)v.size();
    };

    std::cout << "\n====== RPC Detailed Timing (avg over " << N << " runs) ======\n";
    std::cout << "CREATE:   " << avg(create_times) << " us\n";
    std::cout << "SCAN:     " << avg(scan_times) << " us\n";
    std::cout << "FILTER:   " << avg(filter_times) << " us\n";
    std::cout << "EXPAND:   " << avg(expand_times) << " us\n";
}

TEST_F(RpcIntegrationTest, MetadataOverheadAnalysis) {
    using us = std::chrono::microseconds;

    createLabel("Person", {makePropDef("name", eugraph::thrift::PropertyType::STRING),
                           makePropDef("age", eugraph::thrift::PropertyType::INT64)});
    createEdgeLabel("KNOWS");

    for (int i = 0; i < 50; i++) {
        execCypher("CREATE (p:Person {name: \"P" + std::to_string(i) + "\", age: " + std::to_string(i) + "})");
    }

    constexpr int M = 10;
    std::vector<long> scan_small, scan_large;

    for (int i = 0; i < M; i++) {
        auto t0 = Clock::now();
        execCypher("MATCH (n:Person) RETURN n.name, n.age");
        auto t1 = Clock::now();
        scan_small.push_back(std::chrono::duration_cast<us>(t1 - t0).count());
    }

    for (int i = 0; i < 50; i++) {
        execCypher("CREATE (q:Person {name: \"Q" + std::to_string(i) + "\", age: " + std::to_string(i + 50) + "})");
    }

    for (int i = 0; i < M; i++) {
        auto t0 = Clock::now();
        execCypher("MATCH (n:Person) RETURN n.name, n.age");
        auto t1 = Clock::now();
        scan_large.push_back(std::chrono::duration_cast<us>(t1 - t0).count());
    }

    auto avg = [](const std::vector<long>& v) -> long {
        long sum = 0;
        for (auto x : v)
            sum += x;
        return sum / (long)v.size();
    };

    std::cout << "\n====== Scan Scaling Analysis ======\n";
    std::cout << "Scan ~52 vertices:  " << avg(scan_small) << " us\n";
    std::cout << "Scan ~102 vertices: " << avg(scan_large) << " us\n";
    std::cout << "Per-vertex overhead: " << (avg(scan_large) - avg(scan_small)) / 50 << " us/vertex\n";
    std::cout << "Fixed overhead (approx): " << avg(scan_small) - 52 * ((avg(scan_large) - avg(scan_small)) / 50)
              << " us\n";
}

// ==================== Graph Management Integration Tests ====================

TEST_F(RpcIntegrationTest, CreateAndListGraphs) {
    auto info = client_->createGraph("social");
    EXPECT_EQ(info.name().value(), "social");
    EXPECT_GT(info.graph_id().value(), 0);

    auto graphs = client_->listGraphs();
    ASSERT_EQ(graphs.size(), 2u);

    EXPECT_EQ(graphs[0].name().value(), "default");
    EXPECT_EQ(graphs[1].name().value(), "social");
}

TEST_F(RpcIntegrationTest, CreateDuplicateGraph) {
    auto first = client_->createGraph("test");
    auto second = client_->createGraph("test");
    EXPECT_EQ(first.graph_id().value(), second.graph_id().value());
}

TEST_F(RpcIntegrationTest, DropGraph) {
    client_->createGraph("temp");
    EXPECT_TRUE(client_->dropGraph("temp"));

    auto graphs = client_->listGraphs();
    for (const auto& g : graphs) {
        EXPECT_NE(g.name().value(), "temp");
    }
}

TEST_F(RpcIntegrationTest, DropDefaultGraphFails) {
    EXPECT_THROW(client_->dropGraph("default"), std::exception);
}

TEST_F(RpcIntegrationTest, DropNonExistentGraphFails) {
    EXPECT_THROW(client_->dropGraph("nonexistent"), std::exception);
}

TEST_F(RpcIntegrationTest, GraphIdNotReused) {
    auto first = client_->createGraph("first");
    client_->dropGraph("first");
    auto second = client_->createGraph("second");
    EXPECT_GT(second.graph_id().value(), first.graph_id().value());
}

TEST_F(RpcIntegrationTest, MultiGraphLabelIsolation) {
    client_->createGraph("graph_a");
    client_->createGraph("graph_b");

    auto label_a =
        client_->createLabel("Person", {makePropDef("name", eugraph::thrift::PropertyType::STRING)}, "graph_a");
    EXPECT_GT(label_a.id().value(), 0);

    auto labels_a = client_->listLabels("graph_a");
    ASSERT_EQ(labels_a.size(), 1u);
    EXPECT_EQ(labels_a[0].name().value(), "Person");

    auto labels_b = client_->listLabels("graph_b");
    EXPECT_EQ(labels_b.size(), 0u);
}

TEST_F(RpcIntegrationTest, MultiGraphCypherIsolation) {
    client_->createGraph("alpha");
    client_->createGraph("beta");

    client_->createLabel("Node", {makePropDef("val", eugraph::thrift::PropertyType::INT64)}, "alpha");

    execCypherOnGraph("CREATE (n:Node {val: 42})", "alpha");

    auto result_a = execCypherOnGraph("MATCH (n:Node) RETURN n.val", "alpha");
    ASSERT_EQ(result_a.rows.size(), 1u);

    auto result_b = execCypherOnGraph("MATCH (n:Node) RETURN n.val", "beta");
    EXPECT_EQ(result_b.rows.size(), 0u);
}

} // anonymous namespace

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    folly::Init init(&argc, &argv, /*removeFlags=*/false);
    return RUN_ALL_TESTS();
}
