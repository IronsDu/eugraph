#include "gen-cpp2/EuGraphService.h"
#include "program/server/eugraph_handler.hpp"
#include "storage/graph_manager.hpp"

#include <thrift/lib/cpp2/server/ThriftServer.h>

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
    int folly_argc = 1;
    folly::Init init(&folly_argc, &argv);

    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [thread %t] [%l] %v");

    spdlog::info("Starting EuGraph server...");
    spdlog::info("  Port: {}", config.port);
    spdlog::info("  Data dir: {}", config.data_dir);

    auto graph_manager = std::make_shared<GraphManager>();
    if (!graph_manager->init(config.data_dir, config.io_threads, config.compute_threads)) {
        spdlog::error("Failed to initialize graph manager");
        return 1;
    }

    auto handler = std::make_shared<server::EuGraphHandler>(*graph_manager);

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

    server->serve();

    spdlog::info("Shutting down...");
    graph_manager->shutdown();
    spdlog::info("Bye.");

    return 0;
}
