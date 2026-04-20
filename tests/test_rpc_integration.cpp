#include <gtest/gtest.h>

#include <folly/init/Init.h>

#include "compute_service/executor/query_executor.hpp"
#include "gen-cpp2/eugraph_types.h"
#include "metadata_service/metadata_service.hpp"
#include "server/eugraph_handler.hpp"
#include "shell/rpc_client.hpp"
#include "storage/graph_store_impl.hpp"

#include <chrono>
#include <filesystem>
#include <folly/coro/BlockingWait.h>
#include <folly/SocketAddress.h>
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
    std::unique_ptr<GraphStoreImpl> store_;
    std::unique_ptr<MetadataServiceImpl> meta_;
    std::unique_ptr<compute::QueryExecutor> executor_;
    std::shared_ptr<server::EuGraphHandler> handler_;

    // Real RPC server and client
    std::unique_ptr<apache::thrift::ScopedServerInterfaceThread> server_;
    std::unique_ptr<shell::EuGraphRpcClient> client_;

    void SetUp() override {
        db_path_ = getTestDbPath();
        std::filesystem::remove_all(db_path_);

        store_ = std::make_unique<GraphStoreImpl>();
        ASSERT_TRUE(store_->open(db_path_));

        meta_ = std::make_unique<MetadataServiceImpl>();
        auto opened = blockingWait(meta_->open(*store_));
        ASSERT_TRUE(opened);

        executor_ = std::make_unique<compute::QueryExecutor>(*store_, *meta_, compute::QueryExecutor::Config{});
        handler_ = std::make_shared<server::EuGraphHandler>(*store_, *meta_, *executor_);

        // Start real fbthrift server via ScopedServerInterfaceThread
        // Use a config callback to set the IO thread pool as handler executor
        // to avoid the PriorityThreadManager's ReplyQueue notification delay.
        auto ts = std::make_shared<apache::thrift::ThriftServer>();
        ts->setAddress(folly::SocketAddress("::1", 0));
        ts->setAllowPlaintextOnLoopback(true);
        ts->setInterface(handler_);
        ts->setNumIOWorkerThreads(1);
        ts->setNumCPUWorkerThreads(1);
        ts->setThreadManagerType(
            apache::thrift::ThriftServer::ThreadManagerType::SIMPLE);
        ts->setMaxFinishedDebugPayloadsPerWorker(0);
        ts->setThreadManagerFromExecutor(ts->getIOThreadPool().get());

        server_ = std::make_unique<apache::thrift::ScopedServerInterfaceThread>(ts);

        // Get a connected thrift client (uses default RocketClientChannel)
        auto thrift_client = server_->newClient<apache::thrift::Client<thrift::EuGraphService>>();

        // Wrap in EuGraphRpcClient (same wrapper the shell uses)
        client_ = std::make_unique<shell::EuGraphRpcClient>(std::move(thrift_client));
    }

    void TearDown() override {
        client_.reset();
        // Server teardown can deadlock when IO pool is shared with ThreadManager.
        // Release the pointer without blocking; OS will reclaim resources.
        (void)server_.release();
        handler_.reset();
        executor_.reset();
        blockingWait(meta_->close());
        store_->close();
        std::filesystem::remove_all(db_path_);
    }

    QueryResult execCypher(const std::string& query) {
        return client_->executeCypher(query);
    }

    LabelInfo createLabel(const std::string& name, std::vector<PropertyDefThrift> props = {}) {
        return client_->createLabel(name, props);
    }

    EdgeLabelInfo createEdgeLabel(const std::string& name, std::vector<PropertyDefThrift> props = {}) {
        return client_->createEdgeLabel(name, props);
    }
};

// ==================== DDL Tests ====================

TEST_F(RpcIntegrationTest, CreateAndListLabels) {
    auto info = createLabel("Person", {makePropDef("name", eugraph::thrift::PropertyType::STRING),
                                       makePropDef("age", eugraph::thrift::PropertyType::INT64)});
    EXPECT_EQ(info.id().value(), 1);
    EXPECT_EQ(info.name().value(), "Person");

    auto labels = client_->listLabels();
    EXPECT_EQ(labels.size(), 1);
}

TEST_F(RpcIntegrationTest, CreateAndListEdgeLabels) {
    auto info = createEdgeLabel("KNOWS");
    EXPECT_EQ(info.id().value(), 1);
    EXPECT_EQ(info.name().value(), "KNOWS");

    auto labels = client_->listEdgeLabels();
    EXPECT_EQ(labels.size(), 1);
}

// ==================== Vertex CRUD ====================

TEST_F(RpcIntegrationTest, CreateVerticesWithPropertiesAndQuery) {
    createLabel("Person", {makePropDef("name", eugraph::thrift::PropertyType::STRING),
                           makePropDef("age", eugraph::thrift::PropertyType::INT64)});

    auto r1 = execCypher("CREATE (alice:Person {name: \"Alice\", age: 30})");
    EXPECT_TRUE(r1.error().value().empty()) << "CREATE error: " << r1.error().value();

    auto r2 = execCypher("MATCH (n:Person) RETURN n.name, n.age");
    EXPECT_TRUE(r2.error().value().empty()) << "MATCH error: " << r2.error().value();
    ASSERT_EQ(r2.rows()->size(), 1);

    const auto& row = r2.rows()->at(0);
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
    EXPECT_TRUE(r1.error().value().empty()) << "CREATE edge: " << r1.error().value();

    auto r2 = execCypher("MATCH (n:Person) RETURN n.name");
    ASSERT_EQ(r2.rows()->size(), 2) << "Should have 2 Person vertices";

    auto r3 = execCypher("MATCH (a:Person {name: \"Alice\"})-[:KNOWS]->(b) RETURN b.name");
    EXPECT_TRUE(r3.error().value().empty()) << "Expand error: " << r3.error().value();
    ASSERT_EQ(r3.rows()->size(), 1) << "Alice should have 1 KNOWS neighbor";
    EXPECT_EQ(r3.rows()->at(0).values()->at(0).get_string_val(), "Bob");
}

TEST_F(RpcIntegrationTest, FullAdjacencyScenario) {
    createLabel("Person", {makePropDef("name", eugraph::thrift::PropertyType::STRING),
                           makePropDef("age", eugraph::thrift::PropertyType::INT64)});
    createEdgeLabel("KNOWS");

    execCypher("CREATE (alice:Person {name: \"Alice\", age: 30})-[:KNOWS]->(bob:Person {name: \"Bob\", age: 25})");
    execCypher("CREATE (bob:Person {name: \"Bob\", age: 25})-[:KNOWS]->(carol:Person {name: \"Carol\", age: 28})");
    execCypher("CREATE (alice:Person {name: \"Alice\", age: 30})-[:KNOWS]->(carol:Person {name: \"Carol\", age: 28})");

    auto r1 = execCypher("MATCH (n:Person) RETURN n.name");
    EXPECT_TRUE(r1.error().value().empty()) << "Scan error: " << r1.error().value();
    EXPECT_GE(r1.rows()->size(), 3);

    auto r2 = execCypher("MATCH (a:Person {name: \"Alice\"})-[:KNOWS]->(f) RETURN f.name");
    EXPECT_TRUE(r2.error().value().empty()) << "Alice expand error: " << r2.error().value();
    EXPECT_GE(r2.rows()->size(), 1) << "Alice should have at least 1 friend";

    auto r3 = execCypher("MATCH (a:Person)-[:KNOWS]->(b:Person) RETURN a.name, b.name");
    EXPECT_TRUE(r3.error().value().empty()) << "All edges error: " << r3.error().value();
    EXPECT_GE(r3.rows()->size(), 1) << "Should have at least 1 edge";
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
              << " error=" << r1.error().value() << "\n";

    t0 = Clock::now();
    auto r2 = execCypher("MATCH (n:Person) RETURN n.name, n.age");
    t1 = Clock::now();
    std::cout << "[PERF] MATCH scan (2 vertices): " << std::chrono::duration_cast<ms>(t1 - t0).count() << " ms"
              << " rows=" << r2.rows()->size() << "\n";

    t0 = Clock::now();
    auto r3 = execCypher("MATCH (n:Person {name: \"Alice\"}) RETURN n.name, n.age");
    t1 = Clock::now();
    std::cout << "[PERF] MATCH filter: " << std::chrono::duration_cast<ms>(t1 - t0).count() << " ms"
              << " rows=" << r3.rows()->size() << "\n";

    t0 = Clock::now();
    auto r4 = execCypher("MATCH (a:Person {name: \"Alice\"})-[:KNOWS]->(b) RETURN b.name, b.age");
    t1 = Clock::now();
    std::cout << "[PERF] MATCH expand: " << std::chrono::duration_cast<ms>(t1 - t0).count() << " ms"
              << " rows=" << r4.rows()->size() << " error=" << r4.error().value() << "\n";

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

} // anonymous namespace

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    folly::Init init(&argc, &argv, /*removeFlags=*/false);
    return RUN_ALL_TESTS();
}
