#include "program/loader/csv_loader.hpp"
#include "program/shell/rpc_client.hpp"

#include <folly/init/Init.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

static void printUsage() {
    fmt::print("Usage: eugraph-loader --host <host> --port <port> --data-dir <path> [options]\n");
    fmt::print("  --host                Server address (default: 127.0.0.1)\n");
    fmt::print("  --port                Server port (default: 9090)\n");
    fmt::print("  --data-dir            Path to CSV data directory\n");
    fmt::print("  --nodes=Label[:Label...]=file    Explicit vertex file mapping (repeatable)\n");
    fmt::print("  --relationships=TYPE=file        Explicit edge file mapping (repeatable)\n");
    fmt::print("  --delimiter           CSV delimiter (default: '|'; only '|' supported)\n");
    fmt::print("  --batch-size          Records per RPC batch (default: 500)\n");
    fmt::print("  --eventbase-threads   Number of EventBase/RPC client threads (default: 1)\n");
    fmt::print("  --concurrency         Max parallel CSV file loading tasks (default: 1)\n");
    fmt::print("  --loader-concurrency  Alias of --concurrency\n");
}

static std::string resolvePath(const std::string& data_dir, const std::string& file) {
    std::filesystem::path p(file);
    if (p.is_absolute())
        return file;
    return (std::filesystem::path(data_dir) / p).string();
}

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    int port = 9090;
    std::string data_dir;
    int batch_size = 500;
    int eventbase_threads = 1;
    int concurrency = 1;
    std::vector<std::string> node_specs;
    std::vector<std::string> rel_specs;
    std::string delimiter = "|";

    auto take_value = [&](int& i, const std::string& arg) -> std::string {
        if (arg.find('=') != std::string::npos) {
            return arg.substr(arg.find('=') + 1);
        }
        if (i + 1 < argc) {
            return argv[++i];
        }
        return "";
    };

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg.rfind("--host", 0) == 0) {
            host = take_value(i, arg);
        } else if (arg.rfind("--port", 0) == 0) {
            port = std::stoi(take_value(i, arg));
        } else if (arg.rfind("--data-dir", 0) == 0) {
            data_dir = take_value(i, arg);
        } else if (arg.rfind("--batch-size", 0) == 0) {
            batch_size = std::stoi(take_value(i, arg));
        } else if (arg.rfind("--eventbase-threads", 0) == 0 || arg.rfind("--rpc-threads", 0) == 0) {
            eventbase_threads = std::stoi(take_value(i, arg));
        } else if (arg.rfind("--concurrency", 0) == 0 || arg.rfind("--loader-concurrency", 0) == 0) {
            concurrency = std::stoi(take_value(i, arg));
        } else if (arg.rfind("--nodes", 0) == 0) {
            node_specs.push_back(take_value(i, arg));
        } else if (arg.rfind("--relationships", 0) == 0) {
            rel_specs.push_back(take_value(i, arg));
        } else if (arg.rfind("--delimiter", 0) == 0) {
            delimiter = take_value(i, arg);
        } else if (arg == "--help") {
            printUsage();
            return 0;
        }
    }

    if (data_dir.empty()) {
        fmt::print(stderr, "Error: --data-dir is required\n");
        printUsage();
        return 1;
    }
    if (delimiter != "|") {
        fmt::print(stderr, "Error: only '|' delimiter is currently supported\n");
        return 1;
    }
    if (batch_size <= 0 || eventbase_threads <= 0 || concurrency <= 0) {
        fmt::print(stderr, "Error: --batch-size/--eventbase-threads/--concurrency must be positive\n");
        return 1;
    }

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
    spdlog::info("[loader] {} labels, {} edge types", label_schemas.size(), edge_schemas.size());

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
    auto id_maps = eugraph::loader::loadVertices(clients, vertex_files, label_schemas, batch_size, concurrency);
    spdlog::info("[loader] Vertex loading complete. {} groups in ID map", id_maps.group_id_map.size());

    spdlog::info("[loader] Creating unique indexes on ID properties...");
    eugraph::loader::createUniqueIdIndexes(client, label_schemas);

    spdlog::info("[loader] Loading edge data...");
    eugraph::loader::loadEdges(clients, edge_files, edge_schemas, id_maps, batch_size, concurrency);
    spdlog::info("[loader] Edge loading complete.");

    spdlog::info("[loader] All data loaded successfully.");
    return 0;
}
