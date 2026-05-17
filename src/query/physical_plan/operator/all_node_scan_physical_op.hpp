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
                          std::unordered_map<LabelId, LabelDef> label_defs, LabelId anon_label_id = INVALID_LABEL_ID,
                          std::unordered_map<LabelId, std::vector<uint16_t>> label_prop_ids = {})
        : variable_(std::move(variable)), output_types_(std::move(output_types)), store_(store), label_map_(label_map),
          label_defs_(std::move(label_defs)), anon_label_id_(anon_label_id),
          label_prop_ids_(std::move(label_prop_ids)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return "AllNodeScan(variable=" + variable_ + ", " + describeProps() + ")";
    }

private:
    std::string describeProps() const {
        if (label_prop_ids_.empty()) {
            return "props=[]";
        }
        // Check if all labels have all their properties (i.e. ALL)
        bool is_all = true;
        std::vector<std::string> prop_names;
        for (const auto& [lid, prop_ids] : label_prop_ids_) {
            if (prop_ids.empty()) {
                is_all = false;
                continue;
            }
            auto def_it = label_defs_.find(lid);
            if (def_it != label_defs_.end()) {
                if (prop_ids.size() != def_it->second.properties.size()) {
                    is_all = false;
                }
                for (uint16_t pid : prop_ids) {
                    for (const auto& pd : def_it->second.properties) {
                        if (pd.id == pid) {
                            if (std::find(prop_names.begin(), prop_names.end(), pd.name) == prop_names.end()) {
                                prop_names.push_back(pd.name);
                            }
                            break;
                        }
                    }
                }
            }
        }
        if (is_all && !prop_names.empty()) {
            return "props=ALL";
        }
        if (prop_names.empty()) {
            return "props=[]";
        }
        std::string result = "props=[";
        for (size_t i = 0; i < prop_names.size(); ++i) {
            if (i > 0)
                result += ", ";
            result += prop_names[i];
        }
        result += "]";
        return result;
    }
    std::string variable_;
    std::vector<binder::BoundType> output_types_;
    IAsyncGraphDataStore& store_;
    const std::unordered_map<std::string, LabelId>& label_map_;
    std::unordered_map<LabelId, LabelDef> label_defs_;
    LabelId anon_label_id_ = INVALID_LABEL_ID;
    std::unordered_map<LabelId, std::vector<uint16_t>> label_prop_ids_;
};

} // namespace compute
} // namespace eugraph
