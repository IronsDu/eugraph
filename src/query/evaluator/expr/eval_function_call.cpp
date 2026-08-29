#include "query/evaluator/columnar_kernels.hpp"
#include "query/evaluator/expression_evaluator.hpp"

namespace eugraph {
namespace compute {
using namespace eugraph::compute::detail;

void ExpressionEvaluator::evalFunctionCall(const binder::BoundFunctionCall& fc, const DataChunk& input, Column& result,
                                           size_t count) {
    if (!fc.func_def)
        return;

    // Aggregate substitution: when set (used by AggregatePhysicalOp output phase),
    // a function call whose func_def matches a key is replaced with the pre-computed Value.
    if (aggregate_substitutions) {
        auto it = aggregate_substitutions->find(fc.func_def);
        if (it != aggregate_substitutions->end()) {
            for (size_t i = 0; i < count; ++i)
                result.setValue(i, it->second);
            return;
        }
    }

    // Evaluate all arguments first
    std::vector<EvalResult> arg_results;
    arg_results.reserve(fc.args.size());
    for (const auto& arg : fc.args) {
        arg_results.push_back(evaluateInternal(arg, input));
    }

    std::vector<const Column*> arg_cols;
    arg_cols.reserve(arg_results.size());
    for (const auto& ar : arg_results)
        arg_cols.push_back(ar.column);

    if (fc.func_def->fn)
        fc.func_def->fn(arg_cols, result, count, eval_ctx_);
}

} // namespace compute
} // namespace eugraph
