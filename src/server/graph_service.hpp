#pragma once

#include "common/types/graph_types.hpp"
#include "query/executor/query_executor.hpp"
#include "storage/graph_manager.hpp"

#include <folly/coro/Task.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace server {

/// Protocol-agnostic context for a Cypher query execution.
/// Protocol handlers wrap the AsyncGenerator<DataChunk> with their own
/// wire-format serialization.
struct CypherExecutionContext {
    std::shared_ptr<compute::StreamContext> ctx;
    std::unordered_map<LabelId, LabelDef> label_defs;
    std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_defs;
};

/// Protocol-agnostic service layer shared by Thrift and Bolt handlers.
/// Wraps GraphManager and provides business logic using only internal types.
/// Protocol handlers convert wire-format types at their boundary and
/// delegate to this service.
class GraphService {
public:
    explicit GraphService(GraphManager& gm) : gm_(gm) {}

    GraphInstance* resolveGraph(const std::string& name);

    // Graph lifecycle
    GraphEntry createGraph(const std::string& name);
    bool dropGraph(const std::string& name);
    std::vector<GraphEntry> listGraphs();

    // DDL
    folly::coro::Task<LabelDef> createLabel(const std::string& name, const std::vector<PropertyDef>& properties,
                                            const std::string& graph_name);

    folly::coro::Task<std::vector<LabelDef>> listLabels(const std::string& graph_name);

    folly::coro::Task<EdgeLabelDef> createEdgeLabel(const std::string& name, const std::vector<PropertyDef>& properties,
                                                    const std::string& graph_name);

    folly::coro::Task<std::vector<EdgeLabelDef>> listEdgeLabels(const std::string& graph_name);

    /// Execute a Cypher query with pre-parsed parameters.
    /// Returns both the StreamContext and label/edge-label definitions
    /// needed for result value serialization.
    folly::coro::Task<CypherExecutionContext> executeCypher(const std::string& query,
                                                            const std::unordered_map<std::string, Value>& params,
                                                            const std::string& graph_name);

    /// Batch insert vertices. entries[i].props corresponds to label property positions.
    struct BatchVertexEntry {
        VertexId vid;
        std::vector<PropertyValue> props;
    };

    folly::coro::Task<std::vector<VertexId>> batchInsertVertices(const std::string& label_name,
                                                                 std::vector<BatchVertexEntry> entries,
                                                                 const std::string& graph_name);

    struct BatchEdgeEntry {
        EdgeId eid;
        VertexId src_id;
        VertexId dst_id;
        uint64_t seq;
        std::vector<PropertyValue> props;
    };

    folly::coro::Task<int32_t> batchInsertEdges(const std::string& edge_label_name, std::vector<BatchEdgeEntry> entries,
                                                const std::string& graph_name);

private:
    GraphManager& gm_;
};

} // namespace server
} // namespace eugraph
