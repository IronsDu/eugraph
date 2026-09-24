#include "service/bolt/bolt_server.hpp"
#include "service/graph_service.hpp"
#include "service/thrift/eugraph_handler.hpp"
#include "service/thrift/gen-cpp2/EuGraphService.h"
#include "storage/graph_manager.hpp"

#include <thrift/lib/cpp2/server/ThriftServer.h>

#include <args.hxx>

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
    int thrift_port = 9090;
    int bolt_port = 7687;
    std::string data_dir = "./eugraph-data";
    int compute_threads = 4;
    int storage_io_threads = 4;
    int thrift_io_threads = 4;
    int bolt_io_threads = 1;
    int wt_cache_size_mb = 256;
    int wt_evict_threads_max = 4;
    std::string wt_txn_sync = "fsync";
    /// WiredTiger verbose categories (empty = off). Diagnostics only: `log` /
    /// `log_server` make WT report why a log force-write failed, which is otherwise
    /// swallowed by its fail-stop panic.
    std::string wt_verbose;
};

static ServerConfig parseArgs(int argc, char* argv[]) {
    ServerConfig config;
    args::ArgumentParser parser("EuGraph server.");
    parser.helpParams.addDefault = true;
    args::HelpFlag help(parser, "help", "Show this help menu", {'h', "help"});
    args::ValueFlag<int> thrift_port(parser, "port", "Thrift RPC port", {"thrift-port"}, config.thrift_port);
    args::ValueFlag<int> bolt_port(parser, "port", "Bolt protocol port; 0 disables Bolt", {"bolt-port"},
                                   config.bolt_port);
    args::ValueFlag<std::string> data_dir(parser, "path", "Data directory", {'d', "data-dir"}, config.data_dir);
    args::ValueFlag<int> compute_threads(parser, "n", "Query compute threads", {"compute-threads"},
                                         config.compute_threads);
    args::ValueFlag<int> storage_io_threads(parser, "n", "Storage IO threads", {"storage-io-threads"},
                                            config.storage_io_threads);
    args::ValueFlag<int> thrift_io_threads(parser, "n", "Thrift IO and handler executor threads", {"thrift-io-threads"},
                                           config.thrift_io_threads);
    args::ValueFlag<int> bolt_io_threads(parser, "n", "Bolt EventBase threads", {"bolt-io-threads"},
                                         config.bolt_io_threads);
    args::ValueFlag<int> wt_cache_size_mb(parser, "n", "WiredTiger data cache size in MB", {"wt-cache-size-mb"},
                                          config.wt_cache_size_mb);
    args::ValueFlag<int> wt_evict_threads_max(parser, "n", "WiredTiger max eviction threads", {"wt-evict-threads-max"},
                                              config.wt_evict_threads_max);
    args::ValueFlag<std::string> wt_txn_sync(parser, "mode", "WiredTiger commit sync: fsync|none", {"wt-txn-sync"},
                                             config.wt_txn_sync);
    args::ValueFlag<std::string> wt_verbose(parser, "cats",
                                            "WiredTiger verbose categories, comma separated (e.g. log,log_server)",
                                            {"wt-verbose"}, config.wt_verbose);

    try {
        parser.ParseCLI(argc, argv);
    } catch (const args::Help&) {
        std::cout << parser;
        std::exit(0);
    } catch (const args::ParseError& e) {
        std::cerr << e.what() << '\n';
        std::cerr << parser;
        std::exit(1);
    } catch (const args::ValidationError& e) {
        std::cerr << e.what() << '\n';
        std::cerr << parser;
        std::exit(1);
    }

    config.thrift_port = args::get(thrift_port);
    config.bolt_port = args::get(bolt_port);
    config.data_dir = args::get(data_dir);
    config.compute_threads = args::get(compute_threads);
    config.storage_io_threads = args::get(storage_io_threads);
    config.thrift_io_threads = args::get(thrift_io_threads);
    config.bolt_io_threads = args::get(bolt_io_threads);
    config.wt_cache_size_mb = args::get(wt_cache_size_mb);
    config.wt_evict_threads_max = args::get(wt_evict_threads_max);
    config.wt_txn_sync = args::get(wt_txn_sync);
    config.wt_verbose = args::get(wt_verbose);
    return config;
}

int main(int argc, char* argv[]) {
    auto config = parseArgs(argc, argv);
    int folly_argc = 1;
    folly::Init init(&folly_argc, &argv);

    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [thread %t] [%l] %v");

    if (config.compute_threads <= 0 || config.storage_io_threads <= 0 || config.thrift_io_threads <= 0 ||
        config.bolt_io_threads <= 0) {
        spdlog::error("--compute-threads/--storage-io-threads/--thrift-io-threads/--bolt-io-threads must be positive");
        return 1;
    }
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
    if (!config.wt_verbose.empty())
        data_wt_config += ",verbose=[" + config.wt_verbose + "]";

    spdlog::info("Starting EuGraph server...");
    spdlog::info("  Thrift RPC port: {}", config.thrift_port);
    spdlog::info("  Data dir: {}", config.data_dir);
    spdlog::info("  WiredTiger data config: {}", data_wt_config);

    auto graph_manager = std::make_shared<GraphManager>();
    if (!graph_manager->init(config.data_dir, config.storage_io_threads, config.compute_threads,
                             GraphManager::kDefaultCheckpointIntervalSec, data_wt_config)) {
        spdlog::error("Failed to initialize graph manager");
        return 1;
    }

    auto graph_service = std::make_shared<service::GraphService>(*graph_manager);
    auto handler = std::make_shared<service::thrift::EuGraphHandler>(*graph_service);

    auto server = std::make_shared<apache::thrift::ThriftServer>();
    server->setPort(config.thrift_port);
    server->setAllowPlaintextOnLoopback(true);
    server->setInterface(handler);

    server->setThreadManagerType(apache::thrift::ThriftServer::ThreadManagerType::SIMPLE);
    server->setMaxFinishedDebugPayloadsPerWorker(0);

    auto ioPool = std::make_shared<folly::IOThreadPoolExecutor>(
        config.thrift_io_threads, std::make_shared<folly::NamedThreadFactory>("ThriftIO"));
    server->setIOThreadPool(ioPool);
    server->setThreadManagerFromExecutor(ioPool.get());
    spdlog::info("  Using IO thread pool ({} threads) as handler executor", config.thrift_io_threads);

    spdlog::info("EuGraph server initialized successfully");
    spdlog::info("Listening on Thrift RPC port {}...", config.thrift_port);

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
