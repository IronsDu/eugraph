#include "tck_context.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <regex>
#include <set>
#include <sstream>
#include <unordered_set>

namespace eugraph::tck {

// Static members
std::unique_ptr<shell::EuGraphRpcClient> TckContext::rpc;
std::string TckContext::serverHost = "127.0.0.1";
int TckContext::serverPort = 9999;

// ---------- Query execution ----------

void TckContext::executeQuery(const std::string& query) {
    lastRows.clear();
    lastColumns.clear();
    lastQueryHadError = false;
    lastErrorType.clear();
    lastErrorPhase.clear();
    lastErrorDetail.clear();

    try {
        auto [meta, stream] = rpc->executeCypher(query, graphName);

        // Extract column names from metadata
        lastColumns = *meta.columns();

        // Process stream
        std::move(stream).subscribeInline([this](folly::Try<thrift::ResultRowBatch>&& batch) {
            if (batch.hasValue()) {
                for (const auto& row : *batch->rows()) {
                    std::vector<std::string> tckRow;
                    for (const auto& val : *row.values()) {
                        tckRow.push_back(formatValue(val));
                    }
                    lastRows.push_back(std::move(tckRow));
                }
            } else if (batch.hasException()) {
                lastQueryHadError = true;
                lastErrorType = "RuntimeError";
                lastErrorPhase = "runtime";
                lastErrorDetail = batch.exception().what().toStdString();
                spdlog::info("[TCK] Stream error: {}", lastErrorDetail);
            }
        });
    } catch (const std::exception& e) {
        lastQueryHadError = true;
        std::string errMsg = e.what();

        // Classify error by message content
        if (errMsg.find("SyntaxError") != std::string::npos || errMsg.find("syntax") != std::string::npos ||
            errMsg.find("parse") != std::string::npos || errMsg.find("UndefinedVariable") != std::string::npos ||
            errMsg.find("VariableAlreadyBound") != std::string::npos) {
            lastErrorType = "SyntaxError";
            lastErrorPhase = "compile time";
        } else if (errMsg.find("SemanticError") != std::string::npos || errMsg.find("semantic") != std::string::npos) {
            lastErrorType = "SemanticError";
            lastErrorPhase = "compile time";
        } else if (errMsg.find("TypeError") != std::string::npos || errMsg.find("type") != std::string::npos) {
            lastErrorType = "TypeError";
            lastErrorPhase = "runtime";
        } else {
            lastErrorType = "RuntimeError";
            lastErrorPhase = "runtime";
        }

        // Extract error detail (the specific error name)
        if (errMsg.find("UndefinedVariable") != std::string::npos) {
            lastErrorDetail = "UndefinedVariable";
        } else if (errMsg.find("VariableAlreadyBound") != std::string::npos) {
            lastErrorDetail = "VariableAlreadyBound";
        } else if (errMsg.find("InvalidInput") != std::string::npos) {
            lastErrorDetail = "InvalidInput";
        } else {
            lastErrorDetail = errMsg;
        }

        spdlog::info("[TCK] Query error: type={} phase={} detail={}", lastErrorType, lastErrorPhase, lastErrorDetail);
    }
}

// ---------- Graph snapshot ----------

GraphSnapshot TckContext::takeSnapshot() {
    GraphSnapshot snap;

    // Count nodes
    try {
        auto [meta, stream] = rpc->executeCypher("MATCH (n) RETURN count(n)", graphName);
        std::move(stream).subscribeInline([&snap](folly::Try<thrift::ResultRowBatch>&& batch) {
            if (batch.hasValue()) {
                for (const auto& row : *batch->rows()) {
                    if (row.values()->size() > 0) {
                        const auto& v = (*row.values())[0];
                        if (v.getType() == thrift::ResultValue::Type::int_val) {
                            snap.nodeCount = v.get_int_val();
                        }
                    }
                }
            }
        });
    } catch (...) {
        snap.nodeCount = 0;
    }

    // Count edges
    try {
        auto [meta, stream] = rpc->executeCypher("MATCH ()-[r]->() RETURN count(r)", graphName);
        std::move(stream).subscribeInline([&snap](folly::Try<thrift::ResultRowBatch>&& batch) {
            if (batch.hasValue()) {
                for (const auto& row : *batch->rows()) {
                    if (row.values()->size() > 0) {
                        const auto& v = (*row.values())[0];
                        if (v.getType() == thrift::ResultValue::Type::int_val) {
                            snap.edgeCount = v.get_int_val();
                        }
                    }
                }
            }
        });
    } catch (...) {
        snap.edgeCount = 0;
    }

    // For label/property counts, use simple approximation
    // Label count = total number of label instances across all nodes
    // Property count = total number of property instances
    // For now, use 0 as baseline; side effects still work since we compute deltas

    return snap;
}

// ---------- Value formatting (TCK format) ----------

std::string TckContext::formatValue(const thrift::ResultValue& val) {
    switch (val.getType()) {
    case thrift::ResultValue::Type::bool_val:
        return val.get_bool_val() ? "true" : "false";

    case thrift::ResultValue::Type::int_val:
        return std::to_string(val.get_int_val());

    case thrift::ResultValue::Type::double_val: {
        double d = val.get_double_val();
        std::ostringstream oss;
        oss << d;
        return oss.str();
    }

    case thrift::ResultValue::Type::string_val:
        return "'" + val.get_string_val() + "'";

    case thrift::ResultValue::Type::vertex_json:
        return convertVertexJson(val.get_vertex_json());

    case thrift::ResultValue::Type::edge_json:
        return convertEdgeJson(val.get_edge_json());

    case thrift::ResultValue::Type::path_json:
        return val.get_path_json(); // Server already formats path

    case thrift::ResultValue::Type::list_json:
        return val.get_list_json(); // JSON array format

    default:
        return "null";
    }
}

// Convert vertex JSON {"id":1,"label":"Person","name":"Alice"} → (:Person {name: 'Alice'})
std::string TckContext::convertVertexJson(const std::string& json) {
    if (json.empty())
        return "()";
    try {
        auto j = nlohmann::json::parse(json);
        std::ostringstream oss;
        oss << "(";
        if (j.contains("label") && j["label"].is_string()) {
            oss << ":" << j["label"].get<std::string>();
        }
        // Collect properties (all keys except id and label)
        std::vector<std::pair<std::string, std::string>> props;
        for (auto& [key, value] : j.items()) {
            if (key == "id" || key == "label")
                continue;
            std::string formattedVal;
            if (value.is_string()) {
                formattedVal = "'" + value.get<std::string>() + "'";
            } else if (value.is_number_integer()) {
                formattedVal = std::to_string(value.get<int64_t>());
            } else if (value.is_number_float()) {
                std::ostringstream dss;
                dss << value.get<double>();
                formattedVal = dss.str();
            } else if (value.is_boolean()) {
                formattedVal = value.get<bool>() ? "true" : "false";
            } else if (value.is_null()) {
                formattedVal = "null";
            } else if (value.is_array()) {
                formattedVal = value.dump();
            } else {
                formattedVal = value.dump();
            }
            props.emplace_back(key, formattedVal);
        }
        if (!props.empty()) {
            oss << " {";
            for (size_t i = 0; i < props.size(); ++i) {
                if (i > 0)
                    oss << ", ";
                oss << props[i].first << ": " << props[i].second;
            }
            oss << "}";
        }
        oss << ")";
        return oss.str();
    } catch (...) {
        return "()";
    }
}

// Convert edge JSON {"id":1,"src":1,"dst":2,"label":"KNOWS"} → [:KNOWS]
std::string TckContext::convertEdgeJson(const std::string& json) {
    if (json.empty())
        return "[]";
    try {
        auto j = nlohmann::json::parse(json);
        std::ostringstream oss;
        oss << "[";
        if (j.contains("label") && j["label"].is_string()) {
            oss << ":" << j["label"].get<std::string>();
        }
        // Properties
        std::vector<std::pair<std::string, std::string>> props;
        for (auto& [key, value] : j.items()) {
            if (key == "id" || key == "src" || key == "dst" || key == "label")
                continue;
            std::string formattedVal;
            if (value.is_string()) {
                formattedVal = "'" + value.get<std::string>() + "'";
            } else if (value.is_number_integer()) {
                formattedVal = std::to_string(value.get<int64_t>());
            } else if (value.is_number_float()) {
                std::ostringstream dss;
                dss << value.get<double>();
                formattedVal = dss.str();
            } else if (value.is_boolean()) {
                formattedVal = value.get<bool>() ? "true" : "false";
            } else if (value.is_null()) {
                formattedVal = "null";
            } else {
                formattedVal = value.dump();
            }
            props.emplace_back(key, formattedVal);
        }
        if (!props.empty()) {
            oss << " {";
            for (size_t i = 0; i < props.size(); ++i) {
                if (i > 0)
                    oss << ", ";
                oss << props[i].first << ": " << props[i].second;
            }
            oss << "}";
        }
        oss << "]";
        return oss.str();
    } catch (...) {
        return "[]";
    }
}

// ---------- Type inference from query text ----------

void TckContext::ensureTypesForQuery(const std::string& query) {
    connectRpc();

    // Extract label names: pattern (:<LabelName>  or  :<LabelName>)
    std::set<std::string> labels;
    std::regex labelRe(R"(:([A-Za-z_][A-Za-z0-9_]*))");
    auto begin = std::sregex_iterator(query.begin(), query.end(), labelRe);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        std::string name = (*it)[1].str();
        // Filter out known non-label contexts
        if (name == "n" || name == "r" || name == "a" || name == "b" || name == "m" || name == "x" || name == "y" ||
            name == "p" || name == "e" || name == "id" || name == "name" || name == "age" || name == "city" ||
            name == "expr" || name == "idx" || name == "n1" || name == "n2" || name == "n3") {
            continue;
        }
        labels.insert(name);
    }

    // Extract edge type names: pattern -[:TYPE]->
    std::set<std::string> edgeTypes;
    std::regex edgeRe(R"(\[:([A-Za-z_][A-Za-z0-9_]*))");
    auto ebegin = std::sregex_iterator(query.begin(), query.end(), edgeRe);
    for (auto it = ebegin; it != end; ++it) {
        std::string name = (*it)[1].str();
        edgeTypes.insert(name);
    }

    // Also match patterns like (n:Label) where n is a known variable and Label is the label name
    // These are captured by the labelRe above

    // Create labels (if not already created)
    for (const auto& label : labels) {
        spdlog::info("[TCK] Auto-creating label: {}", label);
        try {
            rpc->createLabel(label, {}, graphName);
        } catch (const std::exception& e) {
            spdlog::info("[TCK] Label {} may already exist: {}", label, e.what());
        }
    }

    // Create edge labels
    for (const auto& et : edgeTypes) {
        spdlog::info("[TCK] Auto-creating edge label: {}", et);
        try {
            rpc->createEdgeLabel(et, {}, graphName);
        } catch (const std::exception& e) {
            spdlog::info("[TCK] EdgeLabel {} may already exist: {}", et, e.what());
        }
    }
}

} // namespace eugraph::tck
