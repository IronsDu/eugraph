#include "query/evaluator/expression_evaluator.hpp"

#include "query/planner/bound_expression/bound_label_cast.hpp"

namespace eugraph {
namespace compute {

ExpressionEvaluator::EvalResult ExpressionEvaluator::evalLabelCast(const binder::BoundLabelCast& cast,
                                                                   const DataChunk& input) {
    size_t count = input.numRows();
    auto inner = evaluateInternal(cast.object, input);
    auto& col = acquireTempColumn(binder::BoundTypeKind::VERTEX, count);
    if (inner.column) {
        for (size_t i = 0; i < count; ++i) {
            Value value = inner.column->getValue(i);
            if (auto* vertex = std::get_if<VertexValuePtr>(&value); vertex && *vertex) {
                // n::Label is a scoped view: expose only the properties stored
                // under that label, never the sibling labels of a multi-label
                // vertex. Clone because published VertexValue payloads are
                // shared and must stay immutable.
                auto scoped = mk<VertexValue>(**vertex);
                scoped->properties.clear();
                auto it = (*vertex)->properties.find(cast.label_id);
                if (it != (*vertex)->properties.end())
                    scoped->properties.emplace(cast.label_id, it->second);
                col.setValue(i, Value(std::move(scoped)));
            } else {
                col.setValue(i, std::move(value));
            }
        }
    }
    return {&col, true};
}

} // namespace compute
} // namespace eugraph
