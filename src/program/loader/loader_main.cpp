#include "program/loader/csv_loader.hpp"
#include "program/shell/rpc_client.hpp"

#include <args.hxx>

#include <folly/init/Init.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// Resolve a --nodes/--relationships file path against --data-dir.
static std::string resolvePath(const std::string& data_dir, const std::string& file) {
    std::filesystem::path p(file);
    if (p.is_absolute()) {
        return file;
    }
    return (std::filesystem::path(data_dir) / p).string();
}

int main(int argc, char* argv[]) {
    // Parse our args before folly::Init to avoid gflags conflicts
    args::ArgumentParser parser("EuGraph CSV loader.");
    args::HelpFlag help(parser, "help", "Show this help menu", {"help"});
    args::ValueFlag<std::string> host_flag(parser, "host", "Server address (default: 127.0.0.1)", {"host"},
                                           "127.0.0.1");
    args::ValueFlag<int> port_flag(parser, "port", "Server port (default: 9090)", {"port"}, 9090);
    args::ValueFlag<std::string> data_dir_flag(parser, "path", "Path to CSV data directory", {"data-dir"},
                                               args::Options::Required);
    args::ValueFlagList<std::string> nodes_flag(
        parser, "spec", "Explicit vertex file mapping: Label[:Label...]=file (repeatable)", {"nodes"});
    args::ValueFlagList<std::string> relationships_flag(
        parser, "spec", "Explicit edge file mapping: TYPE=file (repeatable)", {"relationships"});
    args::ValueFlag<std::string> delimiter_flag(parser, "char", "CSV delimiter (default: '|'; only '|' supported)",
                                                {"delimiter"}, "|");
    args::ValueFlag<int> batch_size_flag(parser, "n", "Records per RPC batch (default: 500)", {"batch-size"}, 500);
    args::ValueFlag<int> eventbase_threads_flag(parser, "n", "Number of EventBase/RPC client threads (default: 1)",
                                                {"eventbase-threads", "rpc-threads"}, 1);
    args::ValueFlag<int> concurrency_flag(parser, "n", "Max parallel CSV file loading tasks (default: 1)",
                                          {"concurrency", "loader-concurrency"}, 1);

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

    const std::string host = args::get(host_flag);
    const int port = args::get(port_flag);
    const std::string data_dir = args::get(data_dir_flag);
    const std::vector<std::string> node_specs = args::get(nodes_flag);
    const std::vector<std::string> rel_specs = args::get(relationships_flag);
    const std::string delimiter = args::get(delimiter_flag);
    const int batch_size = args::get(batch_size_flag);
    const int eventbase_threads = args::get(eventbase_threads_flag);
    const int concurrency = args::get(concurrency_flag);

    if (data_dir.empty()) {
        std::cerr << "Error: --data-dir must not be empty\n";
        std::cerr << parser;
        return 1;
    }
    if (delimiter != "|") {
        std::cerr << "Error: only '|' delimiter is currently supported\n";
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

    std::vector<eugraph::loader::CsvFileInfo> vertex_files, edge_files;

    if (!node_specs.empty() || !rel_specs.empty()) {
        spdlog::info("[loader] CLI mode: {} node specs, {} relationship specs", node_specs.size(), rel_specs.size());
        for (const auto& spec : node_specs) {
            eugraph::loader::CsvFileInfo info;
            std::string error;
            if (!eugraph::loader::parseNodeSpec(spec, info, error)) {
                spdlog::error("[loader] {}", error);
                return 1;
            }
            info.path = resolvePath(data_dir, info.path.string());
            vertex_files.push_back(std::move(info));
        }
        for (const auto& spec : rel_specs) {
            eugraph::loader::CsvFileInfo info;
            std::string error;
            if (!eugraph::loader::parseRelationshipSpec(spec, info, error)) {
                spdlog::error("[loader] {}", error);
                return 1;
            }
            info.path = resolvePath(data_dir, info.path.string());
            edge_files.push_back(std::move(info));
        }
    } else {
        spdlog::info("[loader] Scanning CSV files in: {}", data_dir);
        auto files = eugraph::loader::scanCsvFiles(data_dir);
        for (const auto& f : files) {
            if (f.is_vertex) {
                vertex_files.push_back(f);
            } else {
                edge_files.push_back(f);
            }
        }
    }

    spdlog::info("[loader] Found {} vertex files, {} edge files", vertex_files.size(), edge_files.size());

    auto label_schemas = eugraph::loader::buildLabelSchemas(vertex_files);
    auto edge_schemas = eugraph::loader::buildEdgeTypeSchemas(edge_files);
    auto merged_label_props = eugraph::loader::buildMergedLabelProperties(label_schemas);
    spdlog::info("[loader] {} vertex files, {} edge types", label_schemas.size(), edge_schemas.size());

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

    spdlog::info("[loader] Creating labels...");
    eugraph::loader::createLabels(client, label_schemas);
    spdlog::info("[loader] Creating edge labels...");
    eugraph::loader::createEdgeLabels(client, edge_schemas);

    spdlog::info("[loader] Loading vertex data...");
    auto id_maps = eugraph::loader::loadVertices(clients, vertex_files, label_schemas, merged_label_props, batch_size,
                                                 concurrency);
    spdlog::info("[loader] Vertex loading complete. {} groups in ID map", id_maps.group_id_map.size());

    spdlog::info("[loader] Creating unique indexes on ID properties...");
    eugraph::loader::createUniqueIdIndexes(client, label_schemas);

    spdlog::info("[loader] Loading edge data...");
    eugraph::loader::loadEdges(clients, edge_files, edge_schemas, id_maps, batch_size, concurrency);
    spdlog::info("[loader] Edge loading complete.");

    spdlog::info("[loader] All data loaded successfully.");
    return 0;
}
