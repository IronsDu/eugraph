#include "compute_service/executor/query_executor.hpp"
#include "gen-cpp2/EuGraphService.h"
#include "server/eugraph_handler.hpp"
#include "storage/data/async_graph_data_store.hpp"
#include "storage/data/sync_graph_data_store.hpp"
#include "storage/io_scheduler.hpp"
#include "storage/meta/async_graph_meta_store.hpp"
#include "storage/meta/sync_graph_meta_store.hpp"

#include <thrift/lib/cpp2/server/ThriftServer.h>

#include <folly/coro/BlockingWait.h>
#include <folly/init/Init.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

using namespace eugraph;
using namespace eugraph::compute;

struct ServerConfig {
    int port = 9090;
    std::string data_dir = "./eugraph-data";
    int compute_threads = 4;
    int io_threads = 4;
};

static ServerConfig parseArgs(int argc, char* argv[]) {
    ServerConfig config;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
            config.port = std::atoi(argv[++i]);
        } else if ((arg == "--data-dir" || arg == "-d") && i + 1 < argc) {
            config.data_dir = argv[++i];
        } else if ((arg == "--threads" || arg == "-t") && i + 1 < argc) {
            config.compute_threads = std::atoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: eugraph-server [options]\n"
                      << "Options:\n"
                      << "  --port, -p <port>        Server port (default: 9090)\n"
                      << "  --data-dir, -d <path>    Data directory (default: ./eugraph-data)\n"
                      << "  --threads, -t <count>    Compute threads (default: 4)\n"
                      << "  --help, -h               Show this help\n";
            std::exit(0);
        }
    }
    return config;
}

int main(int argc, char* argv[]) {
    auto config = parseArgs(argc, argv);
    // folly::Init only needs program name; our custom flags confuse gflags
    int folly_argc = 1;
    folly::Init init(&folly_argc, &argv);

    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [thread %t] [%l] %v");

    spdlog::info("Starting EuGraph server...");
    spdlog::info("  Port: {}", config.port);
    spdlog::info("  Data dir: {}", config.data_dir);

    // 1. Create sync stores (independent WT connections)
    auto sync_data = std::make_unique<SyncGraphDataStore>();
    if (!sync_data->open(config.data_dir + "/data")) {
        spdlog::error("Failed to open data store at {}/data", config.data_dir);
        return 1;
    }

    auto sync_meta = std::make_unique<SyncGraphMetaStore>();
    if (!sync_meta->open(config.data_dir + "/meta")) {
        spdlog::error("Failed to open meta store at {}/meta", config.data_dir);
        return 1;
    }

    // 2. Create shared IoScheduler
    auto io_scheduler = std::make_shared<IoScheduler>(config.io_threads);

    // 3. Create async stores
    auto async_data = std::make_unique<AsyncGraphDataStore>(*sync_data, *io_scheduler);
    auto async_meta = std::make_unique<AsyncGraphMetaStore>();
    auto opened = folly::coro::blockingWait(async_meta->open(*sync_meta, *io_scheduler));
    if (!opened) {
        spdlog::error("Failed to initialize async meta store");
        return 1;
    }

    // 4. Create query executor
    QueryExecutor::Config executor_config;
    executor_config.compute_threads = config.compute_threads;
    auto executor = std::make_unique<QueryExecutor>(*async_data, *async_meta, executor_config);

    // 5. Create Thrift handler
    auto handler = std::make_shared<server::EuGraphHandler>(*async_data, *async_meta, *executor);

    // 6. Create and start Thrift server
    auto server = std::make_shared<apache::thrift::ThriftServer>();
    server->setPort(config.port);
    server->setAllowPlaintextOnLoopback(true);
    server->setInterface(handler);

    // Use SIMPLE ThreadManager to avoid PriorityThreadManager's ReplyQueue
    // eventfd notification delay, which causes Cypher execution responses to
    // stall on the IO thread.
    server->setThreadManagerType(apache::thrift::ThriftServer::ThreadManagerType::SIMPLE);
    server->setMaxFinishedDebugPayloadsPerWorker(0);

    // Create IO thread pool upfront so we can use it as both IO and handler
    // executor. This avoids the PriorityThreadManager's delayed eventfd
    // notification issue where responses from CPU worker threads don't wake
    // the IO thread promptly.
    auto ioPool = std::make_shared<folly::IOThreadPoolExecutor>(
        config.io_threads, std::make_shared<folly::NamedThreadFactory>("ThriftIO"));
    server->setIOThreadPool(ioPool);
    server->setThreadManagerFromExecutor(ioPool.get());
    spdlog::info("  Using IO thread pool ({} threads) as handler executor", config.io_threads);

    spdlog::info("EuGraph server initialized successfully");
    spdlog::info("Listening on port {}...", config.port);

    // Block until server stops
    server->serve();

    // Cleanup: close WT connections, which forces a checkpoint and flushes all data
    spdlog::info("Shutting down...");
    folly::coro::blockingWait(async_meta->close());
    sync_data->close();
    sync_meta->close();
    spdlog::info("Bye.");

    return 0;
}
