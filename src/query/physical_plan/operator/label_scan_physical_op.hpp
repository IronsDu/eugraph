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

class LabelScanPhysicalOp : public PhysicalOperator {
public:
    LabelScanPhysicalOp(std::string variable, std::vector<LabelId> label_ids,
                        std::vector<binder::BoundType> output_types, IAsyncGraphDataStore& store,
                        std::unordered_map<LabelId, LabelDef> label_defs, LabelId anon_label_id = INVALID_LABEL_ID,
                        std::unordered_map<LabelId, std::vector<uint16_t>> label_prop_ids = {})
        : variable_(std::move(variable)), label_ids_(std::move(label_ids)), output_types_(std::move(output_types)),
          store_(store), label_defs_(std::move(label_defs)), anon_label_id_(anon_label_id),
          label_prop_ids_(std::move(label_prop_ids)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        std::string label_name;
        for (size_t i = 0; i < label_ids_.size(); ++i) {
            if (i > 0)
                label_name += ":";
            auto it = label_defs_.find(label_ids_[i]);
            label_name += (it != label_defs_.end()) ? it->second.name : std::to_string(label_ids_[i]);
        }
        return "LabelScan(variable=" + variable_ + ", labels=" + label_name + ", " + describeProps() + ")";
    }

private:
    std::string describeProps() const {
        if (label_prop_ids_.empty())
            return "props=[]";
        // Build a summary across all labels
        std::string result = "props={";
        bool first_label = true;
        for (LabelId lid : label_ids_) {
            auto it = label_prop_ids_.find(lid);
            if (it == label_prop_ids_.end() || it->second.empty())
                continue;
            if (!first_label)
                result += ", ";
            first_label = false;
            auto def_it = label_defs_.find(lid);
            result += (def_it != label_defs_.end()) ? def_it->second.name : std::to_string(lid);
            result += ":[";
            for (size_t i = 0; i < it->second.size(); ++i) {
                if (i > 0)
                    result += ",";
                if (def_it != label_defs_.end()) {
                    bool found = false;
                    for (const auto& pd : def_it->second.properties) {
                        if (pd.id == it->second[i]) {
                            result += pd.name;
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                        result += std::to_string(it->second[i]);
                } else {
                    result += std::to_string(it->second[i]);
                }
            }
            result += "]";
        }
        result += "}";
        if (first_label)
            return "props=[]";
        return result;
    }

private:
    std::string variable_;
    std::vector<LabelId> label_ids_;
    std::vector<binder::BoundType> output_types_;
    IAsyncGraphDataStore& store_;
    std::unordered_map<LabelId, LabelDef> label_defs_;
    LabelId anon_label_id_ = INVALID_LABEL_ID;
    std::unordered_map<LabelId, std::vector<uint16_t>> label_prop_ids_;
};

} // namespace compute
} // namespace eugraph
