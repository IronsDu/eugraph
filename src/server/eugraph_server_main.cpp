#include "compute_service/executor/query_executor.hpp"
#include "gen-cpp2/EuGraphService.h"
#include "metadata_service/metadata_service.hpp"
#include "server/eugraph_handler.hpp"
#include "storage/graph_store_impl.hpp"

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
    int compute_threads = 0;
    int io_threads = 2;
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

    spdlog::info("Starting EuGraph server...");
    spdlog::info("  Port: {}", config.port);
    spdlog::info("  Data dir: {}", config.data_dir);

    // 1. Initialize storage layer
    auto store = std::make_unique<GraphStoreImpl>();
    if (!store->open(config.data_dir)) {
        spdlog::error("Failed to open database at {}", config.data_dir);
        return 1;
    }

    // 2. Initialize metadata service
    auto meta = std::make_unique<MetadataServiceImpl>();
    auto opened = folly::coro::blockingWait(meta->open(*store));
    if (!opened) {
        spdlog::error("Failed to initialize metadata service");
        return 1;
    }

    // 3. Initialize query executor
    QueryExecutor::Config executor_config;
    executor_config.compute_threads = config.compute_threads;
    executor_config.io_threads = config.io_threads;
    auto executor = std::make_unique<QueryExecutor>(*store, *meta, executor_config);

    // 4. Create Thrift handler
    auto handler = std::make_shared<server::EuGraphHandler>(*store, *meta, *executor);

    // 5. Create and start Thrift server
    auto server = std::make_shared<apache::thrift::ThriftServer>();
    server->setPort(config.port);
    server->setInterface(handler);

    // Create IO thread pool upfront so we can use it as both IO and handler
    // executor. This avoids the PriorityThreadManager's delayed eventfd
    // notification issue where responses from CPU worker threads don't wake
    // the IO thread promptly.
    auto ioPool = std::make_shared<folly::IOThreadPoolExecutor>(
        config.io_threads,
        std::make_shared<folly::NamedThreadFactory>("ThriftIO"));
    server->setIOThreadPool(ioPool);
    server->setThreadManagerFromExecutor(ioPool.get());
    spdlog::info("  Using IO thread pool ({} threads) as handler executor",
                 config.io_threads);

    spdlog::info("EuGraph server initialized successfully");
    spdlog::info("Listening on port {}...", config.port);

    // Block until server stops
    server->serve();

    // Cleanup
    spdlog::info("Shutting down...");
    folly::coro::blockingWait(meta->close());
    store->close();
    spdlog::info("Bye.");

    return 0;
}
