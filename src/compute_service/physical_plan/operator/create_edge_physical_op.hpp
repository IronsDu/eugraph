#pragma once

#include "common/types/graph_types.hpp"
#include "compute_service/physical_plan/physical_operator_base.hpp"
#include "storage/data/i_async_graph_data_store.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace eugraph {
namespace compute {

class CreateEdgePhysicalOp : public PhysicalOperator {
public:
    CreateEdgePhysicalOp(std::string variable, VertexId src_id, VertexId dst_id, EdgeLabelId label_id,
                         EdgeId assigned_eid, Properties props, IAsyncGraphDataStore& store,
                         std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_defs,
                         std::unique_ptr<PhysicalOperator> child)
        : variable_(std::move(variable)), src_id_(src_id), dst_id_(dst_id), label_id_(label_id),
          assigned_eid_(assigned_eid), props_(std::move(props)), store_(store),
          edge_label_defs_(std::move(edge_label_defs)), child_(std::move(child)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override;
    std::string toString() const override {
        return "CreateEdge(variable=" + variable_ + ", src=" + std::to_string(src_id_) +
               ", dst=" + std::to_string(dst_id_) + ")";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

private:
    std::string variable_;
    VertexId src_id_;
    VertexId dst_id_;
    EdgeLabelId label_id_;
    EdgeId assigned_eid_;
    Properties props_;
    IAsyncGraphDataStore& store_;
    std::unordered_map<EdgeLabelId, EdgeLabelDef> edge_label_defs_;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
