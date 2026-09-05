#include "program/loader/csv_loader.hpp"
#include "program/shell/rpc_client.hpp"

#include <args.hxx>

#include <folly/init/Init.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    // Parse our args before folly::Init to avoid gflags conflicts
    std::string host = "127.0.0.1";
    int port = 9090;
    std::string data_dir;
    int batch_size = 500;
    int eventbase_threads = 1;
    int concurrency = 1;

    args::ArgumentParser parser("EuGraph CSV loader.");
    args::HelpFlag help(parser, "help", "Show this help menu", {"help"});
    args::ValueFlag<std::string> host_flag(parser, "host", "Server address (default: 127.0.0.1)", {"host"}, host);
    args::ValueFlag<int> port_flag(parser, "port", "Server port (default: 9090)", {"port"}, port);
    args::ValueFlag<std::string> data_dir_flag(parser, "path", "Path to CSV data directory", {"data-dir"},
                                               args::Options::Required);
    args::ValueFlag<int> batch_size_flag(parser, "n", "Records per RPC batch (default: 500)", {"batch-size"},
                                         batch_size);
    args::ValueFlag<int> eventbase_threads_flag(parser, "n", "Number of EventBase/RPC client threads (default: 1)",
                                                {"eventbase-threads", "rpc-threads"}, eventbase_threads);
    args::ValueFlag<int> concurrency_flag(parser, "n", "Max parallel CSV file loading tasks (default: 1)",
                                          {"concurrency", "loader-concurrency"}, concurrency);

    try {
        parser.ParseCLI(argc, argv);
    } catch (const args::Help&) {
        std::cout << parser;
        return 0;
    } catch (const args::ParseError& e) {
        std::cerr << e.what() << '\n';
        std::cerr << parser;
        return 1;
    } catch (const args::ValidationError& e) {
        std::cerr << e.what() << '\n';
        std::cerr << parser;
        return 1;
    }

    host = args::get(host_flag);
    port = args::get(port_flag);
    data_dir = args::get(data_dir_flag);
    batch_size = args::get(batch_size_flag);
    eventbase_threads = args::get(eventbase_threads_flag);
    concurrency = args::get(concurrency_flag);

    if (data_dir.empty()) {
        std::cerr << "Error: --data-dir must not be empty\n";
        std::cerr << parser;
        return 1;
    }
    if (batch_size <= 0 || eventbase_threads <= 0 || concurrency <= 0) {
        std::cerr << "Error: --batch-size/--eventbase-threads/--concurrency must be positive\n";
        std::cerr << parser;
        return 1;
    }

    // folly::Init only needs program name; our custom flags confuse gflags
    int folly_argc = 1;
    folly::Init init(&folly_argc, &argv);

    spdlog::set_level(spdlog::level::info);

    // Step 1: Scan and classify CSV files
    spdlog::info("[loader] Scanning CSV files in: {}", data_dir);
    auto files = eugraph::loader::scanCsvFiles(data_dir);

    std::vector<eugraph::loader::CsvFileInfo> vertex_files, edge_files;
    for (const auto& f : files) {
        if (f.is_vertex) {
            vertex_files.push_back(f);
        } else {
            edge_files.push_back(f);
        }
    }
    spdlog::info("[loader] Found {} vertex files, {} edge files", vertex_files.size(), edge_files.size());

    // Step 2: Build schemas
    auto label_schemas = eugraph::loader::buildLabelSchemas(vertex_files);
    auto edge_schemas = eugraph::loader::buildEdgeTypeSchemas(edge_files);
    spdlog::info("[loader] {} labels, {} edge types", label_schemas.size(), edge_schemas.size());

    // Step 3: Connect to server. The first client is used for DDL and indexes;
    // additional clients are used only for parallel data loading.
    eugraph::shell::EuGraphRpcClient client(host, port);
    if (!client.connect()) {
        spdlog::error("[loader] Failed to connect to server at {}:{}", host, port);
        return 1;
    }

    std::vector<std::unique_ptr<eugraph::shell::EuGraphRpcClient>> extra_clients;
    std::vector<eugraph::shell::EuGraphRpcClient*> clients;
    clients.reserve(eventbase_threads);
    clients.push_back(&client);
    for (int i = 1; i < eventbase_threads; i++) {
        auto extra = std::make_unique<eugraph::shell::EuGraphRpcClient>(host, port);
        if (!extra->connect()) {
            spdlog::error("[loader] Failed to connect extra EventBase client at {}:{}", host, port);
            return 1;
        }
        extra_clients.push_back(std::move(extra));
        clients.push_back(extra_clients.back().get());
    }

    spdlog::info("[loader] Connected to {}:{} with {} EventBase client(s), concurrency={}", host, port, clients.size(),
                 concurrency);

    // Step 4: Create labels and edge labels
    spdlog::info("[loader] Creating labels...");
    eugraph::loader::createLabels(client, label_schemas);
    spdlog::info("[loader] Creating edge labels...");
    eugraph::loader::createEdgeLabels(client, edge_schemas);

    // Step 5: Load vertex data
    spdlog::info("[loader] Loading vertex data...");
    auto id_map = eugraph::loader::loadVertices(clients, vertex_files, label_schemas, batch_size, concurrency);
    spdlog::info("[loader] Vertex loading complete. {} labels in ID map", id_map.size());

    // Step 5.5: Create unique indexes on ID properties
    spdlog::info("[loader] Creating unique indexes on ID properties...");
    eugraph::loader::createUniqueIdIndexes(client, label_schemas);

    // Step 6: Load edge data
    spdlog::info("[loader] Loading edge data...");
    eugraph::loader::loadEdges(clients, edge_files, edge_schemas, id_map, batch_size, concurrency);
    spdlog::info("[loader] Edge loading complete.");

    spdlog::info("[loader] All data loaded successfully.");
    return 0;
}
