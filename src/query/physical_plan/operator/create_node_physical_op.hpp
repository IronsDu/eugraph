#pragma once

#include "common/types/graph_types.hpp"
#include "query/physical_plan/expression_compiler.hpp"
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

class VectorizedEvaluator;

class CreateNodePhysicalOp : public PhysicalOperator {
public:
    using PropExprs = std::vector<std::pair<uint16_t, binder::BoundExpression>>;

    CreateNodePhysicalOp(std::string variable, std::vector<LabelId> label_ids,
                         std::vector<std::pair<LabelId, PropExprs>> label_prop_exprs, IAsyncGraphDataStore& store,
                         IAsyncGraphMetaStore& meta, std::unique_ptr<PhysicalOperator> child,
                         std::unordered_map<LabelId, LabelDef>& label_defs,
                         std::vector<std::pair<std::string, binder::BoundExpression>> pending_props = {},
                         std::vector<std::string> label_names = {})
        : variable_(std::move(variable)), label_ids_(std::move(label_ids)),
          label_prop_exprs_(std::move(label_prop_exprs)), store_(store), meta_(meta), child_(std::move(child)),
          label_defs_(label_defs), pending_props_(std::move(pending_props)), label_names_(std::move(label_names)) {}

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

    void compileExpressions(const TupleSlotLayout& input_layout) override {
        ExpressionCompiler compiler(input_layout);
        for (auto& [lid, exprs] : label_prop_exprs_)
            for (auto& [pid, expr] : exprs)
                compiler.compile(expr);
        for (auto& [name, expr] : pending_props_)
            compiler.compile(expr);
    }

private:
    // GCC 13.x hits an internal compiler error (build_special_member_call at
    // cp/call.cc:11096) when emitting certain coroutine frames under
    // -fsanitize=undefined. We split the prepare work across many small
    // coroutines (each with only a handful of local variables and a few
    // suspension points) so GCC can emit every frame successfully.
    folly::coro::Task<void> prepare_();
    folly::coro::Task<void> prepareLabels_();
    folly::coro::Task<void> ensureTables_();
    folly::coro::Task<void> prepareAnon_();
    folly::coro::Task<void> loadLabelDef_(LabelId lid);
    folly::coro::Task<void> createOrGetLabel_(const std::string& name, LabelId& out_lid);
    folly::coro::Task<void> registerPropOnLabel_(const std::string& label_name, const std::string& prop_name);
    folly::coro::Task<void> resolvePropOnLabel_(LabelId lid, const std::string& prop_name, LabelId& out_lid,
                                                uint16_t& out_pid);
    // Non-coroutine helper: keeps BoundExpression temporaries out of any
    // coroutine frame (GCC 13 ICE workaround).
    void appendPropExpr_(LabelId lid, uint16_t pid, binder::BoundExpression expr);
    std::vector<std::pair<LabelId, Properties>> buildLabelProps(VectorizedEvaluator& evaluator, const DataChunk* chunk,
                                                                size_t row_idx);
    folly::coro::Task<bool> insertVertex(VertexId vid, const std::vector<std::pair<LabelId, Properties>>& label_props);

    std::string variable_;
    std::vector<LabelId> label_ids_;
    std::vector<std::pair<LabelId, PropExprs>> label_prop_exprs_;
    IAsyncGraphDataStore& store_;
    IAsyncGraphMetaStore& meta_;
    std::unique_ptr<PhysicalOperator> child_;
    std::unordered_map<LabelId, LabelDef>& label_defs_;
    std::vector<std::pair<std::string, binder::BoundExpression>> pending_props_;
    std::vector<std::string> label_names_;

    std::atomic<bool> label_names_resolved_{false};
    bool anon_registered_ = false;
    std::vector<std::tuple<LabelId, uint16_t, binder::BoundExpression>> resolved_pending_;
};

} // namespace compute
} // namespace eugraph
