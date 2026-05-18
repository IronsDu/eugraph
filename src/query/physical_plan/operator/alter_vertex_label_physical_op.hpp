#pragma once

#include "common/types/graph_types.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "storage/meta/i_async_graph_meta_store.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace compute {

/// Adds missing properties to an existing vertex label at runtime.
/// Runs before CreateNodePhysicalOp, updates the shared maps so
/// CreateNodePhysicalOp can resolve the new property IDs.
class AlterVertexLabelPhysicalOp : public PhysicalOperator {
public:
    AlterVertexLabelPhysicalOp(std::string label_name, std::vector<std::pair<std::string, PropertyType>> prop_defs,
                               IAsyncGraphMetaStore& meta, std::unordered_map<LabelId, LabelDef>& defs,
                               std::unique_ptr<PhysicalOperator> child)
        : label_name_(std::move(label_name)), prop_defs_(std::move(prop_defs)), meta_(meta), defs_(defs),
          child_(std::move(child)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return "AlterVertexLabel(name=" + label_name_ + ")";
    }
    std::vector<const PhysicalOperator*> children() const override {
        if (child_)
            return {child_.get()};
        return {};
    }

private:
    std::string label_name_;
    std::vector<std::pair<std::string, PropertyType>> prop_defs_;
    IAsyncGraphMetaStore& meta_;
    std::unordered_map<LabelId, LabelDef>& defs_;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
