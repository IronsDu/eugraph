#include "query/evaluator/expression_evaluator.hpp"

#include "query/planner/bound_expression/bound_list.hpp"

namespace eugraph {
namespace compute {

ExpressionEvaluator::EvalResult ExpressionEvaluator::evalList(const binder::BoundList& list, const DataChunk& input) {
    size_t count = input.numRows();

    // Evaluate each element expression once per chunk, not once per (row,
    // element). The old shape called evaluateInternal() inside the row loop;
    // for a literal list of N elements over R rows it performed R*N evaluations
    // and appended R*N temp columns.
    auto& s = scratch();
    auto& entry_cols = s.entry_cols;
    entry_cols.clear();
    entry_cols.reserve(list.elements.size());
    for (const auto& elem : list.elements)
        entry_cols.push_back(evaluateInternal(elem, input).column);

    auto& col = acquireTempColumn(binder::BoundTypeKind::LIST, count);
    for (size_t i = 0; i < count; ++i) {
        ListValue lv;
        lv.elements.reserve(entry_cols.size());
        for (const Column* entry : entry_cols) {
            if (entry && !entry->isNull(i)) {
                lv.elements.push_back(ValueStorage{entry->getValue(i)});
            } else {
                lv.elements.push_back(ValueStorage{Value{}});
            }
        }
        col.setValue(i, Value(mk<ListValue>(std::move(lv))));
    }
    return {&col, true};
}

} // namespace compute
} // namespace eugraph
