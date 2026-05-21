#include "query/evaluator/vectorized_evaluator.hpp"

namespace eugraph {
namespace compute {

void VectorizedEvaluator::evalFunctionCall(const binder::BoundFunctionCall& fc, const DataChunk& input, Column& result,
                                           size_t count) {
    if (!fc.func_def)
        return;

    // Evaluate all arguments first
    std::vector<EvalResult> arg_results;
    arg_results.reserve(fc.args.size());
    for (const auto& arg : fc.args) {
        arg_results.push_back(evaluateInternal(arg, input));
    }

    // Use batch function if available
    if (fc.func_def->batch_scalar_fn) {
        std::vector<const Column*> arg_cols;
        arg_cols.reserve(arg_results.size());
        for (const auto& ar : arg_results) {
            arg_cols.push_back(ar.column);
        }
        fc.func_def->batch_scalar_fn(arg_cols, result, count, eval_ctx_);
        return;
    }

    // Fallback: per-row scalar function call
    if (!fc.func_def->scalar_fn)
        return;

    for (size_t i = 0; i < count; ++i) {
        std::vector<Value> args;
        args.reserve(arg_results.size());
        for (const auto& ar : arg_results) {
            if (ar.column) {
                args.push_back(ar.column->getValue(i));
            } else {
                args.emplace_back();
            }
        }
        result.setValue(i, fc.func_def->scalar_fn(args, eval_ctx_));
    }
}

} // namespace compute
} // namespace eugraph
