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

// Unsupported features are now detected by parsing the query with
// CypherQueryParser and walking the AST. Two items have no AST
// representation and are kept as regex: FOREACH and LOAD CSV.

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

    // Parameterized query support
    std::map<std::string, std::string> pendingParams;

    // Check whether a Cypher query uses unsupported syntax.
    // Uses the Cypher parser AST for precise detection; falls back
    // to regex for FOREACH and LOAD CSV (no AST nodes exist).
    static bool isQuerySupported(const std::string& query);

    // ---------- RPC helpers ----------

    void connectRpc() {
        if (!rpc) {
            rpc = std::make_unique<shell::EuGraphRpcClient>(serverHost, serverPort);
            rpc->connect();
        }
    }

    // Reconnect after connection loss. Returns false if reconnection fails.
    bool reconnectRpc() {
        rpc.reset();
        try {
            rpc = std::make_unique<shell::EuGraphRpcClient>(serverHost, serverPort);
            return rpc->connect();
        } catch (const std::exception& e) {
            spdlog::error("[TCK] Reconnection failed: {}", e.what());
            rpc.reset();
            return false;
        }
    }

    // Check if exception is a transport/connection error
    static bool isConnectionError(const std::string& msg) {
        return msg.find("Channel got EOF") != std::string::npos ||
               msg.find("Connection not open") != std::string::npos ||
               msg.find("TTransportException") != std::string::npos;
    }

    thrift::GraphInfo createGraph(const std::string& name) {
        connectRpc();
        try {
            return rpc->createGraph(name);
        } catch (const std::exception& e) {
            std::string msg = e.what();
            if (isConnectionError(msg)) {
                spdlog::critical("[TCK] [{}] Connection lost during createGraph — aborting: {}", graphName, msg);
                std::exit(1);
            }
            throw;
        }
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
        try {
            return rpc->createLabel(name, props, gname);
        } catch (const std::exception& e) {
            std::string msg = e.what();
            if (isConnectionError(msg)) {
                spdlog::critical("[TCK] [{}] Connection lost during createLabel — aborting: {}", graphName, msg);
                std::exit(1);
            }
            throw;
        }
    }

    thrift::EdgeLabelInfo createEdgeLabel(const std::string& name, const std::vector<thrift::PropertyDefThrift>& props,
                                          const std::string& gname) {
        try {
            return rpc->createEdgeLabel(name, props, gname);
        } catch (const std::exception& e) {
            std::string msg = e.what();
            if (isConnectionError(msg)) {
                spdlog::critical("[TCK] [{}] Connection lost during createEdgeLabel — aborting: {}", graphName, msg);
                std::exit(1);
            }
            throw;
        }
    }

    // ---------- Query execution ----------

    // Execute a Cypher query, store results in lastRows/lastColumns
    void executeQuery(const std::string& query);

    // Take a snapshot of the current graph (for side-effect computation)
    GraphSnapshot takeSnapshot();

    // Compute side effects by comparing before/after snapshots
    SideEffects computeSideEffects(const GraphSnapshot& before, const GraphSnapshot& after) {
        SideEffects se;
        // Nodes: use ID sets for accurate add/remove
        for (auto id : after.nodeIds) {
            if (!before.nodeIds.count(id))
                se.nodes++;
        }
        for (auto id : before.nodeIds) {
            if (!after.nodeIds.count(id))
                se.removed_nodes++;
        }
        // Edges: prefer ID sets when available, fall back to count delta
        bool hasEdgeIds = !before.edgeIds.empty() || !after.edgeIds.empty();
        if (hasEdgeIds) {
            for (auto id : after.edgeIds) {
                if (!before.edgeIds.count(id))
                    se.relationships++;
            }
            for (auto id : before.edgeIds) {
                if (!after.edgeIds.count(id))
                    se.removed_relationships++;
            }
        } else {
            // Fallback: use net delta from counts
            int64_t edge_delta = after.edgeCount - before.edgeCount;
            if (edge_delta >= 0) {
                se.relationships = edge_delta;
            } else {
                se.removed_relationships = -edge_delta;
            }
        }
        // Labels: use net delta, clamped to non-negative (TCK counts distinct names, not instances)
        se.labels = std::max<int64_t>(0, after.labelCount - before.labelCount);
        // Properties: use net delta (no property-level ID tracking)
        int64_t prop_delta = after.propertyCount - before.propertyCount;
        if (prop_delta >= 0) {
            se.properties = prop_delta;
        } else {
            se.removed_properties = -prop_delta;
        }
        return se;
    }

    // ---------- Graph initialization ----------

    // Scan query text and pre-create any labels / edge types found
    void ensureTypesForQuery(const std::string& query);
};

} // namespace eugraph::tck
