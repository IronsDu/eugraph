#pragma once

#include "common/types/graph_types.hpp"
#include "query/dataset/data_chunk.hpp"
#include "query/parser/ast.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_type.hpp"
#include "storage/data/i_async_graph_data_store.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace eugraph {
namespace compute {

class ExpandPhysicalOp : public PhysicalOperator {
public:
    ExpandPhysicalOp(std::string src_var, std::string dst_var, std::string edge_var,
                     std::optional<std::vector<EdgeLabelId>> label_filters, cypher::RelationshipDirection direction,
                     IAsyncGraphDataStore& store, Schema input_schema, std::vector<binder::BoundType> output_types,
                     std::unique_ptr<PhysicalOperator> child,
                     std::unordered_map<LabelId, std::vector<uint16_t>> dst_label_prop_ids = {},
                     std::vector<uint16_t> edge_prop_ids = {}, std::vector<LabelId> dst_label_ids = {},
                     bool dst_bound = false, bool edge_bound = false, int dst_col_idx = -1, int edge_col_idx = -1,
                     std::vector<EdgeLabelId> full_scan_labels = {}, bool full_edge_scan = false)
        : src_var_(std::move(src_var)), dst_var_(std::move(dst_var)), edge_var_(std::move(edge_var)),
          label_filters_(std::move(label_filters)), direction_(direction), store_(store),
          input_schema_(std::move(input_schema)), output_types_(std::move(output_types)), child_(std::move(child)),
          dst_label_prop_ids_(std::move(dst_label_prop_ids)), edge_prop_ids_(std::move(edge_prop_ids)),
          dst_label_ids_(std::move(dst_label_ids)), dst_bound_(dst_bound), edge_bound_(edge_bound),
          dst_col_idx_(dst_col_idx), edge_col_idx_(edge_col_idx), full_scan_labels_(std::move(full_scan_labels)),
          full_edge_scan_(full_edge_scan) {
        for (size_t i = 0; i < input_schema_.size(); ++i) {
            if (input_schema_[i] == src_var_) {
                src_col_idx_ = static_cast<int>(i);
                break;
            }
        }
        if (dst_bound_ && dst_col_idx_ < 0) {
            for (size_t i = 0; i < input_schema_.size(); ++i) {
                if (input_schema_[i] == dst_var_) {
                    dst_col_idx_ = static_cast<int>(i);
                    break;
                }
            }
        }
        if (edge_bound_ && edge_col_idx_ < 0) {
            for (size_t i = 0; i < input_schema_.size(); ++i) {
                if (input_schema_[i] == edge_var_) {
                    edge_col_idx_ = static_cast<int>(i);
                    break;
                }
            }
        }
    }

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override;

    /// Configure an allowed-destination filter. Instead of scanning every
    /// edge neighbour, Expand probes edges for the destination vertex ids
    /// resolved from `allowed values` via `index_id`.
    void setAllowedDstFilter(uint32_t index_id, EdgeLabelId edge_label) {
        allowed_dst_index_id_ = index_id;
        allowed_edge_label_ = edge_label;
    }

    void setAllowedDstValues(std::shared_ptr<std::vector<PropertyValue>> values) {
        allowed_dst_values_ = std::move(values);
    }
    void setLimitHint(size_t limit) override {
        limit_hint_ = limit_hint_.has_value() ? std::min(*limit_hint_, limit) : limit;
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    std::string src_var_;
    std::string dst_var_;
    std::string edge_var_;
    std::optional<std::vector<EdgeLabelId>> label_filters_;
    cypher::RelationshipDirection direction_;
    IAsyncGraphDataStore& store_;
    Schema input_schema_;
    std::vector<binder::BoundType> output_types_;
    int src_col_idx_ = -1;
    std::unique_ptr<PhysicalOperator> child_;
    std::unordered_map<LabelId, std::vector<uint16_t>> dst_label_prop_ids_;
    std::vector<uint16_t> edge_prop_ids_;
    std::vector<LabelId> dst_label_ids_;
    bool dst_bound_ = false;
    bool edge_bound_ = false;
    int dst_col_idx_ = -1;
    int edge_col_idx_ = -1;
    std::vector<EdgeLabelId> full_scan_labels_;
    bool full_edge_scan_ = false;

    uint32_t allowed_dst_index_id_ = 0;
    EdgeLabelId allowed_edge_label_ = INVALID_EDGE_LABEL_ID;
    std::shared_ptr<std::vector<PropertyValue>> allowed_dst_values_;
    std::optional<size_t> limit_hint_;
};

} // namespace compute
} // namespace eugraph
