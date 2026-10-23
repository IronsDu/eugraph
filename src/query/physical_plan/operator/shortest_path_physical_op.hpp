#pragma once

#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"
#include "query/parser/ast.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "storage/data/i_async_graph_data_store.hpp"

#include <folly/coro/AsyncGenerator.h>
#include <folly/coro/Task.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eugraph {
namespace compute {

/// shortestPath / allShortestPaths execution.
///
/// Kept separate from VarLenExpandPhysicalOp: shortest-path search has a
/// different algorithm, different output cardinality, and different
/// performance characteristics from variable-length path enumeration.
class ShortestPathPhysicalOp : public PhysicalOperator {
public:
    ShortestPathPhysicalOp(std::string src_var, std::string dst_var,
                           std::optional<std::vector<EdgeLabelId>> label_filters,
                           cypher::RelationshipDirection direction, int64_t min_hops, int64_t max_hops,
                           IAsyncGraphDataStore& store, Schema input_schema, std::unique_ptr<PhysicalOperator> child,
                           std::string path_var, binder::SlotId path_slot, bool all_shortest_paths = false,
                           std::vector<LabelId> dst_label_ids = {}, bool dst_label_missing = false,
                           uint32_t src_filter_index_id = 0, PropertyValue src_filter_value = {},
                           uint32_t dst_filter_index_id = 0, PropertyValue dst_filter_value = {});

    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override;
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }
    void deriveOutputLayout(const TupleSlotLayout& input_layout) override;

private:
    struct EdgeLink {
        // Traversal direction (from -> to); physical src/dst may be opposite
        // for a backward search on an incoming adjacency.
        VertexId from = INVALID_VERTEX_ID;
        VertexId to = INVALID_VERTEX_ID;
        VertexId src = INVALID_VERTEX_ID;
        VertexId dst = INVALID_VERTEX_ID;
        EdgeId edge_id = INVALID_EDGE_ID;
        EdgeLabelId edge_label_id = INVALID_EDGE_LABEL_ID;
        uint64_t seq = 0;
    };
    struct Neighbor {
        VertexId id = INVALID_VERTEX_ID;
        EdgeLink link;
    };

    folly::coro::Task<std::vector<Neighbor>> scan(VertexId vid, Direction scan_dir);
    folly::coro::Task<std::optional<PathValue>> shortestBidirectional(VertexId start, VertexId end);
    folly::coro::Task<std::vector<PathValue>> allShortestPaths(VertexId start, VertexId end);
    folly::coro::Task<bool> hasDstLabels(VertexId vid);

    static PathValue buildPath(VertexId start, const std::vector<EdgeLink>& links);
    static std::string pathKey(const std::vector<EdgeLink>& links);

    std::string src_var_;
    std::string dst_var_;
    std::optional<std::vector<EdgeLabelId>> label_filters_;
    std::vector<std::optional<EdgeLabelId>> scan_filters_;
    cypher::RelationshipDirection direction_;
    Direction forward_dir_;
    Direction backward_dir_;
    int64_t min_hops_;
    int64_t max_hops_;
    IAsyncGraphDataStore& store_;
    Schema input_schema_;
    std::unique_ptr<PhysicalOperator> child_;
    std::string path_var_;
    binder::SlotId path_slot_ = binder::INVALID_SLOT_ID;
    bool all_shortest_paths_ = false;
    std::vector<LabelId> dst_label_ids_;
    bool dst_label_missing_ = false;
    uint32_t src_filter_index_id_ = 0;
    PropertyValue src_filter_value_;
    uint32_t dst_filter_index_id_ = 0;
    PropertyValue dst_filter_value_;
    int src_col_idx_ = -1;
    int dst_col_idx_ = -1;
    std::unordered_map<uint64_t, std::vector<Neighbor>> edge_cache_;
    std::unordered_map<VertexId, bool> dst_label_cache_;
};

} // namespace compute
} // namespace eugraph
