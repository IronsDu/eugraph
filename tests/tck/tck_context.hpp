#pragma once

#include "tck_types.hpp"

#include "gen-cpp2/EuGraphService.h"
#include "program/shell/rpc_client.hpp"

#include <spdlog/spdlog.h>

#include <atomic>
#include <memory>
#include <regex>
#include <string>
#include <vector>

namespace eugraph::tck {

// Keywords for features that the query engine does not support yet.
// Scenarios whose test query contains any of these are skipped (pending).
inline const std::vector<std::string> kUnsupportedKeywords = {
    "DELETE",      "DETACH DELETE", "MERGE",       "CALL ",          "UNWIND", "UNION", "CASE",
    "ENDS WITH",   "STARTS WITH",   "CONTAINS",    "EXISTS",         "OPTIONAL MATCH", "XOR",
    " IN ",        "\\$",           "REMOVE",      "FOREACH",        "LOAD CSV",
    "SHORTESTPATH","apoc\\.",
};

// Unsupported syntax patterns that need regex matching
inline const std::vector<std::string> kUnsupportedSyntax = {
    // Multiple MATCH clauses
    R"(MATCH\s+\(.*\)\s+MATCH\s+\()",
    // Multiple pattern parts (comma-separated paths)
    R"(CREATE\s+\(.*\),\s*\(.*\),\s*\()",
    // Multi-label CREATE like CREATE (:A:B)
    R"(CREATE\s+\([^)]*:[^)]*:[^)]*\))",
    // EXISTS subquery (as opposed to EXISTS(n.prop))
    R"(EXISTS\s*\{)",
    // Relationship type alternation like [:A|B]
    R"(\[[^\]]*:[^\]]*\|[^\]]*\])",
};

// -------------------- Scenario context shared across steps --------------------

struct TckContext {
    // RPC connection
    static std::unique_ptr<shell::EuGraphRpcClient> rpc;
    static std::string serverHost;
    static int serverPort;

    // Per-scenario state
    std::string graphName;
    std::vector<std::string> lastColumns;           // column names from last query
    std::vector<std::vector<std::string>> lastRows; // TCK-formatted result rows
    bool lastQueryHadError = false;
    std::string lastErrorType;
    std::string lastErrorPhase;
    std::string lastErrorDetail;

    // Side-effect tracking
    GraphSnapshot snapshotBefore;

    // Side-effect results from last query
    SideEffects lastSideEffects;

    // Check whether a Cypher query uses unsupported syntax
    static bool isQuerySupported(const std::string& query) {
        std::string upper = query;
        // Check keyword patterns
        for (const auto& kw : kUnsupportedKeywords) {
            std::regex re(kw, std::regex::icase);
            if (std::regex_search(query, re)) {
                spdlog::info("[TCK] skipping: unsupported keyword '{}'", kw);
                return false;
            }
        }
        // Check syntax patterns
        for (const auto& pat : kUnsupportedSyntax) {
            std::regex re(pat, std::regex::icase);
            if (std::regex_search(query, re)) {
                spdlog::info("[TCK] skipping: unsupported pattern");
                return false;
            }
        }
        return true;
    }

    // ---------- RPC helpers ----------

    void connectRpc() {
        if (!rpc) {
            rpc = std::make_unique<shell::EuGraphRpcClient>(serverHost, serverPort);
            rpc->connect();
        }
    }

    thrift::GraphInfo createGraph(const std::string& name) {
        connectRpc();
        return rpc->createGraph(name);
    }

    bool dropGraph(const std::string& name) {
        if (!rpc)
            return false;
        try {
            return rpc->dropGraph(name);
        } catch (const std::exception& e) {
            spdlog::warn("[TCK] dropGraph failed: {}", e.what());
            return false;
        }
    }

    thrift::LabelInfo createLabel(const std::string& name, const std::vector<thrift::PropertyDefThrift>& props,
                                  const std::string& gname) {
        return rpc->createLabel(name, props, gname);
    }

    thrift::EdgeLabelInfo createEdgeLabel(const std::string& name, const std::vector<thrift::PropertyDefThrift>& props,
                                          const std::string& gname) {
        return rpc->createEdgeLabel(name, props, gname);
    }

    // ---------- Query execution ----------

    // Execute a Cypher query, store results in lastRows/lastColumns
    void executeQuery(const std::string& query);

    // Take a snapshot of the current graph (for side-effect computation)
    GraphSnapshot takeSnapshot();

    // Compute side effects by comparing before/after snapshots
    SideEffects computeSideEffects(const GraphSnapshot& before, const GraphSnapshot& after) {
        SideEffects se;
        se.nodes = after.nodeCount - before.nodeCount;
        se.relationships = after.edgeCount - before.edgeCount;
        se.labels = after.labelCount - before.labelCount;
        se.properties = after.propertyCount - before.propertyCount;
        return se;
    }

    // ---------- Value formatting (TCK format) ----------

    // Convert a thrift ResultValue to TCK format string
    static std::string formatValue(const thrift::ResultValue& val);

    // Convert JSON representations to TCK format
    static std::string convertVertexJson(const std::string& json);
    static std::string convertEdgeJson(const std::string& json);

    // ---------- Graph initialization ----------

    // Scan query text and pre-create any labels / edge types found
    void ensureTypesForQuery(const std::string& query);
};

} // namespace eugraph::tck
