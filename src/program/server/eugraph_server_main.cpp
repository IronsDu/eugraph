#include "service/bolt/bolt_server.hpp"
#include "service/graph_service.hpp"
#include "service/thrift/eugraph_handler.hpp"
#include "service/thrift/gen-cpp2/EuGraphService.h"
#include "storage/graph_manager.hpp"

#include <thrift/lib/cpp2/server/ThriftServer.h>

#include <folly/init/Init.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

using namespace eugraph;
using namespace eugraph::compute;

struct ServerConfig {
    int port = 9090;
    int bolt_port = 7687;
    std::string data_dir = "./eugraph-data";
    int compute_threads = 4;
    int io_threads = 4;
    int bolt_io_threads = 1;
    int wt_cache_size_mb = 256;
    int wt_evict_threads_max = 4;
    std::string wt_txn_sync = "fsync";
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
        } else if ((arg == "--bolt-port") && i + 1 < argc) {
            config.bolt_port = std::atoi(argv[++i]);
        } else if ((arg == "--bolt-io-threads") && i + 1 < argc) {
            config.bolt_io_threads = std::atoi(argv[++i]);
        } else if ((arg == "--wt-cache-size-mb") && i + 1 < argc) {
            config.wt_cache_size_mb = std::atoi(argv[++i]);
        } else if ((arg == "--wt-evict-threads-max") && i + 1 < argc) {
            config.wt_evict_threads_max = std::atoi(argv[++i]);
        } else if ((arg == "--wt-txn-sync") && i + 1 < argc) {
            config.wt_txn_sync = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: eugraph-server [options]\n"
                      << "Options:\n"
                      << "  --port, -p <port>        Server port (default: 9090)\n"
                      << "  --bolt-port <port>      Bolt protocol port (default: 7687, 0 to disable)\n"
                      << "  --bolt-io-threads <n>   Bolt EventBase threads (default: 1)\n"
                      << "  --data-dir, -d <path>    Data directory (default: ./eugraph-data)\n"
                      << "  --threads, -t <count>    Compute threads (default: 4)\n"
                      << "  --wt-cache-size-mb <n>  WiredTiger data cache size in MB (default: 256)\n"
                      << "  --wt-evict-threads-max <n>\n"
                      << "                           WiredTiger max eviction threads (default: 4)\n"
                      << "  --wt-txn-sync <mode>    WiredTiger commit sync: fsync|none (default: fsync)\n"
                      << "  --help, -h               Show this help\n";
            std::exit(0);
        }
    }
    return config;
}

int main(int argc, char* argv[]) {
    auto config = parseArgs(argc, argv);
    int folly_argc = 1;
    folly::Init init(&folly_argc, &argv);

    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [thread %t] [%l] %v");

    if (config.wt_cache_size_mb <= 0 || config.wt_evict_threads_max <= 0) {
        spdlog::error("--wt-cache-size-mb and --wt-evict-threads-max must be positive");
        return 1;
    }

    std::string txn_sync_cfg;
    if (config.wt_txn_sync == "none") {
        txn_sync_cfg = "transaction_sync=(enabled=false)";
    } else if (config.wt_txn_sync == "fsync") {
        txn_sync_cfg = "transaction_sync=(enabled=true,method=fsync)";
    } else {
        spdlog::error("--wt-txn-sync must be either 'fsync' or 'none'");
        return 1;
    }

    std::string data_wt_config = fmt::format("cache_size={}MB,eviction=(threads_max={}),{}", config.wt_cache_size_mb,
                                             config.wt_evict_threads_max, txn_sync_cfg);

    spdlog::info("Starting EuGraph server...");
    spdlog::info("  Port: {}", config.port);
    spdlog::info("  Data dir: {}", config.data_dir);
    spdlog::info("  WiredTiger data config: {}", data_wt_config);

    auto graph_manager = std::make_shared<GraphManager>();
    if (!graph_manager->init(config.data_dir, config.io_threads, config.compute_threads,
                             GraphManager::kDefaultCheckpointIntervalSec, data_wt_config)) {
        spdlog::error("Failed to initialize graph manager");
        return 1;
    }

    auto graph_service = std::make_shared<service::GraphService>(*graph_manager);
    auto handler = std::make_shared<service::thrift::EuGraphHandler>(*graph_service);

    auto server = std::make_shared<apache::thrift::ThriftServer>();
    server->setPort(config.port);
    server->setAllowPlaintextOnLoopback(true);
    server->setInterface(handler);

    server->setThreadManagerType(apache::thrift::ThriftServer::ThreadManagerType::SIMPLE);
    server->setMaxFinishedDebugPayloadsPerWorker(0);

    auto ioPool = std::make_shared<folly::IOThreadPoolExecutor>(
        config.io_threads, std::make_shared<folly::NamedThreadFactory>("ThriftIO"));
    server->setIOThreadPool(ioPool);
    server->setThreadManagerFromExecutor(ioPool.get());
    spdlog::info("  Using IO thread pool ({} threads) as handler executor", config.io_threads);

    spdlog::info("EuGraph server initialized successfully");
    spdlog::info("Listening on port {}...", config.port);

    // Start Bolt protocol server (if enabled)
    std::unique_ptr<service::bolt::BoltServer> bolt_server;
    if (config.bolt_port > 0) {
        bolt_server = std::make_unique<service::bolt::BoltServer>(
            *graph_service, static_cast<uint16_t>(config.bolt_port), static_cast<size_t>(config.bolt_io_threads));
        bolt_server->start();
        spdlog::info("Bolt server listening on port {}...", config.bolt_port);
    }

    server->serve();

    spdlog::info("Shutting down...");
    if (bolt_server) {
        bolt_server->stop();
    }
    graph_manager->shutdown();
    spdlog::info("Bye.");

    return 0;
}
