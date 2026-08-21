#include "query/physical_plan/operator/filter_physical_op.hpp"

#include "query/physical_plan/operator/cross_product_physical_op.hpp"

namespace eugraph {
namespace compute {

namespace {

void resolveCrossRightRefs(binder::BoundExpression& expr, const TupleSlotLayout& right_layout, uint32_t left_cols) {
    std::visit(
        [&](auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, binder::BoundColumnRef>) {
                if (val.name.empty()) {
                    int idx = right_layout.getColumnIndex(val.slot_id);
                    if (idx >= 0)
                        val.column_index = static_cast<uint32_t>(idx) + left_cols;
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryOp>>) {
                resolveCrossRightRefs(val->left, right_layout, left_cols);
                resolveCrossRightRefs(val->right, right_layout, left_cols);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnaryOp>>) {
                resolveCrossRightRefs(val->operand, right_layout, left_cols);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFunctionCall>>) {
                for (auto& arg : val->args)
                    resolveCrossRightRefs(arg, right_layout, left_cols);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>>) {
                resolveCrossRightRefs(val->object, right_layout, left_cols);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDynamicPropertyRef>>) {
                resolveCrossRightRefs(val->object, right_layout, left_cols);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundList>>) {
                for (auto& elem : val->elements)
                    resolveCrossRightRefs(elem, right_layout, left_cols);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCase>>) {
                if (val->subject.has_value())
                    resolveCrossRightRefs(*val->subject, right_layout, left_cols);
                for (auto& [w, t] : val->when_thens) {
                    resolveCrossRightRefs(w, right_layout, left_cols);
                    resolveCrossRightRefs(t, right_layout, left_cols);
                }
                if (val->else_expr.has_value())
                    resolveCrossRightRefs(*val->else_expr, right_layout, left_cols);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSubscript>>) {
                resolveCrossRightRefs(val->list, right_layout, left_cols);
                resolveCrossRightRefs(val->index, right_layout, left_cols);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSlice>>) {
                resolveCrossRightRefs(val->list, right_layout, left_cols);
                if (val->from.has_value())
                    resolveCrossRightRefs(*val->from, right_layout, left_cols);
                if (val->to.has_value())
                    resolveCrossRightRefs(*val->to, right_layout, left_cols);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMap>>) {
                for (auto& [k, v] : val->entries)
                    resolveCrossRightRefs(v, right_layout, left_cols);
            }
        },
        expr);
}

} // namespace

void FilterPhysicalOp::compileExpressions(const TupleSlotLayout& input_layout) {
    if (auto* cp = dynamic_cast<CrossProductPhysicalOp*>(child_.get()))
        resolveCrossRightRefs(predicate_, cp->rightSlotLayout(), static_cast<uint32_t>(cp->leftColumnCount()));
    ExpressionCompiler compiler(input_layout);
    compiler.compile(predicate_);
}

folly::coro::AsyncGenerator<DataChunk> FilterPhysicalOp::executeChunk() {
    auto child_gen = child_->executeChunk();

    while (auto chunk = co_await child_gen.next()) {
        size_t n = chunk->numRows();

        VectorizedEvaluator evaluator(eval_ctx_);
        std::vector<bool> predicate(n);
        evaluator.evaluatePredicate(predicate_, *chunk, predicate);

        // Build filtered SelectionVector
        SelectionVector filtered;
        filtered.is_identity = false;
        filtered.indices.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            if (predicate[i]) {
                filtered.indices.push_back(static_cast<uint32_t>(i));
            }
        }
        filtered.count = filtered.indices.size();

        if (filtered.count == 0)
            continue;

        DataChunk output;
        output.columns.reserve(chunk->columns.size());
        for (auto& col : chunk->columns) {
            if ((col.form == VectorForm::FLAT || col.form == VectorForm::DICTIONARY) && col.buffer) {
                SelectionVector mapped;
                mapped.is_identity = false;
                mapped.indices.reserve(filtered.count);
                for (size_t i = 0; i < filtered.count; ++i) {
                    uint32_t physical = filtered[i];
                    if (col.form == VectorForm::DICTIONARY) {
                        mapped.indices.push_back(col.dict_sel[physical]);
                    } else {
                        mapped.indices.push_back(physical);
                    }
                }
                mapped.count = filtered.count;
                output.columns.push_back(Column::dict(col.buffer, mapped));
            } else if (col.form == VectorForm::CONSTANT) {
                output.columns.push_back(Column::constant(col.constant_value));
            } else {
                output.columns.push_back(Column(col.type));
            }
        }
        output.count = filtered.count;
        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
