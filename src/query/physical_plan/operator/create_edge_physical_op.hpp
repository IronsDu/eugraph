#pragma once

#include "common/types/graph_types.hpp"
#include "query/physical_plan/expression_compiler.hpp"
#include "query/physical_plan/physical_operator_base.hpp"
#include "query/planner/bound_expression/bound_expression.hpp"
#include "storage/data/i_async_graph_data_store.hpp"
#include "storage/meta/i_async_graph_meta_store.hpp"

#include <folly/coro/AsyncGenerator.h>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eugraph {
namespace compute {

class CreateEdgePhysicalOp : public PhysicalOperator {
public:
    using PropExprs = std::vector<std::pair<uint16_t, binder::BoundExpression>>;

    CreateEdgePhysicalOp(std::string variable, size_t src_col_idx, size_t dst_col_idx,
                         std::optional<EdgeLabelId> label_id, PropExprs prop_exprs, IAsyncGraphDataStore& store,
                         IAsyncGraphMetaStore& meta, std::unordered_map<EdgeLabelId, EdgeLabelDef>& edge_label_defs,
                         std::optional<std::string> label_name,
                         const std::unordered_map<std::string, EdgeLabelId>& edge_label_name_to_id,
                         std::vector<std::pair<std::string, binder::BoundExpression>> pending_props,
                         std::unique_ptr<PhysicalOperator> child)
        : variable_(std::move(variable)), src_col_idx_(src_col_idx), dst_col_idx_(dst_col_idx), label_id_(label_id),
          prop_exprs_(std::move(prop_exprs)), store_(store), meta_(meta), edge_label_defs_(edge_label_defs),
          label_name_(std::move(label_name)), edge_label_name_to_id_(edge_label_name_to_id),
          pending_props_(std::move(pending_props)), child_(std::move(child)) {}

    folly::coro::AsyncGenerator<RowBatch> execute() override {
        return executeViaChunk();
    }
    folly::coro::AsyncGenerator<DataChunk> executeChunk() override;
    std::string toString() const override {
        return "CreateEdge(variable=" + variable_ + ", src_col=" + std::to_string(src_col_idx_) +
               ", dst_col=" + std::to_string(dst_col_idx_) + ")";
    }
    std::vector<const PhysicalOperator*> children() const override {
        return {child_.get()};
    }

    void compileExpressions(const TupleSlotLayout& input_layout) override {
        ExpressionCompiler compiler(input_layout);
        for (auto& [pid, expr] : prop_exprs_)
            compiler.compile(expr);
        for (auto& [name, expr] : pending_props_)
            compiler.compile(expr);
    }

private:
    std::string variable_;
    size_t src_col_idx_;
    size_t dst_col_idx_;
    std::optional<EdgeLabelId> label_id_;
    PropExprs prop_exprs_;
    IAsyncGraphDataStore& store_;
    IAsyncGraphMetaStore& meta_;
    std::unordered_map<EdgeLabelId, EdgeLabelDef>& edge_label_defs_;
    std::optional<std::string> label_name_;
    const std::unordered_map<std::string, EdgeLabelId>& edge_label_name_to_id_;
    std::vector<std::pair<std::string, binder::BoundExpression>> pending_props_;
    std::unique_ptr<PhysicalOperator> child_;
};

} // namespace compute
} // namespace eugraph
