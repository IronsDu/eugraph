#pragma once

#include "common/schema/graph_schema.hpp"
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
                          std::shared_ptr<const GraphSchema> schema = nullptr, LabelId anon_label_id = INVALID_LABEL_ID,
                          std::unordered_map<LabelId, std::vector<uint16_t>> /*label_prop_ids*/ = {},
                          std::vector<LabelId> candidate_labels = {})
        : variable_(std::move(variable)), output_types_(std::move(output_types)), store_(store), label_map_(label_map),
          schema_(std::move(schema)), anon_label_id_(anon_label_id), candidate_labels_(std::move(candidate_labels)) {}

    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        std::string labels;
        for (size_t i = 0; i < candidate_labels_.size(); ++i) {
            if (i > 0)
                labels += ":";
            auto it = schema_ ? schema_->labels.find(candidate_labels_[i])
                              : std::unordered_map<LabelId, LabelDef>::const_iterator{};
            labels += (schema_ && it != schema_->labels.end()) ? it->second.name : std::to_string(candidate_labels_[i]);
        }
        return labels.empty() ? "AllNodeScan(variable=" + variable_ + ")"
                              : "AllNodeScan(variable=" + variable_ + ", labels=" + labels + ")";
    }

    void explain(ExplainNode& node, const ExplainContext& ctx) const override {
        ExplainContext local = ctx;
        if (local.schema == nullptr)
            local.schema = schema_.get();
        node.operator_name = "AllNodeScan";
        node.details.push_back("variable=" + variable_);
        if (!candidate_labels_.empty())
            node.details.push_back("labels=" + explainLabelList(candidate_labels_, local));
    }

private:
    std::string variable_;
    std::vector<binder::BoundType> output_types_;
    IAsyncGraphDataStore& store_;
    const std::unordered_map<std::string, LabelId>& label_map_;
    std::shared_ptr<const GraphSchema> schema_;
    LabelId anon_label_id_ = INVALID_LABEL_ID;
    std::vector<LabelId> candidate_labels_;
};

} // namespace compute
} // namespace eugraph
