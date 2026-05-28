#include "program/shell/shell_repl.hpp"
#include "program/shell/rpc_client.hpp"

#include "gen-cpp2/eugraph_types.h"
#include "thrift_fmt/result_format.hpp"

#include <linenoise.h>

#include <spdlog/spdlog.h>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace eugraph {
namespace shell {

using namespace eugraph::thrift;

// ==================== Value formatting ====================

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

std::string formatPropertyList(const std::vector<PropertyDefThrift>& props) {
    if (props.empty())
        return "(none)";
    std::string result;
    for (size_t i = 0; i < props.size(); ++i) {
        if (i > 0)
            result += ", ";
        result += props[i].name().value() + ":" + thrift_fmt::propertyTypeToString(props[i].type().value());
    }
    return result;
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

std::string formatTimestamp(int64_t ts) {
    auto time_t_val = static_cast<time_t>(ts);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t_val), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string formatGraphList(const std::vector<thrift::GraphInfo>& graphs) {
    std::vector<std::string> cols = {"graph_id", "graph_name", "created_at"};
    std::vector<std::vector<std::string>> rows;
    for (const auto& g : graphs) {
        rows.push_back(
            {std::to_string(g.graph_id().value()), g.name().value(), formatTimestamp(g.created_at().value())});
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

std::vector<PropertyDefThrift> parsePropertyDefs(const std::string& args) {
    std::vector<PropertyDefThrift> props;
    std::istringstream iss(args);
    std::string token;
    while (iss >> token) {
        auto colon_pos = token.find(':');
        PropertyDefThrift def;
        if (colon_pos != std::string::npos) {
            def.name() = token.substr(0, colon_pos);
            def.type() = thrift_fmt::parsePropertyType(token.substr(colon_pos + 1));
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

using CommandHandler = std::function<void(const std::string& cmd, const std::string& args,
                                          const std::string& accumulated, const std::string& graph_name)>;

static void completionCallback(const char* buf, linenoiseCompletions* lc) {
    if (buf[0] == ':') {
        const char* cmds[] = {":help",
                              ":exit",
                              ":quit",
                              ":create-label",
                              ":create-edge-label",
                              ":list-labels",
                              ":list-edge-labels",
                              ":create-graph",
                              ":drop-graph",
                              ":list-graphs",
                              ":use"};
        for (const auto* c : cmds) {
            if (strncmp(c, buf, strlen(buf)) == 0) {
                linenoiseAddCompletion(lc, c);
            }
        }
    }
}

static void runReplLoop(const std::string& mode_label, const std::string& current_graph, CommandHandler handler) {
    linenoiseSetCompletionCallback(completionCallback);
    linenoiseHistorySetMaxLen(256);

    std::string history_path = std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.eugraph_history";
    linenoiseHistoryLoad(history_path.c_str());

    std::cout << "EuGraph Shell (" << mode_label << " mode)" << std::endl;
    std::cout << "Type :help for available commands, :exit to quit." << std::endl;
    std::cout << std::endl;

    std::string graph = current_graph;

    while (true) {
        std::string accumulated;

        while (true) {
            std::string prompt = "\033[32meugraph(" + graph + ")>\033[0m ";
            const char* prompt_str = accumulated.empty() ? prompt.c_str() : "\033[36m......>\033[0m ";
            char* line_raw = linenoise(prompt_str);
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
        } else if (cmd == ":use") {
            linenoiseHistoryAdd(accumulated.c_str());
            if (args.empty()) {
                std::cerr << "Usage: :use <graph_name>" << std::endl;
            } else {
                graph = args;
                std::cout << "Switched to graph '" << graph << "'" << std::endl;
            }
        } else {
            linenoiseHistoryAdd(accumulated.c_str());
            auto t0 = std::chrono::steady_clock::now();
            handler(cmd, args, accumulated, graph);
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

    std::string current_graph = "default";

    auto cmd_handler = [&client](const std::string& cmd, const std::string& args, const std::string& accumulated,
                                 const std::string& graph_name) {
        try {
            if (cmd == ":help") {
                std::cout << "Available commands:" << std::endl;
                std::cout << "  :create-graph <name>            Create a new graph" << std::endl;
                std::cout << "  :drop-graph <name>              Drop a graph" << std::endl;
                std::cout << "  :list-graphs                    List all graphs" << std::endl;
                std::cout << "  :use <name>                     Switch current graph" << std::endl;
                std::cout << "  :create-label <name> [prop:type ...]" << std::endl;
                std::cout << "  :create-edge-label <name> [prop:type ...]" << std::endl;
                std::cout << "  :list-labels                    List labels in current graph" << std::endl;
                std::cout << "  :list-edge-labels               List edge labels in current graph" << std::endl;
                std::cout << "  :help                           Show this help" << std::endl;
                std::cout << "  :exit / :quit                   Exit shell" << std::endl;
                std::cout << std::endl;
                std::cout << "Any other input is treated as a Cypher query (end with ;)." << std::endl;
            } else if (cmd == ":create-graph") {
                if (args.empty()) {
                    std::cerr << "Usage: :create-graph <name>" << std::endl;
                    return;
                }
                auto resp = client.createGraph(args);
                std::cout << "Graph created: " << args << " (id=" << resp.graph_id().value() << ")" << std::endl;
            } else if (cmd == ":drop-graph") {
                if (args.empty()) {
                    std::cerr << "Usage: :drop-graph <name>" << std::endl;
                    return;
                }
                client.dropGraph(args);
                std::cout << "Graph dropped: " << args << std::endl;
            } else if (cmd == ":list-graphs") {
                auto graphs = client.listGraphs();
                if (graphs.empty()) {
                    std::cout << "(no graphs)" << std::endl;
                } else {
                    std::cout << formatGraphList(graphs);
                }
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
                auto resp = client.createLabel(name, props, graph_name);
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
                auto resp = client.createEdgeLabel(name, props, graph_name);
                std::cout << formatEdgeLabelCreated(resp.name().value(), resp.id().value());
            } else if (cmd == ":list-labels") {
                auto labels = client.listLabels(graph_name);
                if (labels.empty()) {
                    std::cout << "(no labels)" << std::endl;
                } else {
                    std::vector<std::tuple<int, std::string, std::string>> rows;
                    for (const auto& l : labels) {
                        rows.push_back({l.id().value(), l.name().value(), formatPropertyList(*l.properties())});
                    }
                    std::cout << formatLabelList(rows);
                }
            } else if (cmd == ":list-edge-labels") {
                auto labels = client.listEdgeLabels(graph_name);
                if (labels.empty()) {
                    std::cout << "(no edge labels)" << std::endl;
                } else {
                    std::vector<std::tuple<int, std::string, std::string, bool>> rows;
                    for (const auto& l : labels) {
                        rows.push_back({l.id().value(), l.name().value(), formatPropertyList(*l.properties()),
                                        l.directed().value()});
                    }
                    std::cout << formatEdgeLabelList(rows);
                }
            } else if (cmd.empty()) {
                std::string query = accumulated;
                if (!query.empty() && query.back() == ';') {
                    query.pop_back();
                }
                try {
                    auto [meta, stream] = client.executeCypher(query, graph_name);
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
                                        cells.push_back(thrift_fmt::formatResultValue(val));
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

    runReplLoop("rpc", current_graph, cmd_handler);
}

// ==================== Public REPL entry ====================

void runRepl(const ShellConfig& config) {
    runRpcRepl(config);
}

} // namespace shell
} // namespace eugraph
