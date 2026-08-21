#include "query/physical_plan/operator/filter_physical_op.hpp"

#include "query/physical_plan/operator/cross_product_physical_op.hpp"
#include "query/planner/binder/join_equality.hpp"

namespace eugraph {
namespace compute {

namespace {

void resolveCrossEqualityRefs(binder::BoundExpression& expr, const TupleSlotLayout& left_layout,
                              const TupleSlotLayout& right_layout, const Schema& left_schema,
                              const Schema& right_schema, uint32_t left_cols) {
    std::visit(
        [&](auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, binder::BoundColumnRef>) {
                if (binder::isJoinEqualityLeft(val.name)) {
                    int idx = left_layout.getColumnIndex(val.slot_id);
                    if (idx < 0) {
                        std::string var = binder::joinEqualityVarName(val.name, binder::kJoinEqualityLeft);
                        for (size_t c = 0; c < left_schema.size(); ++c) {
                            if (left_schema[c] == var) {
                                idx = static_cast<int>(c);
                                break;
                            }
                        }
                    }
                    if (idx >= 0)
                        val.column_index = static_cast<uint32_t>(idx);
                } else if (binder::isJoinEqualityRight(val.name)) {
                    int idx = right_layout.getColumnIndex(val.slot_id);
                    if (idx < 0) {
                        std::string var = binder::joinEqualityVarName(val.name, binder::kJoinEqualityRight);
                        for (size_t c = 0; c < right_schema.size(); ++c) {
                            if (right_schema[c] == var) {
                                idx = static_cast<int>(c);
                                break;
                            }
                        }
                        if (idx >= 0)
                            idx += static_cast<int>(left_cols);
                    } else {
                        idx += static_cast<int>(left_cols);
                    }
                    if (idx >= 0)
                        val.column_index = static_cast<uint32_t>(idx);
                }
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundBinaryOp>>) {
                resolveCrossEqualityRefs(val->left, left_layout, right_layout, left_schema, right_schema, left_cols);
                resolveCrossEqualityRefs(val->right, left_layout, right_layout, left_schema, right_schema, left_cols);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundUnaryOp>>) {
                resolveCrossEqualityRefs(val->operand, left_layout, right_layout, left_schema, right_schema, left_cols);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundFunctionCall>>) {
                for (auto& arg : val->args)
                    resolveCrossEqualityRefs(arg, left_layout, right_layout, left_schema, right_schema, left_cols);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundPropertyRef>>) {
                resolveCrossEqualityRefs(val->object, left_layout, right_layout, left_schema, right_schema, left_cols);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundDynamicPropertyRef>>) {
                resolveCrossEqualityRefs(val->object, left_layout, right_layout, left_schema, right_schema, left_cols);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundList>>) {
                for (auto& elem : val->elements)
                    resolveCrossEqualityRefs(elem, left_layout, right_layout, left_schema, right_schema, left_cols);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundCase>>) {
                if (val->subject.has_value())
                    resolveCrossEqualityRefs(*val->subject, left_layout, right_layout, left_schema, right_schema,
                                             left_cols);
                for (auto& [w, t] : val->when_thens) {
                    resolveCrossEqualityRefs(w, left_layout, right_layout, left_schema, right_schema, left_cols);
                    resolveCrossEqualityRefs(t, left_layout, right_layout, left_schema, right_schema, left_cols);
                }
                if (val->else_expr.has_value())
                    resolveCrossEqualityRefs(*val->else_expr, left_layout, right_layout, left_schema, right_schema,
                                             left_cols);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSubscript>>) {
                resolveCrossEqualityRefs(val->list, left_layout, right_layout, left_schema, right_schema, left_cols);
                resolveCrossEqualityRefs(val->index, left_layout, right_layout, left_schema, right_schema, left_cols);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundSlice>>) {
                resolveCrossEqualityRefs(val->list, left_layout, right_layout, left_schema, right_schema, left_cols);
                if (val->from.has_value())
                    resolveCrossEqualityRefs(*val->from, left_layout, right_layout, left_schema, right_schema,
                                             left_cols);
                if (val->to.has_value())
                    resolveCrossEqualityRefs(*val->to, left_layout, right_layout, left_schema, right_schema, left_cols);
            } else if constexpr (std::is_same_v<T, std::unique_ptr<binder::BoundMap>>) {
                for (auto& [k, v] : val->entries)
                    resolveCrossEqualityRefs(v, left_layout, right_layout, left_schema, right_schema, left_cols);
            }
        },
        expr);
}

} // namespace

void FilterPhysicalOp::compileExpressions(const TupleSlotLayout& input_layout) {
    if (auto* cp = dynamic_cast<CrossProductPhysicalOp*>(child_.get()))
        resolveCrossEqualityRefs(predicate_, cp->leftSlotLayout(), cp->rightSlotLayout(), cp->leftOutputSchema(),
                                 cp->rightOutputSchema(), static_cast<uint32_t>(cp->leftColumnCount()));
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
