#include "query/evaluator/vectorized_evaluator.hpp"

namespace eugraph {
namespace compute {

void VectorizedEvaluator::evalCase(const binder::BoundCase& case_expr, const DataChunk& input, Column& result,
                                   size_t count) {
    // Evaluate subject once if present (simple CASE: CASE expr WHEN val THEN ...)
    EvalResult subject_eval;
    if (case_expr.subject) {
        subject_eval = evaluateInternal(*case_expr.subject, input);
    }

    // Evaluate all WHEN conditions and THEN expressions
    struct WhenThenEval {
        EvalResult when_col;
        EvalResult then_col;
    };
    std::vector<WhenThenEval> branches;
    branches.reserve(case_expr.when_thens.size());
    for (const auto& [when_expr, then_expr] : case_expr.when_thens) {
        WhenThenEval wte;
        wte.when_col = evaluateInternal(when_expr, input);
        wte.then_col = evaluateInternal(then_expr, input);
        branches.push_back(std::move(wte));
    }

    // Evaluate ELSE once if present
    EvalResult else_eval;
    if (case_expr.else_expr) {
        else_eval = evaluateInternal(*case_expr.else_expr, input);
    }

    for (size_t i = 0; i < count; ++i) {
        bool matched = false;

        for (const auto& br : branches) {
            if (!br.when_col.column || !br.then_col.column)
                continue;

            bool condition_met = false;
            if (subject_eval.column) {
                // Simple CASE: compare subject == when_value
                Value sv = subject_eval.column->getValue(i);
                Value wv = br.when_col.column->getValue(i);
                condition_met = !eugraph::isNull(sv) && !eugraph::isNull(wv) && sv == wv;
            } else {
                // Searched CASE: when_value is a boolean condition
                Value wv = br.when_col.column->getValue(i);
                condition_met = std::holds_alternative<bool>(wv) && std::get<bool>(wv);
            }

            if (condition_met) {
                result.setValue(i, br.then_col.column->getValue(i));
                matched = true;
                break;
            }
        }

        if (!matched) {
            if (else_eval.column) {
                result.setValue(i, else_eval.column->getValue(i));
            } else {
                result.setNull(i);
            }
        }
    }
}

} // namespace compute
} // namespace eugraph
