#pragma once

#include "common/types/graph_types.hpp"
#include "query/evaluator/vectorized_evaluator.hpp"
#include "query/parser/ast.hpp"
#include "query/physical_plan/operator/set_physical_op.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"
#include "storage/data/i_async_graph_data_store.hpp"
#include "storage/meta/i_async_graph_meta_store.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eugraph {
namespace compute {

struct MergeEdgeKey {
    VertexId src_id;
    VertexId dst_id;
    EdgeLabelId label_id;
    bool operator==(const MergeEdgeKey& o) const {
        return src_id == o.src_id && dst_id == o.dst_id && label_id == o.label_id;
    }
};

struct MergeEdgeKeyHash {
    size_t operator()(const MergeEdgeKey& k) const {
        return std::hash<uint64_t>{}(k.src_id) ^ (std::hash<uint64_t>{}(k.dst_id) << 1) ^
               (std::hash<uint16_t>{}(k.label_id) << 2);
    }
};

class MergePhysicalOp : public PhysicalOperator {
public:
    MergePhysicalOp(std::string start_var, bool start_pre_bound, std::vector<LabelId> start_labels,
                    std::vector<std::pair<uint16_t, binder::BoundExpression>> start_prop_filters,
                    std::vector<std::pair<std::string, binder::BoundExpression>> start_pending_props,
                    bool has_relationship, std::string edge_var, std::optional<EdgeLabelId> edge_label_id,
                    std::optional<std::string> edge_label_name, cypher::RelationshipDirection direction,
                    std::vector<std::pair<uint16_t, binder::BoundExpression>> edge_prop_filters,
                    std::vector<std::pair<std::string, binder::BoundExpression>> edge_pending_props,
                    std::string end_var, bool end_pre_bound, std::vector<LabelId> end_labels,
                    std::vector<std::pair<uint16_t, binder::BoundExpression>> end_prop_filters,
                    std::vector<std::pair<std::string, binder::BoundExpression>> end_pending_props,
                    std::optional<std::string> path_variable, std::vector<SetPhysicalOp::BoundSetItem> on_create_items,
                    std::vector<SetPhysicalOp::BoundSetItem> on_match_items, IAsyncGraphDataStore& store,
                    IAsyncGraphMetaStore& meta, std::unordered_map<LabelId, LabelDef>& label_defs,
                    const std::unordered_map<std::string, LabelId>& label_name_to_id,
                    std::unordered_map<EdgeLabelId, EdgeLabelDef>& edge_label_defs,
                    const std::unordered_map<std::string, EdgeLabelId>& edge_label_name_to_id,
                    std::unique_ptr<PhysicalOperator> child);

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override;
    std::vector<const PhysicalOperator*> children() const override;

private:
    // Pattern data
    std::string start_var_;
    bool start_pre_bound_;
    std::vector<LabelId> start_labels_;
    std::vector<std::pair<uint16_t, binder::BoundExpression>> start_prop_filters_;
    std::vector<std::pair<std::string, binder::BoundExpression>> start_pending_props_;

    bool has_relationship_;
    std::string edge_var_;
    std::optional<EdgeLabelId> edge_label_id_;
    std::optional<std::string> edge_label_name_;
    cypher::RelationshipDirection direction_;
    std::vector<std::pair<uint16_t, binder::BoundExpression>> edge_prop_filters_;
    std::vector<std::pair<std::string, binder::BoundExpression>> edge_pending_props_;

    std::string end_var_;
    bool end_pre_bound_;
    std::vector<LabelId> end_labels_;
    std::vector<std::pair<uint16_t, binder::BoundExpression>> end_prop_filters_;
    std::vector<std::pair<std::string, binder::BoundExpression>> end_pending_props_;

    std::optional<std::string> path_variable_;

    // ON CREATE / ON MATCH SET items
    std::vector<SetPhysicalOp::BoundSetItem> on_create_items_;
    std::vector<SetPhysicalOp::BoundSetItem> on_match_items_;

    // Store references
    IAsyncGraphDataStore& store_;
    IAsyncGraphMetaStore& meta_;
    std::unordered_map<LabelId, LabelDef>& label_defs_;
    const std::unordered_map<std::string, LabelId>& label_name_to_id_;
    std::unordered_map<EdgeLabelId, EdgeLabelDef>& edge_label_defs_;
    const std::unordered_map<std::string, EdgeLabelId>& edge_label_name_to_id_;

    std::unique_ptr<PhysicalOperator> child_;

    // Read-own-writes: cross-batch tracking
    std::unordered_set<VertexId> created_vertices_;
    std::unordered_map<MergeEdgeKey, EdgeId, MergeEdgeKeyHash> created_edges_;

    // Cached label scan results (for label → vertex list)
    LabelId anon_label_id_ = INVALID_LABEL_ID;

    folly::coro::Task<std::optional<VertexId>>
    findMatchingNode(const std::vector<LabelId>& labels,
                     const std::vector<std::pair<uint16_t, binder::BoundExpression>>& prop_filters,
                     const std::vector<std::pair<std::string, binder::BoundExpression>>& pending_props,
                     const DataChunk* chunk, size_t row_idx, VectorizedEvaluator& evaluator);

    folly::coro::Task<VertexId>
    createNode(const std::vector<LabelId>& labels,
               const std::vector<std::pair<uint16_t, binder::BoundExpression>>& prop_filters,
               const std::vector<std::pair<std::string, binder::BoundExpression>>& pending_props,
               const DataChunk* chunk, size_t row_idx, VectorizedEvaluator& evaluator);

    folly::coro::Task<std::optional<std::tuple<EdgeId, EdgeLabelId>>>
    findMatchingEdge(VertexId src_vid, VertexId dst_vid,
                     const std::vector<std::pair<uint16_t, binder::BoundExpression>>& prop_filters,
                     const DataChunk* chunk, size_t row_idx, VectorizedEvaluator& evaluator);

    folly::coro::Task<std::tuple<EdgeId, EdgeLabelId>>
    createEdge(VertexId src_vid, VertexId dst_vid,
               const std::vector<std::pair<uint16_t, binder::BoundExpression>>& prop_filters,
               const std::vector<std::pair<std::string, binder::BoundExpression>>& pending_props,
               const DataChunk* chunk, size_t row_idx, VectorizedEvaluator& evaluator);

    folly::coro::Task<void> executeSetItems(const std::vector<SetPhysicalOp::BoundSetItem>& items,
                                            const DataChunk* chunk, size_t row_idx, const DataChunk& merged_chunk,
                                            VectorizedEvaluator& evaluator);

    folly::coro::Task<void> ensureLabelTables(const std::vector<LabelId>& labels);
    folly::coro::Task<void> ensureEdgeLabelTable(EdgeLabelId elid);
    folly::coro::Task<void>
    registerPendingProps(const std::vector<std::pair<std::string, binder::BoundExpression>>& start_pending,
                         const std::vector<std::pair<std::string, binder::BoundExpression>>& end_pending,
                         const std::vector<std::pair<std::string, binder::BoundExpression>>& edge_pending,
                         const std::vector<SetPhysicalOp::BoundSetItem>& on_create_items,
                         const std::vector<SetPhysicalOp::BoundSetItem>& on_match_items);

    bool comparePropertyValue(const PropertyValue& stored, const Value& expected);
};

} // namespace compute
} // namespace eugraph
