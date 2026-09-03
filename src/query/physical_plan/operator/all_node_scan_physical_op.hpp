#pragma once

#include "common/types/graph_types.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_type.hpp"
#include "storage/data/i_async_graph_data_store.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eugraph {
namespace compute {

class AllNodeScanPhysicalOp : public PhysicalOperator {
public:
    AllNodeScanPhysicalOp(std::string variable, std::vector<binder::BoundType> output_types,
                          IAsyncGraphDataStore& store, const std::unordered_map<std::string, LabelId>& label_map,
                          std::unordered_map<LabelId, LabelDef> label_defs = {},
                          LabelId anon_label_id = INVALID_LABEL_ID,
                          std::unordered_map<LabelId, std::vector<uint16_t>> /*label_prop_ids*/ = {},
                          std::vector<LabelId> candidate_labels = {})
        : variable_(std::move(variable)), output_types_(std::move(output_types)), store_(store), label_map_(label_map),
          label_defs_(std::move(label_defs)), anon_label_id_(anon_label_id),
          candidate_labels_(std::move(candidate_labels)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        std::string labels;
        for (size_t i = 0; i < candidate_labels_.size(); ++i) {
            if (i > 0)
                labels += ":";
            auto it = label_defs_.find(candidate_labels_[i]);
            labels += (it != label_defs_.end()) ? it->second.name : std::to_string(candidate_labels_[i]);
        }
        return labels.empty() ? "AllNodeScan(variable=" + variable_ + ")"
                              : "AllNodeScan(variable=" + variable_ + ", labels=" + labels + ")";
    }

private:
    std::string variable_;
    std::vector<binder::BoundType> output_types_;
    IAsyncGraphDataStore& store_;
    const std::unordered_map<std::string, LabelId>& label_map_;
    std::unordered_map<LabelId, LabelDef> label_defs_;
    LabelId anon_label_id_ = INVALID_LABEL_ID;
    std::vector<LabelId> candidate_labels_;
};

} // namespace compute
} // namespace eugraph
