#include "program/loader/csv_loader.hpp"
#include "program/loader/loader_progress.hpp"
#include "program/shell/rpc_client.hpp"

#include <args.hxx>

#include <folly/init/Init.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

eugraph::loader::ProgressMode parseProgressMode(const std::string& raw) {
    if (raw == "auto")
        return eugraph::loader::ProgressMode::AUTO;
    if (raw == "plain")
        return eugraph::loader::ProgressMode::PLAIN;
    if (raw == "off")
        return eugraph::loader::ProgressMode::OFF;
    throw std::runtime_error("--progress must be auto|plain|off, got '" + raw + "'");
}

std::string formatElapsed(std::chrono::steady_clock::duration elapsed) {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(1);
    oss << static_cast<double>(ms) / 1000.0 << "s";
    return oss.str();
}

} // namespace

int main(int argc, char* argv[]) {
    // Parse our args before folly::Init to avoid gflags conflicts
    args::ArgumentParser parser("EuGraph CSV loader (schema-driven).");
    parser.helpParams.addDefault = true;
    args::HelpFlag help(parser, "help", "Show this help menu", {"help"});
    args::ValueFlag<std::string> host_flag(parser, "host", "Server address", {"host"}, "127.0.0.1");
    args::ValueFlag<int> port_flag(parser, "port", "Server port", {"port"}, 9090);
    args::ValueFlag<std::string> schema_flag(
        parser, "path", "Schema config (JSON). Required: the only declaration entry point", {"schema"});
    args::ValueFlag<std::string> data_dir_flag(
        parser, "path", "Data directory; schema 'file' paths are resolved against it. Required", {"data-dir"});
    args::ValueFlag<std::string> graph_flag(parser, "name", "Target graph; created when missing (default: default)",
                                            {"graph"}, "default");
    args::ValueFlag<std::string> progress_flag(parser, "mode", "Progress display: auto (bar on a TTY) | plain | off",
                                               {"progress"}, "auto");
    args::ValueFlag<int> batch_size_flag(parser, "n", "Records per RPC batch", {"batch-size"}, 500);
    args::ValueFlag<int> rpc_connections_flag(parser, "n", "Number of concurrent RPC connections", {"rpc-connections"},
                                              1);
    args::ValueFlag<int> parallel_files_flag(parser, "n", "Max number of CSV files loaded in parallel",
                                             {"parallel-files"}, 1);
    args::Flag no_strict_flag(parser, "no-schema-strict",
                              "Infer types for columns not declared in the schema (default: error)",
                              {"no-schema-strict"});

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
    const std::string schema_path = args::get(schema_flag);
    const std::string data_dir = args::get(data_dir_flag);
    const std::string graph_name = args::get(graph_flag);
    const int batch_size = args::get(batch_size_flag);
    const int rpc_connections = args::get(rpc_connections_flag);
    const int parallel_files = args::get(parallel_files_flag);
    const bool strict_types = !args::get(no_strict_flag);

    if (schema_path.empty() || data_dir.empty()) {
        std::cerr << "Error: --schema and --data-dir are required (the schema file is the only declaration entry "
                     "point)\n";
        std::cerr << parser;
        return 1;
    }
    if (batch_size <= 0 || rpc_connections <= 0 || parallel_files <= 0) {
        std::cerr << "Error: --batch-size/--rpc-connections/--parallel-files must be positive\n";
        return 1;
    }

    eugraph::loader::ProgressMode progress_mode;
    try {
        progress_mode = parseProgressMode(args::get(progress_flag));
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    // folly::Init only needs program name; our custom flags confuse gflags
    int folly_argc = 1;
    folly::Init init(&folly_argc, &argv);

    spdlog::set_level(spdlog::level::info);

    // 先连服务器：端口写错 / 服务没起时立刻失败，不要等解析完 schema、扫完数据目录才发现。
    //
    // 注意 `EuGraphRpcClient::connect()` 只建 channel（惰性连接），返回 true 不代表对端可达；
    // 因此这里用一次真实 RPC（listGraphs）做握手验证。
    auto reachable = [&](eugraph::shell::EuGraphRpcClient& c, int index) {
        try {
            (void)c.listGraphs();
            return true;
        } catch (const std::exception& e) {
            spdlog::error("[loader] Cannot reach server {}:{} (connection {} of {}): {}", host, port, index,
                          rpc_connections, e.what());
            return false;
        }
    };

    eugraph::shell::EuGraphRpcClient client(host, port);
    if (!client.connect() || !reachable(client, 1))
        return 1;

    std::vector<std::unique_ptr<eugraph::shell::EuGraphRpcClient>> extra_clients;
    std::vector<eugraph::shell::EuGraphRpcClient*> clients;
    clients.reserve(static_cast<size_t>(rpc_connections));
    clients.push_back(&client);
    for (int i = 1; i < rpc_connections; i++) {
        auto extra = std::make_unique<eugraph::shell::EuGraphRpcClient>(host, port);
        if (!extra->connect() || !reachable(*extra, i + 1))
            return 1;
        extra_clients.push_back(std::move(extra));
        clients.push_back(extra_clients.back().get());
    }
    spdlog::info("[loader] Connected to {}:{} with {} RPC connection(s)", host, port, clients.size());

    eugraph::loader::SchemaConfig config;
    try {
        config = eugraph::loader::loadSchemaConfig(schema_path, data_dir, strict_types);
    } catch (const std::exception& e) {
        spdlog::error("[loader] schema error: {}", e.what());
        return 1;
    }
    // 目标图是运行期去向（不来自 schema），由 --graph 决定
    config.graph = graph_name;

    if (config.undeclared_files == "error") {
        auto undeclared = eugraph::loader::findUndeclaredCsvFiles(config);
        if (!undeclared.empty()) {
            std::string list;
            for (const auto& f : undeclared)
                list += "\n  " + f;
            spdlog::error("[loader] {} CSV file(s) under {} are not declared in the schema (declare them, add them "
                          "to 'ignore', or set \"undeclared_files\": \"ignore\"):{}",
                          undeclared.size(), data_dir, list);
            return 1;
        }
    }

    spdlog::info("[loader] graph='{}', parallel-files={}, batch-size={}", config.graph, parallel_files, batch_size);

    const auto started = std::chrono::steady_clock::now();
    try {
        eugraph::loader::ProgressReporter progress(progress_mode, std::cerr);
        eugraph::loader::LoadOptions options;
        options.batch_size = batch_size;
        options.parallel_files = parallel_files;
        options.progress = &progress;
        const auto result = eugraph::loader::loadAll(clients, config, options);

        spdlog::info("[loader] Vertices: {} written, {} skipped (duplicate primary key)", result.vertices_written,
                     result.vertices_duplicate);
        spdlog::info("[loader] Edges: {} written, {} skipped (endpoint unresolved)", result.edges_written,
                     result.edges_skipped);
        spdlog::info("[loader] All data loaded into graph '{}' in {}", config.graph,
                     formatElapsed(std::chrono::steady_clock::now() - started));
    } catch (const std::exception& e) {
        spdlog::error("[loader] load failed: {}", e.what());
        return 1;
    }

    return 0;
}
