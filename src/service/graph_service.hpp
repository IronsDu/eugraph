#pragma once

#include "common/types/graph_types.hpp"
#include "query/executor/query_executor.hpp"
#include "query/parser/database_ddl_parser.hpp"
#include "storage/graph_manager.hpp"

#include <folly/coro/Task.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace service {

/// Protocol-agnostic context for a Cypher query execution.
/// Protocol handlers wrap the AsyncGenerator<DataChunk> with their own
/// wire-format serialization.
struct CypherExecutionContext {
    std::shared_ptr<compute::StreamContext> ctx;
    std::unordered_map<LabelId, LabelDef> label_defs;
    std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_defs;
    std::string switched_database; // non-empty when USE <graph> was executed
};

/// Protocol-agnostic service layer shared by Thrift and Bolt handlers.
/// Wraps GraphManager and provides business logic using only internal types.
/// Protocol handlers convert wire-format types at their boundary and
/// delegate to this service.
class GraphService {
public:
    explicit GraphService(GraphManager& gm) : gm_(gm) {}

    /// 图实例**租约**：持有期间禁止该图被 DROP（DROP 会关闸并等在飞租约归零后再关 WT 连接）。
    /// 调用方必须把租约作为局部变量持有到本次操作结束（协程里即整条语句的生命周期）。
    struct GraphLease {
        GraphInstance* inst = nullptr;
        GraphUsageGate::Guard guard;
        explicit operator bool() const {
            return inst != nullptr;
        }
        GraphInstance* operator->() const {
            return inst;
        }
    };
    GraphLease resolveGraph(const std::string& name);
    /// Executor used for Cypher evaluation; Bolt uses it to run query
    /// coroutines off the socket EventBase.
    folly::Executor* computeExecutor();

    // Graph lifecycle
    GraphEntry createGraph(const std::string& name);
    bool dropGraph(const std::string& name);
    std::vector<GraphEntry> listGraphs();

    // DDL
    /// pk_props = 主键属性名（有序）；空 = 无主键。
    /// merge_properties 非空时对已存在的标签做**增量加属性**（同名属性跳过、类型冲突报错）。
    /// 对应 neo4j-admin import 的语义：同名标签可来自多个文件，属性取并集。
    folly::coro::Task<LabelDef> createLabel(const std::string& name, const std::vector<PropertyDef>& properties,
                                            const std::string& graph_name,
                                            const std::vector<std::string>& pk_props = {},
                                            const std::vector<PropertyDef>& merge_properties = {});

    folly::coro::Task<std::vector<LabelDef>> listLabels(const std::string& graph_name);

    folly::coro::Task<EdgeLabelDef> createEdgeLabel(const std::string& name, const std::vector<PropertyDef>& properties,
                                                    const std::string& graph_name);

    folly::coro::Task<std::vector<EdgeLabelDef>> listEdgeLabels(const std::string& graph_name);

    /// Execute a Cypher query with pre-parsed parameters.
    /// Returns both the StreamContext and label/edge-label definitions
    /// needed for result value serialization.
    /// `cancel` (optional) lets the protocol layer stop the statement when the client
    /// goes away; it lives in the statement's QueryContext and the operators check it
    /// as they consume each upstream chunk.
    folly::coro::Task<CypherExecutionContext> executeCypher(const std::string& query,
                                                            const std::unordered_map<std::string, Value>& params,
                                                            const std::string& graph_name,
                                                            compute::QueryCancel cancel = nullptr);

    /// Batch insert vertices. entries[i].props corresponds to primary label
    /// property positions; entries[i].extra_labels are added as pure labels
    /// (empty property set). The primary label is the batch label_name.
    /// 端点主键值（有序，顺序与标签的主键声明一致）
    using PkValues = std::vector<PropertyValue>;

    struct BatchVertexEntry {
        VertexId vid;
        std::vector<PropertyValue> props;
        std::vector<std::string> extra_labels;
        /// 该顶点的主键；空 = 无主键（不可被边引用）
        PkValues pk;
    };

    /// 边端点引用：目标顶点标签 + 主键值。VertexId 不出现在 RPC 协议里。
    struct BatchEdgeEndpoint {
        std::string label;
        PkValues pk;
        bool valid() const {
            return !label.empty() && !pk.empty();
        }
    };

    struct BatchInsertVerticesOutcome {
        std::vector<VertexId> vertex_ids; ///< 实际写入的顶点 id（与写入顺序一致）
        int32_t duplicate_pk = 0;         ///< 因主键已存在/批内重复而跳过的记录数（first-wins）
    };

    folly::coro::Task<BatchInsertVerticesOutcome> batchInsertVertices(const std::string& label_name,
                                                                      std::vector<BatchVertexEntry> entries,
                                                                      const std::string& graph_name);

    struct BatchEdgeEntry {
        EdgeId eid;
        BatchEdgeEndpoint src;
        BatchEdgeEndpoint dst;
        uint64_t seq;
        std::vector<PropertyValue> props;
    };

    /// 返回 {inserted, skipped_unresolved}：端点解析不到的边被跳过并计数。
    folly::coro::Task<std::pair<int32_t, int32_t>> batchInsertEdges(const std::string& edge_label_name,
                                                                    std::vector<BatchEdgeEntry> entries,
                                                                    const std::string& graph_name);

private:
    GraphManager& gm_;

    /// Handle a pre-parsed database-DDL statement.
    ///
    /// Takes the whole GraphInstance rather than just its data store because the
    /// DESCRIBE family is graph-scoped: it reads the *selected* graph's schema.
    /// Database-level statements (CREATE/DROP/SHOW DATABASE, USE) still resolve the
    /// default graph themselves, so passing the selected instance is safe for both.
    folly::coro::Task<CypherExecutionContext> handleDatabaseDdl(const DatabaseDdlStatement& stmt,
                                                                GraphInstance& instance);
};

} // namespace service
} // namespace eugraph
