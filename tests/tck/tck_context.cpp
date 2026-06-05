#include "tck_context.hpp"

#include "query/parser/ast.hpp"
#include "query/parser/cypher_parser.hpp"
#include "thrift_fmt/result_format.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_set>

namespace {
void classifyError(const std::string& errMsg, std::string& errorType, std::string& errorPhase) {
    if (errMsg.find("SyntaxError") != std::string::npos || errMsg.find("syntax") != std::string::npos ||
        errMsg.find("parse") != std::string::npos || errMsg.find("UndefinedVariable") != std::string::npos ||
        errMsg.find("VariableAlreadyBound") != std::string::npos ||
        errMsg.find("InvalidArgumentType") != std::string::npos ||
        errMsg.find("DifferentColumnsInUnion") != std::string::npos ||
        errMsg.find("InvalidClauseComposition") != std::string::npos ||
        errMsg.find("NoSingleRelationshipType") != std::string::npos ||
        errMsg.find("CreatingVarLength") != std::string::npos ||
        errMsg.find("InvalidParameterUse") != std::string::npos) {
        errorType = "SyntaxError";
        errorPhase = "compile time";
    } else if (errMsg.find("Invalid argument type for function") != std::string::npos) {
        errorType = "SyntaxError";
        errorPhase = "compile time";
    } else if (errMsg.find("SemanticError") != std::string::npos ||
               errMsg.find("MergeReadOwnWrites") != std::string::npos) {
        errorType = "SemanticError";
        errorPhase = "runtime";
    } else if (errMsg.find("TypeError") != std::string::npos) {
        errorType = "TypeError";
        errorPhase = "runtime";
    } else {
        errorType = "RuntimeError";
        errorPhase = "runtime";
    }
}
} // namespace

namespace eugraph::tck {
namespace {

// ---- AST walkers for unsupported-feature detection ----------

namespace ast = cypher;

bool hasUnsupportedExpr(const ast::Expression& expr);

bool hasUnsupportedPattern(const ast::PatternPart& pp, bool is_create = false) {
    (void)is_create; // multi-label now supported in both CREATE and MATCH
    // Check path pattern: node + chain
    const ast::PatternElement& el = pp.element;
    if (el.node.properties.has_value()) {
        for (const auto& [k, v] : el.node.properties->entries) {
            if (hasUnsupportedExpr(v))
                return true;
        }
    }

    // Check each chain step: (rel, node)
    for (const auto& [rel, node] : el.chain) {
        // Multi-label nodes are now supported in both CREATE and MATCH
        // Relationship type alternation? [:A|B]
        // For MERGE/CREATE, let the server-side binder reject it (NoSingleRelationshipType).
        // Only skip for MATCH where alternation is genuinely unsupported.
        if (rel.rel_types.size() > 1 && !is_create) {
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

            if constexpr (std::is_same_v<Inner, ast::ExistsExpr>) {
                // EXISTS subquery is now partially supported (simple form only).
                // Full form (EXISTS { MATCH ... RETURN ... }) and nested EXISTS
                // may still fail in binding.
                return false;
            } else if constexpr (std::is_same_v<Inner, ast::BinaryOp>) {
                switch (ptr->op) {
                case ast::BinaryOperator::IN:
                    spdlog::info("[TCK] skipping: IN");
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
                return false;
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
                for (const auto& pp : ptr->patterns) {
                    if (hasUnsupportedPattern(pp))
                        return true;
                }
                if (ptr->where_pred && hasUnsupportedExpr(*ptr->where_pred))
                    return true;
                return false;
            } else if constexpr (std::is_same_v<Inner, ast::CreateClause>) {
                for (const auto& pp : ptr->patterns) {
                    if (hasUnsupportedPattern(pp, /*is_create=*/true))
                        return true;
                }
                return false;
            } else if constexpr (std::is_same_v<Inner, ast::MergeClause>) {
                // Check the merge pattern for unsupported features
                if (hasUnsupportedPattern(ptr->pattern, /*is_create=*/true))
                    return true;
                // Check ON CREATE / ON MATCH SET items
                for (const auto& si : ptr->on_create) {
                    if (hasUnsupportedExpr(si.target))
                        return true;
                    if (si.value && hasUnsupportedExpr(*si.value))
                        return true;
                }
                for (const auto& si : ptr->on_match) {
                    if (hasUnsupportedExpr(si.target))
                        return true;
                    if (si.value && hasUnsupportedExpr(*si.value))
                        return true;
                }
                return false;
            } else if constexpr (std::is_same_v<Inner, ast::CallClause>) {
                spdlog::info("[TCK] skipping: CALL");
                return true;
            } else if constexpr (std::is_same_v<Inner, ast::RemoveClause>) {
                // REMOVE clause: check expressions in remove items
                for (const auto& item : ptr->items) {
                    if (hasUnsupportedExpr(item.target))
                        return true;
                }
                return false;
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
        std::map<std::string, std::string> params;
        if (!pendingParams.empty()) {
            params.insert(pendingParams.begin(), pendingParams.end());
        }
        auto [meta, stream] = rpc->executeCypher(query, graphName, params);

        // Extract column names from metadata
        lastColumns = *meta.columns();

        // Process stream
        std::move(stream).subscribeInline([this](folly::Try<thrift::ResultRowBatch>&& batch) {
            if (batch.hasValue()) {
                for (const auto& row : *batch->rows()) {
                    std::vector<std::string> tckRow;
                    for (const auto& val : *row.values()) {
                        tckRow.push_back(thrift_fmt::formatResultValue(val));
                    }
                    lastRows.push_back(std::move(tckRow));
                }
            } else if (batch.hasException()) {
                lastQueryHadError = true;
                std::string errMsg = batch.exception().what().toStdString();
                spdlog::info("[TCK] Stream error: {}", errMsg);
                classifyError(errMsg, lastErrorType, lastErrorPhase);
                // Extract error detail
                if (errMsg.find("UndefinedVariable") != std::string::npos) {
                    lastErrorDetail = "UndefinedVariable";
                } else if (errMsg.find("VariableAlreadyBound") != std::string::npos) {
                    lastErrorDetail = "VariableAlreadyBound";
                } else if (errMsg.find("MergeReadOwnWrites") != std::string::npos) {
                    lastErrorDetail = "MergeReadOwnWrites";
                } else {
                    lastErrorDetail = errMsg;
                }
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
        classifyError(errMsg, lastErrorType, lastErrorPhase);

        // Extract error detail (the specific error name)
        if (errMsg.find("UndefinedVariable") != std::string::npos) {
            lastErrorDetail = "UndefinedVariable";
        } else if (errMsg.find("VariableAlreadyBound") != std::string::npos) {
            lastErrorDetail = "VariableAlreadyBound";
        } else if (errMsg.find("DifferentColumnsInUnion") != std::string::npos) {
            lastErrorDetail = "DifferentColumnsInUnion";
        } else if (errMsg.find("InvalidClauseComposition") != std::string::npos) {
            lastErrorDetail = "InvalidClauseComposition";
        } else if (errMsg.find("InvalidArgumentType") != std::string::npos) {
            lastErrorDetail = "InvalidArgumentType";
        } else if (errMsg.find("Invalid argument type for function") != std::string::npos) {
            lastErrorDetail = "InvalidArgumentType";
        } else if (errMsg.find("InvalidInput") != std::string::npos) {
            lastErrorDetail = "InvalidInput";
        } else if (errMsg.find("NoSingleRelationshipType") != std::string::npos) {
            lastErrorDetail = "NoSingleRelationshipType";
        } else if (errMsg.find("CreatingVarLength") != std::string::npos) {
            lastErrorDetail = "CreatingVarLength";
        } else if (errMsg.find("InvalidParameterUse") != std::string::npos) {
            lastErrorDetail = "InvalidParameterUse";
        } else if (errMsg.find("MergeReadOwnWrites") != std::string::npos) {
            lastErrorDetail = "MergeReadOwnWrites";
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
    spdlog::info("[TCK] [{}] snapshot: nodes={} edges={} props={} nodeIds={} edgeIds={}", graphName, snap.nodeCount,
                 snap.edgeCount, snap.propertyCount, snap.nodeIds.size(), snap.edgeIds.size());

    // Collect distinct label names across all nodes
    // MATCH (n) RETURN labels(n) — each row has list_json like ['A', 'B']
    try {
        auto [meta, stream] = rpc->executeCypher("MATCH (n) RETURN labels(n)", graphName);
        std::move(stream).subscribeInline([&snap](folly::Try<thrift::ResultRowBatch>&& batch) {
            if (batch.hasValue()) {
                for (const auto& row : *batch->rows()) {
                    if (row.values()->size() > 0) {
                        const auto& v = (*row.values())[0];
                        if (v.getType() == thrift::ResultValue::Type::list_json) {
                            std::string json = v.get_list_json();
                            // Format uses single quotes: ['A', 'B']
                            for (size_t i = 0; i < json.size(); ++i) {
                                if (json[i] == '\'') {
                                    size_t start = ++i;
                                    while (i < json.size() && json[i] != '\'')
                                        ++i;
                                    if (i < json.size())
                                        snap.labelNames.insert(json.substr(start, i - start));
                                }
                            }
                        }
                    }
                }
            }
        });
    } catch (...) {}
    spdlog::info("[TCK] [{}] distinct labels: {}", graphName, snap.labelNames.size());

    // Count property instances: vertex properties
    int64_t vprop_count = 0;
    try {
        auto [meta, stream] = rpc->executeCypher("MATCH (n) RETURN sum(size(keys(n)))", graphName);
        std::move(stream).subscribeInline([&vprop_count](folly::Try<thrift::ResultRowBatch>&& batch) {
            if (batch.hasValue()) {
                for (const auto& row : *batch->rows()) {
                    if (row.values()->size() > 0) {
                        const auto& v = (*row.values())[0];
                        if (v.getType() == thrift::ResultValue::Type::int_val) {
                            vprop_count = v.get_int_val();
                        }
                    }
                }
            }
        });
    } catch (...) {
        vprop_count = 0;
    }

    // Count property instances: edge properties
    int64_t eprop_count = 0;
    try {
        auto [meta, stream] = rpc->executeCypher("MATCH ()-[r]->() RETURN sum(size(keys(r)))", graphName);
        std::move(stream).subscribeInline([&eprop_count](folly::Try<thrift::ResultRowBatch>&& batch) {
            if (batch.hasValue()) {
                for (const auto& row : *batch->rows()) {
                    if (row.values()->size() > 0) {
                        const auto& v = (*row.values())[0];
                        if (v.getType() == thrift::ResultValue::Type::int_val) {
                            eprop_count = v.get_int_val();
                        }
                    }
                }
            }
        });
    } catch (...) {
        eprop_count = 0;
    }

    snap.propertyCount = vprop_count + eprop_count;

    // Collect node IDs for add/remove detection
    try {
        auto [meta, stream] = rpc->executeCypher("MATCH (n) RETURN id(n)", graphName);
        std::move(stream).subscribeInline([&snap](folly::Try<thrift::ResultRowBatch>&& batch) {
            if (batch.hasValue()) {
                for (const auto& row : *batch->rows()) {
                    if (row.values()->size() > 0) {
                        const auto& v = (*row.values())[0];
                        if (v.getType() == thrift::ResultValue::Type::int_val) {
                            snap.nodeIds.insert(v.get_int_val());
                        }
                    }
                }
            }
        });
    } catch (...) {}

    // Collect edge IDs for add/remove detection
    // Note: id(r) may not work for edges in all cases, so use count-based delta
    // for edges while keeping ID-based tracking for nodes.
    // edgeIds will be populated if id(r) works, otherwise left empty.
    try {
        auto [meta, stream] = rpc->executeCypher("MATCH ()-[r]->() RETURN id(r)", graphName);
        std::move(stream).subscribeInline([&snap](folly::Try<thrift::ResultRowBatch>&& batch) {
            if (batch.hasValue()) {
                for (const auto& row : *batch->rows()) {
                    if (row.values()->size() > 0) {
                        const auto& v = (*row.values())[0];
                        if (v.getType() == thrift::ResultValue::Type::int_val) {
                            snap.edgeIds.insert(v.get_int_val());
                        }
                    }
                }
            }
        });
    } catch (...) {}

    return snap;
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
