#include "query/physical_plan/operator/filter_physical_op.hpp"

#include "query/physical_plan/operator/cross_product_physical_op.hpp"
#include "query/planner/binder/join_equality.hpp"

namespace eugraph {
namespace compute {

namespace {

enum class StaticTruth {
    Unknown,
    False,
    True
};

bool objectIsGraphEntity(const binder::BoundExpression& object) {
    const binder::BoundType* type = nullptr;
    if (auto* cref = std::get_if<binder::BoundColumnRef>(&object))
        type = &cref->type;
    else if (auto* vref = std::get_if<binder::BoundVariableRef>(&object))
        type = &vref->type;
    if (!type)
        return false;
    switch (type->kind) {
    case binder::BoundTypeKind::VERTEX:
    case binder::BoundTypeKind::EDGE:
    case binder::BoundTypeKind::VERTEX_REF:
    case binder::BoundTypeKind::EDGE_KEY:
        return true;
    default:
        return false;
    }
}

bool isStaticNullProperty(const binder::BoundExpression& expr) {
    if (auto* pr = std::get_if<std::unique_ptr<binder::BoundPropertyRef>>(&expr)) {
        return *pr && (*pr)->candidates.empty() && !(*pr)->property_name.empty() && objectIsGraphEntity((*pr)->object);
    }
    return false;
}

StaticTruth staticTruthOf(const binder::BoundExpression& expr) {
    if (auto* un = std::get_if<std::unique_ptr<binder::BoundUnaryOp>>(&expr)) {
        if (!*un)
            return StaticTruth::Unknown;
        if ((*un)->op == cypher::UnaryOperator::IS_NULL && isStaticNullProperty((*un)->operand))
            return StaticTruth::True;
        if ((*un)->op == cypher::UnaryOperator::IS_NOT_NULL && isStaticNullProperty((*un)->operand))
            return StaticTruth::False;
        if ((*un)->op == cypher::UnaryOperator::NOT) {
            auto inner = staticTruthOf((*un)->operand);
            if (inner == StaticTruth::True)
                return StaticTruth::False;
            if (inner == StaticTruth::False)
                return StaticTruth::True;
        }
        return StaticTruth::Unknown;
    }
    if (auto* bin = std::get_if<std::unique_ptr<binder::BoundBinaryOp>>(&expr)) {
        if (!*bin)
            return StaticTruth::Unknown;
        if ((*bin)->op == cypher::BinaryOperator::AND) {
            auto left = staticTruthOf((*bin)->left);
            auto right = staticTruthOf((*bin)->right);
            if (left == StaticTruth::False || right == StaticTruth::False)
                return StaticTruth::False;
            if (left == StaticTruth::True && right == StaticTruth::True)
                return StaticTruth::True;
        } else if ((*bin)->op == cypher::BinaryOperator::OR) {
            auto left = staticTruthOf((*bin)->left);
            auto right = staticTruthOf((*bin)->right);
            if (left == StaticTruth::True || right == StaticTruth::True)
                return StaticTruth::True;
            if (left == StaticTruth::False && right == StaticTruth::False)
                return StaticTruth::False;
        }
        return StaticTruth::Unknown;
    }
    return StaticTruth::Unknown;
}

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
    // Read-only statements can short-circuit predicates that are statically
    // false/true against the bind-time schema. This avoids executing a full
    // scan only to discard every row (e.g. a relationship property that no
    // edge label defines).
    if (eval_ctx_.allow_static_schema_pruning) {
        auto truth = staticTruthOf(predicate_);
        if (truth == StaticTruth::False)
            co_return;
        if (truth == StaticTruth::True) {
            auto child_gen = child_->executeChunk();
            while (auto chunk = co_await child_gen.next())
                co_yield std::move(*chunk);
            co_return;
        }
    }

    auto child_gen = child_->executeChunk();

    while (auto chunk = co_await child_gen.next()) {
        size_t n = chunk->numRows();

        ExpressionEvaluator evaluator(eval_ctx_);
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
