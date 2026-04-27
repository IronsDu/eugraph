#include "loader/csv_loader.hpp"
#include "shell/rpc_client.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdlib>
#include <string>

static void printUsage() {
    fmt::print("Usage: eugraph-loader --host <host> --port <port> --data-dir <path> [--batch-size <N>]\n");
    fmt::print("  --host       Server address (default: 127.0.0.1)\n");
    fmt::print("  --port       Server port (default: 9090)\n");
    fmt::print("  --data-dir   Path to CSV data directory\n");
    fmt::print("  --batch-size Records per RPC batch (default: 500)\n");
}

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    int port = 9090;
    std::string data_dir;
    int batch_size = 500;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--data-dir" && i + 1 < argc) {
            data_dir = argv[++i];
        } else if (arg == "--batch-size" && i + 1 < argc) {
            batch_size = std::stoi(argv[++i]);
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

    // Step 3: Connect to server
    eugraph::shell::EuGraphRpcClient client(host, port);
    if (!client.connect()) {
        spdlog::error("[loader] Failed to connect to server at {}:{}", host, port);
        return 1;
    }
    spdlog::info("[loader] Connected to {}:{}", host, port);

    // Step 4: Create labels and edge labels
    spdlog::info("[loader] Creating labels...");
    eugraph::loader::createLabels(client, label_schemas);
    spdlog::info("[loader] Creating edge labels...");
    eugraph::loader::createEdgeLabels(client, edge_schemas);

    // Step 5: Load vertex data
    spdlog::info("[loader] Loading vertex data...");
    auto id_map = eugraph::loader::loadVertices(client, vertex_files, label_schemas, batch_size);
    spdlog::info("[loader] Vertex loading complete. {} labels in ID map", id_map.size());

    // Step 6: Load edge data
    spdlog::info("[loader] Loading edge data...");
    eugraph::loader::loadEdges(client, edge_files, edge_schemas, id_map, batch_size);
    spdlog::info("[loader] Edge loading complete.");

    spdlog::info("[loader] All data loaded successfully.");
    return 0;
}
