#pragma once

#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"
#include "query/parser/ast.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_type.hpp"
#include "storage/data/i_async_graph_data_store.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eugraph {
namespace compute {

struct EdgeVisitKey {
    VertexId src_id;
    EdgeLabelId edge_label_id;
    VertexId dst_id;
    uint64_t seq;

    bool operator==(const EdgeVisitKey& o) const noexcept {
        return src_id == o.src_id && edge_label_id == o.edge_label_id && dst_id == o.dst_id && seq == o.seq;
    }
};

struct EdgeVisitKeyHash {
    size_t operator()(const EdgeVisitKey& k) const noexcept {
        size_t h = k.src_id;
        h ^= std::hash<uint64_t>{}(k.edge_label_id) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint64_t>{}(k.dst_id) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint64_t>{}(k.seq) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

class VarLenExpandPhysicalOp : public PhysicalOperator {
public:
    ~VarLenExpandPhysicalOp() override;

    VarLenExpandPhysicalOp(
        std::string src_var, std::string dst_var, std::optional<std::vector<EdgeLabelId>> label_filters,
        cypher::RelationshipDirection direction, int64_t min_hops, int64_t max_hops, IAsyncGraphDataStore& store,
        Schema input_schema, std::vector<binder::BoundType> output_types, std::unique_ptr<PhysicalOperator> child,
        std::unordered_map<LabelId, std::vector<uint16_t>> /*dst_label_prop_ids*/ = {}, std::string path_var = {},
        std::string edge_var = {},
        std::unordered_map<EdgeLabelId, std::vector<std::pair<uint16_t, PropertyValue>>> edge_prop_filters = {})
        : src_var_(std::move(src_var)), dst_var_(std::move(dst_var)), label_filters_(std::move(label_filters)),
          direction_(direction), min_hops_(min_hops), max_hops_(max_hops), store_(store),
          input_schema_(std::move(input_schema)), output_types_(std::move(output_types)), child_(std::move(child)),
          path_var_(std::move(path_var)), edge_var_(std::move(edge_var)),
          edge_prop_filters_(std::move(edge_prop_filters)) {
        for (size_t i = 0; i < input_schema_.size(); ++i) {
            if (input_schema_[i] == src_var_) {
                src_col_idx_ = static_cast<int>(i);
                break;
            }
        }
    }

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override;
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    std::string src_var_;
    std::string dst_var_;
    std::optional<std::vector<EdgeLabelId>> label_filters_;
    cypher::RelationshipDirection direction_;
    int64_t min_hops_;
    int64_t max_hops_;
    IAsyncGraphDataStore& store_;
    Schema input_schema_;
    std::vector<binder::BoundType> output_types_;
    int src_col_idx_ = -1;
    std::unique_ptr<PhysicalOperator> child_;
    std::string path_var_;
    std::string edge_var_;
    // P3: per-edge-label property filter [{prop_id: expected_value}]
    std::unordered_map<EdgeLabelId, std::vector<std::pair<uint16_t, PropertyValue>>> edge_prop_filters_;
};

} // namespace compute
} // namespace eugraph
