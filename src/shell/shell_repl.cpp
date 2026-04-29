#include "shell/shell_repl.hpp"
#include "server/eugraph_handler.hpp"
#include "shell/rpc_client.hpp"

#include "compute_service/executor/query_executor.hpp"
#include "gen-cpp2/eugraph_types.h"
#include "storage/data/async_graph_data_store.hpp"
#include "storage/data/sync_graph_data_store.hpp"
#include "storage/io_scheduler.hpp"
#include "storage/meta/async_graph_meta_store.hpp"
#include "storage/meta/sync_graph_meta_store.hpp"

#include <linenoise.h>

#include <folly/coro/BlockingWait.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace eugraph {
namespace shell {

using namespace eugraph::thrift;

// ==================== Value formatting ====================

std::string resultValueToString(const ResultValue& rv) {
    switch (rv.getType()) {
    case ResultValue::Type::__EMPTY__:
        return "null";
    case ResultValue::Type::bool_val:
        return rv.get_bool_val() ? "true" : "false";
    case ResultValue::Type::int_val:
        return std::to_string(rv.get_int_val());
    case ResultValue::Type::double_val:
        return std::to_string(rv.get_double_val());
    case ResultValue::Type::string_val:
        return rv.get_string_val();
    case ResultValue::Type::vertex_json:
        return rv.get_vertex_json();
    case ResultValue::Type::edge_json:
        return rv.get_edge_json();
    }
    return "?";
}

// Format a compute::Value (from executor) to display string.
std::string valueToString(const Value& val) {
    if (std::holds_alternative<std::monostate>(val)) {
        return "null";
    } else if (std::holds_alternative<bool>(val)) {
        return std::get<bool>(val) ? "true" : "false";
    } else if (std::holds_alternative<int64_t>(val)) {
        return std::to_string(std::get<int64_t>(val));
    } else if (std::holds_alternative<double>(val)) {
        return std::to_string(std::get<double>(val));
    } else if (std::holds_alternative<std::string>(val)) {
        return std::get<std::string>(val);
    } else if (std::holds_alternative<VertexValue>(val)) {
        return "v[" + std::to_string(std::get<VertexValue>(val).id) + "]";
    } else if (std::holds_alternative<EdgeValue>(val)) {
        auto& e = std::get<EdgeValue>(val);
        return "e[" + std::to_string(e.id) + "]";
    } else if (std::holds_alternative<ListValue>(val)) {
        return "[...]";
    }
    return "?";
}

std::string formatTable(const std::vector<std::string>& columns, const std::vector<std::vector<std::string>>& rows) {
    if (columns.empty()) {
        return "(no results)\n";
    }

    std::vector<size_t> widths(columns.size());
    for (size_t i = 0; i < columns.size(); i++) {
        widths[i] = columns[i].size();
    }
    for (const auto& row : rows) {
        for (size_t i = 0; i < row.size() && i < columns.size(); i++) {
            widths[i] = std::max(widths[i], row[i].size());
        }
    }

    auto formatRow = [&](const std::vector<std::string>& vals) {
        std::string line = "|";
        for (size_t i = 0; i < columns.size(); i++) {
            std::string val = (i < vals.size()) ? vals[i] : "";
            line += " " + val + std::string(widths[i] - val.size(), ' ') + " |";
        }
        return line;
    };

    auto separator = [&]() {
        std::string line = "+";
        for (size_t i = 0; i < columns.size(); i++) {
            line += std::string(widths[i] + 2, '-') + "+";
        }
        return line;
    };

    std::ostringstream os;
    os << separator() << "\n";
    os << formatRow(columns) << "\n";
    os << separator() << "\n";
    for (const auto& row : rows) {
        os << formatRow(row) << "\n";
    }
    os << separator() << "\n";

    return os.str();
}

std::string formatLabelCreated(const std::string& name, int id) {
    return "Label created: " + name + " (id=" + std::to_string(id) + ")\n";
}

std::string formatEdgeLabelCreated(const std::string& name, int id) {
    return "EdgeLabel created: " + name + " (id=" + std::to_string(id) + ")\n";
}

std::string formatLabelList(const std::vector<std::tuple<int, std::string, std::string>>& labels) {
    std::vector<std::string> cols = {"ID", "Name", "Properties"};
    std::vector<std::vector<std::string>> rows;
    for (const auto& [id, name, props] : labels) {
        rows.push_back({std::to_string(id), name, props});
    }
    return formatTable(cols, rows);
}

std::string formatEdgeLabelList(const std::vector<std::tuple<int, std::string, std::string, bool>>& labels) {
    std::vector<std::string> cols = {"ID", "Name", "Properties", "Directed"};
    std::vector<std::vector<std::string>> rows;
    for (const auto& [id, name, props, directed] : labels) {
        rows.push_back({std::to_string(id), name, props, directed ? "true" : "false"});
    }
    return formatTable(cols, rows);
}

// ==================== REPL helpers ====================

std::pair<std::string, std::string> parseCommand(const std::string& line) {
    std::string trimmed = line;
    size_t start = trimmed.find_first_not_of(" \t");
    if (start == std::string::npos)
        return {"", ""};
    trimmed = trimmed.substr(start);

    if (trimmed.empty() || trimmed[0] != ':') {
        return {"", line};
    }

    size_t space_pos = trimmed.find(' ');
    std::string cmd = (space_pos == std::string::npos) ? trimmed : trimmed.substr(0, space_pos);
    std::string args = (space_pos == std::string::npos) ? "" : trimmed.substr(space_pos + 1);

    size_t end = args.find_last_not_of(" \t");
    if (end != std::string::npos)
        args = args.substr(0, end + 1);

    return {cmd, args};
}

bool isCompleteCypher(const std::string& input) {
    if (input.empty())
        return false;
    size_t end = input.find_last_not_of(" \t\n\r");
    if (end == std::string::npos)
        return false;
    return input[end] == ';';
}

// ==================== Property type parsing ====================

thrift::PropertyType parsePropertyType(const std::string& type_str) {
    std::string upper = type_str;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    if (upper == "BOOL")
        return thrift::PropertyType::BOOL;
    if (upper == "INT64" || upper == "INT" || upper == "INTEGER")
        return thrift::PropertyType::INT64;
    if (upper == "DOUBLE" || upper == "FLOAT")
        return thrift::PropertyType::DOUBLE;
    return thrift::PropertyType::STRING;
}

std::vector<PropertyDefThrift> parsePropertyDefs(const std::string& args) {
    std::vector<PropertyDefThrift> props;
    std::istringstream iss(args);
    std::string token;
    while (iss >> token) {
        auto colon_pos = token.find(':');
        PropertyDefThrift def;
        if (colon_pos != std::string::npos) {
            def.name() = token.substr(0, colon_pos);
            def.type() = parsePropertyType(token.substr(colon_pos + 1));
        } else {
            def.name() = token;
            def.type() = thrift::PropertyType::STRING;
        }
        def.is_required() = false;
        props.push_back(std::move(def));
    }
    return props;
}

// ==================== REPL common logic ====================

// Common REPL loop for both embedded and RPC modes.
// The handler_func callback processes each command.
using CommandHandler =
    std::function<void(const std::string& cmd, const std::string& args, const std::string& accumulated)>;

static const char* kPrompt = "\033[32meugraph>\033[0m ";
static const char* kMultilinePrompt = "\033[36m......>\033[0m ";

static void completionCallback(const char* buf, linenoiseCompletions* lc) {
    if (buf[0] == ':') {
        const char* cmds[] = {
            ":help", ":exit", ":quit", ":create-label", ":create-edge-label", ":list-labels", ":list-edge-labels"};
        for (const auto* c : cmds) {
            if (strncmp(c, buf, strlen(buf)) == 0) {
                linenoiseAddCompletion(lc, c);
            }
        }
    }
}

static void runReplLoop(const std::string& mode_label, CommandHandler handler) {
    linenoiseSetCompletionCallback(completionCallback);
    linenoiseHistorySetMaxLen(256);

    std::string history_path = std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.eugraph_history";
    linenoiseHistoryLoad(history_path.c_str());

    std::cout << "EuGraph Shell (" << mode_label << " mode)" << std::endl;
    std::cout << "Type :help for available commands, :exit to quit." << std::endl;
    std::cout << std::endl;

    while (true) {
        std::string accumulated;

        while (true) {
            const char* prompt = accumulated.empty() ? kPrompt : kMultilinePrompt;
            char* line_raw = linenoise(prompt);
            if (!line_raw) {
                std::cout << std::endl;
                linenoiseHistorySave(history_path.c_str());
                return;
            }
            std::string line(line_raw);
            linenoiseFree(line_raw);

            accumulated = accumulated.empty() ? line : accumulated + "\n" + line;

            auto [cmd, args] = parseCommand(accumulated);

            if (!cmd.empty()) {
                break;
            }

            if (accumulated.find_first_not_of(" \t\n\r") == std::string::npos) {
                accumulated.clear();
                continue;
            }

            if (isCompleteCypher(accumulated)) {
                break;
            }
        }

        auto [cmd, args] = parseCommand(accumulated);

        if (cmd == ":exit" || cmd == ":quit") {
            linenoiseHistorySave(history_path.c_str());
            std::cout << "Bye!" << std::endl;
            return;
        } else {
            linenoiseHistoryAdd(accumulated.c_str());
            auto t0 = std::chrono::steady_clock::now();
            handler(cmd, args, accumulated);
            auto t1 = std::chrono::steady_clock::now();
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
            if (us > 1000) {
                std::cout << "  [" << (us / 1000) << " ms]" << std::endl;
            } else {
                std::cout << "  [" << us << " us]" << std::endl;
            }
        }
    }
}

// ==================== Embedded mode REPL ====================

static void runEmbeddedRepl(const ShellConfig& config) {
    // Initialize embedded database
    std::string db_path = config.data_dir;
    if (db_path.empty()) {
        db_path = "/tmp/eugraph-shell-" + std::to_string(getpid());
    }

    std::filesystem::create_directories(db_path + "/data");
    std::filesystem::create_directories(db_path + "/meta");

    auto sync_data = std::make_unique<SyncGraphDataStore>();
    if (!sync_data->open(db_path + "/data")) {
        std::cerr << "Error: Failed to open data store at " << db_path << "/data" << std::endl;
        return;
    }

    auto sync_meta = std::make_unique<SyncGraphMetaStore>();
    if (!sync_meta->open(db_path + "/meta")) {
        std::cerr << "Error: Failed to open meta store at " << db_path << "/meta" << std::endl;
        return;
    }

    auto io_scheduler = std::make_shared<IoScheduler>(2);
    auto async_data = std::make_unique<AsyncGraphDataStore>(*sync_data, *io_scheduler);

    auto async_meta = std::make_unique<AsyncGraphMetaStore>();
    auto opened = folly::coro::blockingWait(async_meta->open(*sync_meta, *io_scheduler));
    if (!opened) {
        std::cerr << "Error: Failed to initialize async meta store" << std::endl;
        return;
    }

    compute::QueryExecutor executor(*async_data, *async_meta, compute::QueryExecutor::Config{});
    server::EuGraphHandler handler(*async_data, *async_meta, executor);

    std::cout << "Database: " << db_path << std::endl;

    auto cmd_handler = [&handler, &executor](const std::string& cmd, const std::string& args,
                                             const std::string& accumulated) {
        if (cmd == ":help") {
            std::cout << "Available commands:" << std::endl;
            std::cout << "  :create-label <name> [prop1:type1 prop2:type2 ...]" << std::endl;
            std::cout << "  :create-edge-label <name> [prop1:type1 ...]" << std::endl;
            std::cout << "  :list-labels                    List all labels" << std::endl;
            std::cout << "  :list-edge-labels               List all edge labels" << std::endl;
            std::cout << "  :help                           Show this help" << std::endl;
            std::cout << "  :exit / :quit                   Exit shell" << std::endl;
            std::cout << std::endl;
            std::cout << "Any other input is treated as a Cypher query (end with ;)." << std::endl;
        } else if (cmd == ":create-label") {
            if (args.empty()) {
                std::cerr << "Usage: :create-label <name> [prop1:type1 ...]" << std::endl;
                return;
            }
            std::istringstream iss(args);
            std::string name;
            iss >> name;
            std::string rest;
            std::getline(iss, rest);
            auto props = parsePropertyDefs(rest);
            auto resp = folly::coro::blockingWait(
                handler.co_createLabel(std::make_unique<std::string>(name),
                                       std::make_unique<std::vector<PropertyDefThrift>>(std::move(props))));
            std::cout << formatLabelCreated(resp->name().value(), resp->id().value());
        } else if (cmd == ":create-edge-label") {
            if (args.empty()) {
                std::cerr << "Usage: :create-edge-label <name> [prop1:type1 ...]" << std::endl;
                return;
            }
            std::istringstream iss(args);
            std::string name;
            iss >> name;
            std::string rest;
            std::getline(iss, rest);
            auto props = parsePropertyDefs(rest);
            auto resp = folly::coro::blockingWait(
                handler.co_createEdgeLabel(std::make_unique<std::string>(name),
                                           std::make_unique<std::vector<PropertyDefThrift>>(std::move(props))));
            std::cout << formatEdgeLabelCreated(resp->name().value(), resp->id().value());
        } else if (cmd == ":list-labels") {
            auto labels = folly::coro::blockingWait(handler.co_listLabels());
            if (labels->empty()) {
                std::cout << "(no labels)" << std::endl;
            } else {
                std::vector<std::tuple<int, std::string, std::string>> rows;
                for (const auto& l : *labels) {
                    std::string props_str;
                    for (size_t i = 0; i < l.properties()->size(); i++) {
                        if (i > 0)
                            props_str += ", ";
                        props_str += l.properties()->at(i).name().value();
                    }
                    if (props_str.empty())
                        props_str = "(none)";
                    rows.push_back({l.id().value(), l.name().value(), props_str});
                }
                std::cout << formatLabelList(rows);
            }
        } else if (cmd == ":list-edge-labels") {
            auto labels = folly::coro::blockingWait(handler.co_listEdgeLabels());
            if (labels->empty()) {
                std::cout << "(no edge labels)" << std::endl;
            } else {
                std::vector<std::tuple<int, std::string, std::string, bool>> rows;
                for (const auto& l : *labels) {
                    rows.push_back({l.id().value(), l.name().value(), "(none)", l.directed().value()});
                }
                std::cout << formatEdgeLabelList(rows);
            }
        } else if (cmd.empty()) {
            // Cypher query - use executor directly for embedded mode
            std::string query = accumulated;
            if (!query.empty() && query.back() == ';') {
                query.pop_back();
            }
            auto result = folly::coro::blockingWait(executor.executeAsync(query));
            if (!result.error.empty()) {
                std::cerr << "Error: " << result.error << std::endl;
            } else if (result.columns.empty()) {
                std::cout << "OK" << std::endl;
            } else {
                std::vector<std::vector<std::string>> rows;
                for (const auto& row : result.rows) {
                    std::vector<std::string> cells;
                    for (const auto& val : row) {
                        cells.push_back(valueToString(val));
                    }
                    rows.push_back(std::move(cells));
                }
                std::cout << formatTable(result.columns, rows);
                std::cout << result.rows.size() << " row" << (result.rows.size() == 1 ? "" : "s");
            }
        } else {
            std::cerr << "Unknown command: " << cmd << std::endl;
            std::cerr << "Type :help for available commands." << std::endl;
        }
    };

    runReplLoop("embedded", cmd_handler);

    folly::coro::blockingWait(async_meta->close());
    sync_data->close();
    sync_meta->close();
}

// ==================== RPC mode REPL ====================

static void runRpcRepl(const ShellConfig& config) {
    EuGraphRpcClient client(config.host, config.port);

    std::cout << "Connecting to " << config.host << ":" << config.port << "..." << std::endl;
    {
        auto t0 = std::chrono::steady_clock::now();
        if (!client.connect()) {
            std::cerr << "Error: Failed to connect to server." << std::endl;
            return;
        }
        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        std::cout << "Connected. (connect took " << ms << "ms)" << std::endl;
    }

    auto cmd_handler = [&client](const std::string& cmd, const std::string& args, const std::string& accumulated) {
        try {
            if (cmd == ":help") {
                std::cout << "Available commands:" << std::endl;
                std::cout << "  :create-label <name> [prop1:type1 ...]" << std::endl;
                std::cout << "  :create-edge-label <name> [prop1:type1 ...]" << std::endl;
                std::cout << "  :list-labels" << std::endl;
                std::cout << "  :list-edge-labels" << std::endl;
                std::cout << "  :help                           Show this help" << std::endl;
                std::cout << "  :exit / :quit                   Exit shell" << std::endl;
                std::cout << std::endl;
                std::cout << "Any other input is treated as a Cypher query (end with ;)." << std::endl;
            } else if (cmd == ":create-label") {
                if (args.empty()) {
                    std::cerr << "Usage: :create-label <name> [prop1:type1 ...]" << std::endl;
                    return;
                }
                std::istringstream iss(args);
                std::string name;
                iss >> name;
                std::string rest;
                std::getline(iss, rest);
                auto props = parsePropertyDefs(rest);
                auto resp = client.createLabel(name, props);
                std::cout << formatLabelCreated(resp.name().value(), resp.id().value());
            } else if (cmd == ":create-edge-label") {
                if (args.empty()) {
                    std::cerr << "Usage: :create-edge-label <name> [prop1:type1 ...]" << std::endl;
                    return;
                }
                std::istringstream iss(args);
                std::string name;
                iss >> name;
                std::string rest;
                std::getline(iss, rest);
                auto props = parsePropertyDefs(rest);
                auto resp = client.createEdgeLabel(name, props);
                std::cout << formatEdgeLabelCreated(resp.name().value(), resp.id().value());
            } else if (cmd == ":list-labels") {
                auto labels = client.listLabels();
                if (labels.empty()) {
                    std::cout << "(no labels)" << std::endl;
                } else {
                    std::vector<std::tuple<int, std::string, std::string>> rows;
                    for (const auto& l : labels) {
                        std::string props_str;
                        for (size_t i = 0; i < l.properties()->size(); i++) {
                            if (i > 0)
                                props_str += ", ";
                            props_str += l.properties()->at(i).name().value();
                        }
                        if (props_str.empty())
                            props_str = "(none)";
                        rows.push_back({l.id().value(), l.name().value(), props_str});
                    }
                    std::cout << formatLabelList(rows);
                }
            } else if (cmd == ":list-edge-labels") {
                auto labels = client.listEdgeLabels();
                if (labels.empty()) {
                    std::cout << "(no edge labels)" << std::endl;
                } else {
                    std::vector<std::tuple<int, std::string, std::string, bool>> rows;
                    for (const auto& l : labels) {
                        rows.push_back({l.id().value(), l.name().value(), "(none)", l.directed().value()});
                    }
                    std::cout << formatEdgeLabelList(rows);
                }
            } else if (cmd.empty()) {
                std::string query = accumulated;
                if (!query.empty() && query.back() == ';') {
                    query.pop_back();
                }
                try {
                    auto [meta, stream] = client.executeCypher(query);
                    if (meta.columns()->empty()) {
                        std::cout << "OK" << std::endl;
                    } else {
                        size_t total_rows = 0;
                        std::vector<std::vector<std::string>> all_rows;
                        std::move(stream).subscribeInline([&](folly::Try<ResultRowBatch> batch) {
                            if (batch.hasValue()) {
                                for (const auto& row : *batch->rows()) {
                                    std::vector<std::string> cells;
                                    for (const auto& val : *row.values()) {
                                        cells.push_back(resultValueToString(val));
                                    }
                                    all_rows.push_back(std::move(cells));
                                    ++total_rows;
                                }
                            }
                        });
                        std::cout << formatTable(*meta.columns(), all_rows);
                        std::cout << total_rows << " row" << (total_rows == 1 ? "" : "s");
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Error: " << e.what() << std::endl;
                }
            } else {
                std::cerr << "Unknown command: " << cmd << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "RPC error: " << e.what() << std::endl;
        }
    };

    runReplLoop("rpc", cmd_handler);
}

// ==================== Public REPL entry ====================

void runRepl(const ShellConfig& config) {
    if (config.data_dir.empty()) {
        // No data dir → RPC mode
        runRpcRepl(config);
    } else {
        // Data dir provided → embedded mode
        runEmbeddedRepl(config);
    }
}

} // namespace shell
} // namespace eugraph
