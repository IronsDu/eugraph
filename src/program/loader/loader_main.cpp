#include "program/loader/csv_loader.hpp"
#include "program/shell/rpc_client.hpp"

#include <args.hxx>

#include <folly/init/Init.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

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
    args::ValueFlag<std::string> delimiter_flag(parser, "char", "CSV delimiter: | , ; or \\t (default |)",
                                                {"delimiter"}, "|");
    args::ValueFlag<int> batch_size_flag(parser, "n", "Records per RPC batch", {"batch-size"}, 500);
    args::ValueFlag<int> rpc_connections_flag(parser, "n", "Number of concurrent RPC connections", {"rpc-connections"},
                                              1);
    args::ValueFlag<int> parallel_files_flag(parser, "n", "Max number of CSV files loaded in parallel",
                                             {"parallel-files"}, 1);
    args::ValueFlagList<std::string> pk_flag(
        parser, "label=prop[,prop...]", "Override a label's primary key (property names, ordered; repeatable)", {"pk"});
    args::ValueFlagList<std::string> types_flag(parser, "label=prop:TYPE[,prop:TYPE...]",
                                                "Override/declare property types (keys are property names; repeatable)",
                                                {"types"});
    args::ValueFlag<std::string> date_format_flag(
        parser, "fmt", "Override schema date_format: epoch_ms|epoch_s|epoch_us|epoch_ns|iso", {"date-format"});
    args::Flag no_strict_flag(parser, "no-schema-strict",
                              "Infer types for columns not declared in the schema (default: error)",
                              {"no-schema-strict"});
    args::Flag skip_undeclared_flag(
        parser, "skip-undeclared",
        "Do not fail when the data dir contains CSV files that are not declared in the schema",
        {"skip-undeclared-check"});

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
    const std::string delimiter_raw = args::get(delimiter_flag);
    const int batch_size = args::get(batch_size_flag);
    const int rpc_connections = args::get(rpc_connections_flag);
    const int parallel_files = args::get(parallel_files_flag);
    const bool skip_undeclared = args::get(skip_undeclared_flag);
    const bool strict_types = !args::get(no_strict_flag);
    const std::vector<std::string> pk_overrides = args::get(pk_flag);
    const std::vector<std::string> types_overrides = args::get(types_flag);
    const std::string date_format_override = args::get(date_format_flag);

    if (schema_path.empty() || data_dir.empty()) {
        std::cerr << "Error: --schema and --data-dir are required (the schema file is the only declaration entry "
                     "point)\n";
        std::cerr << parser;
        return 1;
    }
    eugraph::loader::CsvDialect dialect;
    if (!eugraph::loader::parseDelimiter(delimiter_raw, dialect.delimiter)) {
        std::cerr << "Error: unsupported --delimiter '" << delimiter_raw << "' (expected | , ; or \\t)\n";
        return 1;
    }
    if (batch_size <= 0 || rpc_connections <= 0 || parallel_files <= 0) {
        std::cerr << "Error: --batch-size/--rpc-connections/--parallel-files must be positive\n";
        return 1;
    }

    // folly::Init only needs program name; our custom flags confuse gflags
    int folly_argc = 1;
    folly::Init init(&folly_argc, &argv);

    spdlog::set_level(spdlog::level::info);

    // CLI 覆盖：key=value，value 内以 ',' 分隔多项；--types 的项形如 prop:TYPE
    eugraph::loader::CliOverrides overrides;
    if (!date_format_override.empty())
        overrides.date_format = date_format_override;
    auto split = [](const std::string& s, char sep) {
        std::vector<std::string> out;
        std::string cur;
        std::stringstream ss(s);
        while (std::getline(ss, cur, sep))
            if (!cur.empty())
                out.push_back(cur);
        return out;
    };
    for (const auto& spec : pk_overrides) {
        auto eq = spec.find('=');
        if (eq == std::string::npos || eq == 0 || eq + 1 >= spec.size()) {
            std::cerr << "Error: --pk expects label=prop[,prop...], got '" << spec << "'\n";
            return 1;
        }
        overrides.pk[spec.substr(0, eq)] = split(spec.substr(eq + 1), ',');
    }
    for (const auto& spec : types_overrides) {
        auto eq = spec.find('=');
        if (eq == std::string::npos || eq == 0 || eq + 1 >= spec.size()) {
            std::cerr << "Error: --types expects label=prop:TYPE[,prop:TYPE...], got '" << spec << "'\n";
            return 1;
        }
        const std::string label = spec.substr(0, eq);
        for (const auto& item : split(spec.substr(eq + 1), ',')) {
            auto colon = item.rfind(':');
            if (colon == std::string::npos || colon == 0 || colon + 1 >= item.size()) {
                std::cerr << "Error: --types entry expects prop:TYPE, got '" << item << "'\n";
                return 1;
            }
            overrides.types[label][item.substr(0, colon)] = item.substr(colon + 1);
        }
    }

    eugraph::loader::SchemaConfig config;
    try {
        config = eugraph::loader::loadSchemaConfig(schema_path, data_dir, dialect, strict_types, &overrides);
    } catch (const std::exception& e) {
        spdlog::error("[loader] schema error: {}", e.what());
        return 1;
    }

    if (!skip_undeclared) {
        auto undeclared = eugraph::loader::findUndeclaredCsvFiles(config);
        if (!undeclared.empty()) {
            std::string list;
            for (const auto& f : undeclared)
                list += "\n  " + f;
            spdlog::error("[loader] {} CSV file(s) under {} are not declared in the schema (add them or pass "
                          "--skip-undeclared-check):{}",
                          undeclared.size(), data_dir, list);
            return 1;
        }
    }

    eugraph::shell::EuGraphRpcClient client(host, port);
    if (!client.connect()) {
        spdlog::error("[loader] Failed to connect to server at {}:{}", host, port);
        return 1;
    }

    std::vector<std::unique_ptr<eugraph::shell::EuGraphRpcClient>> extra_clients;
    std::vector<eugraph::shell::EuGraphRpcClient*> clients;
    clients.reserve(static_cast<size_t>(rpc_connections));
    clients.push_back(&client);
    for (int i = 1; i < rpc_connections; i++) {
        auto extra = std::make_unique<eugraph::shell::EuGraphRpcClient>(host, port);
        if (!extra->connect()) {
            spdlog::error("[loader] Failed to open extra RPC connection to {}:{}", host, port);
            return 1;
        }
        extra_clients.push_back(std::move(extra));
        clients.push_back(extra_clients.back().get());
    }

    spdlog::info("[loader] Connected to {}:{} with {} RPC connection(s), parallel-files={}", host, port, clients.size(),
                 parallel_files);

    try {
        spdlog::info("[loader] Creating labels...");
        eugraph::loader::createLabels(client, config);
        spdlog::info("[loader] Creating edge labels...");
        eugraph::loader::createEdgeLabels(client, config);

        // 先建主键唯一索引：批量写入会在同一事务内维护它，装载期即可用于解析边端点
        spdlog::info("[loader] Creating primary key unique indexes...");
        eugraph::loader::createPrimaryKeyIndexes(client, config);

        spdlog::info("[loader] Loading vertex data...");
        auto [vertices_written, vertices_dup] =
            eugraph::loader::loadVertices(clients, config, dialect, batch_size, parallel_files);
        spdlog::info("[loader] Vertices: {} written, {} skipped (duplicate primary key)", vertices_written,
                     vertices_dup);

        spdlog::info("[loader] Loading edge data...");
        auto [edges_written, edges_skipped] =
            eugraph::loader::loadEdges(clients, config, dialect, batch_size, parallel_files);
        spdlog::info("[loader] Edges: {} written, {} skipped (endpoint unresolved)", edges_written, edges_skipped);
    } catch (const std::exception& e) {
        spdlog::error("[loader] load failed: {}", e.what());
        return 1;
    }

    spdlog::info("[loader] All data loaded successfully.");
    return 0;
}
