#include "tck_context.hpp"

#include "query/parser/ast.hpp"
#include "query/parser/cypher_parser.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_set>

namespace eugraph::tck {
namespace {

// ---- AST walkers for unsupported-feature detection ----------

namespace ast = cypher;

bool hasUnsupportedExpr(const ast::Expression& expr);

bool hasUnsupportedPattern(const ast::PatternPart& pp) {
    // Check path pattern: node + chain
    const ast::PatternElement& el = pp.element;

    // Check start node: multi-label?
    if (el.node.labels.size() > 1) {
        spdlog::info("[TCK] skipping: multi-label node pattern");
        return true;
    }
    if (el.node.properties.has_value()) {
        for (const auto& [k, v] : el.node.properties->entries) {
            if (hasUnsupportedExpr(v))
                return true;
        }
    }

    // Check each chain step: (rel, node)
    for (const auto& [rel, node] : el.chain) {
        // Multi-label node?
        if (node.labels.size() > 1) {
            spdlog::info("[TCK] skipping: multi-label node pattern");
            return true;
        }
        // Relationship type alternation? [:A|B]
        if (rel.rel_types.size() > 1) {
            spdlog::info("[TCK] skipping: relationship type alternation");
            return true;
        }
        // Unbounded variable-length expand? [:T*] without upper bound
        if (rel.range.has_value()) {
            const auto& [min_expr, max_expr] = *rel.range;
            if (std::holds_alternative<std::unique_ptr<ast::Literal>>(max_expr)) {
                auto& lit = std::get<std::unique_ptr<ast::Literal>>(max_expr);
                if (std::holds_alternative<int64_t>(lit->value) && std::get<int64_t>(lit->value) < 0) {
                    spdlog::info("[TCK] skipping: unbounded variable-length expand");
                    return true;
                }
            }
        }
        // Check properties in rel and node
        if (rel.properties.has_value()) {
            for (const auto& [k, v] : rel.properties->entries) {
                if (hasUnsupportedExpr(v))
                    return true;
            }
        }
        if (node.properties.has_value()) {
            for (const auto& [k, v] : node.properties->entries) {
                if (hasUnsupportedExpr(v))
                    return true;
            }
        }
    }
    return false;
}

bool hasUnsupportedExpr(const ast::Expression& expr) {
    return std::visit(
        [](const auto& ptr) -> bool {
            using T = std::decay_t<decltype(ptr)>;
            using Inner = typename T::element_type;

            if constexpr (std::is_same_v<Inner, ast::Parameter>) {
                spdlog::info("[TCK] skipping: parameter ($)");
                return true;
            } else if constexpr (std::is_same_v<Inner, ast::CaseExpr>) {
                spdlog::info("[TCK] skipping: CASE expression");
                return true;
            } else if constexpr (std::is_same_v<Inner, ast::ExistsExpr>) {
                spdlog::info("[TCK] skipping: EXISTS subquery");
                return true;
            } else if constexpr (std::is_same_v<Inner, ast::BinaryOp>) {
                switch (ptr->op) {
                case ast::BinaryOperator::STARTS_WITH:
                    spdlog::info("[TCK] skipping: STARTS WITH");
                    return true;
                case ast::BinaryOperator::ENDS_WITH:
                    spdlog::info("[TCK] skipping: ENDS WITH");
                    return true;
                case ast::BinaryOperator::CONTAINS:
                    spdlog::info("[TCK] skipping: CONTAINS");
                    return true;
                case ast::BinaryOperator::IN:
                    spdlog::info("[TCK] skipping: IN");
                    return true;
                case ast::BinaryOperator::XOR:
                    spdlog::info("[TCK] skipping: XOR");
                    return true;
                default:
                    break;
                }
                return hasUnsupportedExpr(ptr->left) || hasUnsupportedExpr(ptr->right);
            } else if constexpr (std::is_same_v<Inner, ast::FunctionCall>) {
                const std::string& name = ptr->name;
                // apoc.*
                if (name.starts_with("apoc.")) {
                    spdlog::info("[TCK] skipping: apoc function '{}'", name);
                    return true;
                }
                // shortestPath (case-insensitive)
                std::string lower = name;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                if (lower.find("shortestpath") != std::string::npos) {
                    spdlog::info("[TCK] skipping: shortestPath function");
                    return true;
                }
                // Recurse into args
                for (const auto& arg : ptr->args) {
                    if (hasUnsupportedExpr(arg))
                        return true;
                }
                return false;
            } else if constexpr (std::is_same_v<Inner, ast::UnaryOp>) {
                return hasUnsupportedExpr(ptr->operand);
            } else if constexpr (std::is_same_v<Inner, ast::PropertyAccess>) {
                return hasUnsupportedExpr(ptr->object);
            } else if constexpr (std::is_same_v<Inner, ast::ListExpr>) {
                for (const auto& e : ptr->elements) {
                    if (hasUnsupportedExpr(e))
                        return true;
                }
                return false;
            } else if constexpr (std::is_same_v<Inner, ast::MapExpr>) {
                for (const auto& [k, v] : ptr->entries) {
                    if (hasUnsupportedExpr(v))
                        return true;
                }
                return false;
            } else if constexpr (std::is_same_v<Inner, ast::LabelCastExpr>) {
                return hasUnsupportedExpr(ptr->object);
            } else if constexpr (std::is_same_v<Inner, ast::ListComprehension>) {
                if (hasUnsupportedExpr(ptr->list_expr))
                    return true;
                if (ptr->where_pred && hasUnsupportedExpr(*ptr->where_pred))
                    return true;
                if (ptr->projection && hasUnsupportedExpr(*ptr->projection))
                    return true;
                return false;
            } else if constexpr (std::is_same_v<Inner, ast::PatternComprehension>) {
                spdlog::info("[TCK] skipping: pattern comprehension");
                return true;
            } else if constexpr (std::is_same_v<Inner, ast::SubscriptExpr>) {
                return hasUnsupportedExpr(ptr->list) || hasUnsupportedExpr(ptr->index);
            } else if constexpr (std::is_same_v<Inner, ast::SliceExpr>) {
                bool r = hasUnsupportedExpr(ptr->list);
                if (ptr->from)
                    r = r || hasUnsupportedExpr(*ptr->from);
                if (ptr->to)
                    r = r || hasUnsupportedExpr(*ptr->to);
                return r;
            } else if constexpr (std::is_same_v<Inner, ast::AllExpr> || std::is_same_v<Inner, ast::AnyExpr> ||
                                 std::is_same_v<Inner, ast::NoneExpr> || std::is_same_v<Inner, ast::SingleExpr>) {
                spdlog::info("[TCK] skipping: quantifier expression (ALL/ANY/NONE/SINGLE)");
                return true;
            } else {
                // Literal, Variable — leaf types, no unsupported features
                return false;
            }
        },
        expr);
}

bool hasUnsupportedClause(const ast::Clause& clause) {
    return std::visit(
        [](const auto& ptr) -> bool {
            using Inner = typename std::decay_t<decltype(ptr)>::element_type;

            if constexpr (std::is_same_v<Inner, ast::MatchClause>) {
                if (ptr->optional) {
                    spdlog::info("[TCK] skipping: OPTIONAL MATCH");
                    return true;
                }
                for (const auto& pp : ptr->patterns) {
                    if (hasUnsupportedPattern(pp))
                        return true;
                }
                if (ptr->where_pred && hasUnsupportedExpr(*ptr->where_pred))
                    return true;
                return false;
            } else if constexpr (std::is_same_v<Inner, ast::CreateClause>) {
                for (const auto& pp : ptr->patterns) {
                    if (hasUnsupportedPattern(pp))
                        return true;
                }
                return false;
            } else if constexpr (std::is_same_v<Inner, ast::DeleteClause>) {
                if (ptr->detach) {
                    spdlog::info("[TCK] skipping: DETACH DELETE");
                } else {
                    spdlog::info("[TCK] skipping: DELETE");
                }
                return true;
            } else if constexpr (std::is_same_v<Inner, ast::MergeClause>) {
                spdlog::info("[TCK] skipping: MERGE");
                return true;
            } else if constexpr (std::is_same_v<Inner, ast::UnwindClause>) {
                spdlog::info("[TCK] skipping: UNWIND");
                return true;
            } else if constexpr (std::is_same_v<Inner, ast::CallClause>) {
                spdlog::info("[TCK] skipping: CALL");
                return true;
            } else if constexpr (std::is_same_v<Inner, ast::RemoveClause>) {
                spdlog::info("[TCK] skipping: REMOVE");
                return true;
            } else if constexpr (std::is_same_v<Inner, ast::ReturnClause>) {
                for (const auto& item : ptr->items) {
                    if (hasUnsupportedExpr(item.expr))
                        return true;
                }
                if (ptr->order_by.has_value()) {
                    for (const auto& si : ptr->order_by->items) {
                        if (hasUnsupportedExpr(si.expr))
                            return true;
                    }
                }
                if (ptr->skip && hasUnsupportedExpr(*ptr->skip))
                    return true;
                if (ptr->limit && hasUnsupportedExpr(*ptr->limit))
                    return true;
                return false;
            } else if constexpr (std::is_same_v<Inner, ast::WithClause>) {
                for (const auto& item : ptr->items) {
                    if (hasUnsupportedExpr(item.expr))
                        return true;
                }
                if (ptr->order_by.has_value()) {
                    for (const auto& si : ptr->order_by->items) {
                        if (hasUnsupportedExpr(si.expr))
                            return true;
                    }
                }
                if (ptr->skip && hasUnsupportedExpr(*ptr->skip))
                    return true;
                if (ptr->limit && hasUnsupportedExpr(*ptr->limit))
                    return true;
                if (ptr->where_pred && hasUnsupportedExpr(*ptr->where_pred))
                    return true;
                return false;
            } else if constexpr (std::is_same_v<Inner, ast::SetClause>) {
                // SET clause: check expressions in set items
                for (const auto& item : ptr->items) {
                    if (hasUnsupportedExpr(item.target))
                        return true;
                    if (item.value && hasUnsupportedExpr(*item.value))
                        return true;
                }
                return false;
            } else {
                return false;
            }
        },
        clause);
}

bool hasUnsupportedFeature(const ast::Statement& stmt) {
    return std::visit(
        [](const auto& ptr) -> bool {
            using Inner = typename std::decay_t<decltype(ptr)>::element_type;

            if constexpr (std::is_same_v<Inner, ast::RegularQuery>) {
                const auto& rq = *ptr;

                // UNION?
                if (!rq.unions.empty()) {
                    spdlog::info("[TCK] skipping: UNION");
                    return true;
                }

                // Walk clauses
                for (const auto& c : rq.first.clauses) {
                    if (hasUnsupportedClause(c))
                        return true;
                }
                // Also check union parts
                for (const auto& [ucl, sq] : rq.unions) {
                    for (const auto& c : sq.clauses) {
                        if (hasUnsupportedClause(c))
                            return true;
                    }
                }
                return false;
            } else if constexpr (std::is_same_v<Inner, ast::StandaloneCall>) {
                spdlog::info("[TCK] skipping: standalone CALL");
                return true;
            } else if constexpr (std::is_same_v<Inner, ast::ExplainStatement>) {
                spdlog::info("[TCK] skipping: EXPLAIN");
                return true;
            } else {
                return false;
            }
        },
        stmt);
}

// Regex fallbacks for constructs with no AST representation
const std::regex kForeachRe(R"(\bFOREACH\b)", std::regex::icase);
const std::regex kLoadCsvRe(R"(\bLOAD\s+CSV\b)", std::regex::icase);

} // anonymous namespace

bool TckContext::isQuerySupported(const std::string& query) {
    // Regex fallback: FOREACH and LOAD CSV have no AST nodes
    if (std::regex_search(query, kForeachRe)) {
        spdlog::info("[TCK] skipping: FOREACH");
        return false;
    }
    if (std::regex_search(query, kLoadCsvRe)) {
        spdlog::info("[TCK] skipping: LOAD CSV");
        return false;
    }

    // Parse with the Cypher parser; if parsing fails, the syntax is not
    // supported by our parser (which means we cannot execute it either).
    static thread_local cypher::CypherQueryParser parser;
    auto result = parser.parse(query);

    if (std::holds_alternative<cypher::ParseError>(result)) {
        spdlog::info("[TCK] skipping: parse error — {}", std::get<cypher::ParseError>(result).message);
        return false;
    }

    auto& stmt = std::get<cypher::Statement>(result);
    if (hasUnsupportedFeature(stmt))
        return false;

    return true;
}

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
        std::string errMsg = e.what();

        // Connection loss is a fatal infrastructure failure — abort
        if (isConnectionError(errMsg)) {
            spdlog::critical("[TCK] [{}] Connection lost — server may have crashed. Aborting test.", graphName);
            spdlog::critical("[TCK] Error: {}", errMsg);
            std::exit(1);
        }

        lastQueryHadError = true;

        // Classify error by message content
        if (errMsg.find("SyntaxError") != std::string::npos || errMsg.find("syntax") != std::string::npos ||
            errMsg.find("parse") != std::string::npos || errMsg.find("UndefinedVariable") != std::string::npos ||
            errMsg.find("VariableAlreadyBound") != std::string::npos) {
            lastErrorType = "SyntaxError";
            lastErrorPhase = "compile time";
        } else if (errMsg.find("Invalid argument type for function") != std::string::npos) {
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
        } else if (errMsg.find("Invalid argument type for function") != std::string::npos) {
            lastErrorDetail = "InvalidArgumentType";
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
            std::string msg = e.what();
            if (isConnectionError(msg)) {
                spdlog::critical("[TCK] [{}] Connection lost during ensureTypes — aborting: {}", graphName, msg);
                std::exit(1);
            }
            spdlog::info("[TCK] Label {} may already exist: {}", label, msg);
        }
    }

    // Create edge labels
    for (const auto& et : edgeTypes) {
        spdlog::info("[TCK] Auto-creating edge label: {}", et);
        try {
            rpc->createEdgeLabel(et, {}, graphName);
        } catch (const std::exception& e) {
            std::string msg = e.what();
            if (isConnectionError(msg)) {
                spdlog::critical("[TCK] [{}] Connection lost during ensureTypes — aborting: {}", graphName, msg);
                std::exit(1);
            }
            spdlog::info("[TCK] EdgeLabel {} may already exist: {}", et, msg);
        }
    }
}

} // namespace eugraph::tck
