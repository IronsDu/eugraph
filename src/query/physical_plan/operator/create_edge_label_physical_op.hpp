#pragma once

#include "common/types/graph_types.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "storage/data/i_async_graph_data_store.hpp"
#include "storage/meta/i_async_graph_meta_store.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eugraph {
namespace compute {

/// Auto-creates a missing edge label (and optionally its properties) at runtime.
class CreateEdgeLabelPhysicalOp : public PhysicalOperator {
public:
    CreateEdgeLabelPhysicalOp(std::string label_name, std::vector<std::pair<std::string, PropertyType>> prop_defs,
                              IAsyncGraphMetaStore& meta, IAsyncGraphDataStore& store,
                              std::unordered_map<std::string, EdgeLabelId>& name_to_id,
                              std::unordered_map<EdgeLabelId, EdgeLabelDef>& defs,
                              std::unique_ptr<PhysicalOperator> child)
        : label_name_(std::move(label_name)), prop_defs_(std::move(prop_defs)), meta_(meta), store_(store),
          name_to_id_(name_to_id), defs_(defs), child_(std::move(child)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return "CreateEdgeLabel(name=" + label_name_ + ")";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    std::string label_name_;
    std::vector<std::pair<std::string, PropertyType>> prop_defs_;
    IAsyncGraphMetaStore& meta_;
    IAsyncGraphDataStore& store_;
    std::unordered_map<std::string, EdgeLabelId>& name_to_id_;
    std::unordered_map<EdgeLabelId, EdgeLabelDef>& defs_;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
