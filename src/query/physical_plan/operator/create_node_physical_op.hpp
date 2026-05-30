#pragma once

#include "common/types/graph_types.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"
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

class CreateNodePhysicalOp : public PhysicalOperator {
public:
    using PropExprs = std::vector<std::pair<uint16_t, binder::BoundExpression>>;

    CreateNodePhysicalOp(std::string variable, std::vector<LabelId> label_ids,
                         std::vector<std::pair<LabelId, PropExprs>> label_prop_exprs, IAsyncGraphDataStore& store,
                         IAsyncGraphMetaStore& meta, std::unique_ptr<PhysicalOperator> child,
                         std::unordered_map<LabelId, LabelDef>& label_defs,
                         std::vector<std::pair<std::string, binder::BoundExpression>> pending_props = {})
        : variable_(std::move(variable)), label_ids_(std::move(label_ids)),
          label_prop_exprs_(std::move(label_prop_exprs)), store_(store), meta_(meta), child_(std::move(child)),
          label_defs_(label_defs), pending_props_(std::move(pending_props)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override;
    std::vector<const PhysicalOperator*> children() const override {
        if (child_)
            return {child_.get()};
        return {};
    }

private:
    std::string variable_;
    std::vector<LabelId> label_ids_;
    std::vector<std::pair<LabelId, PropExprs>> label_prop_exprs_;
    IAsyncGraphDataStore& store_;
    IAsyncGraphMetaStore& meta_;
    std::unique_ptr<PhysicalOperator> child_;
    std::unordered_map<LabelId, LabelDef>& label_defs_;
    std::vector<std::pair<std::string, binder::BoundExpression>> pending_props_;

    bool anon_registered_ = false;
    std::vector<std::tuple<LabelId, uint16_t, binder::BoundExpression>> resolved_pending_;
};

} // namespace compute
} // namespace eugraph
