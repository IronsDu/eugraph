#include "query/evaluator/expression_evaluator.hpp"

#include "query/planner/bound_expression/bound_map.hpp"

#include <vector>

namespace eugraph {
namespace compute {

void ExpressionEvaluator::evalMap(const binder::BoundMap& map, const DataChunk& input, Column& result, size_t count) {
    // Evaluate each entry expression ONCE for the whole chunk, then read it per
    // row. The previous shape called evaluateInternal() inside the row loop,
    // which is O(rows x entries) evaluations; every evaluation of a non-trivial
    // expression appends a fresh temp column, so a two-entry map over a
    // 1024-row chunk allocated ~2048 columns instead of 2. Measured on
    // complex-7's `collect({msg: message.id, likeTime: likeTime})`: 28.0M
    // allocations per query versus 1.7M for the same map holding a plain column
    // reference — 16x more work for the same result.
    //
    // Lifetime: acquireTempColumn appends to a std::deque that is never cleared
    // during a query, so the pointers stay valid for as long as we hold them.
    std::vector<const Column*> entry_cols;
    entry_cols.reserve(map.entries.size());
    for (const auto& [key, expr] : map.entries) {
        (void)key;
        entry_cols.push_back(evaluateInternal(expr, input).column);
    }

    for (size_t i = 0; i < count; ++i) {
        MapValue mv;
        mv.entries.reserve(map.entries.size());
        for (size_t e = 0; e < map.entries.size(); ++e) {
            const Column* col = entry_cols[e];
            if (col && !col->isNull(i)) {
                mv.entries.push_back({map.entries[e].first, ValueStorage{col->getValue(i)}});
            } else {
                mv.entries.push_back({map.entries[e].first, ValueStorage{Value{}}});
            }
        }
        result.setValue(i, Value(std::move(mv)));
    }
}

} // namespace compute
} // namespace eugraph
