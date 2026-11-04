#include "query/dataset/cell_access.hpp"
#include "query/evaluator/expression_evaluator.hpp"

namespace eugraph {
namespace compute {

void ExpressionEvaluator::evalCase(const binder::BoundCase& case_expr, const DataChunk& input, Column& result,
                                   size_t count) {
    // Evaluate subject once if present (simple CASE: CASE expr WHEN val THEN ...)
    EvalResult subject_eval;
    if (case_expr.subject) {
        subject_eval = evaluateInternal(*case_expr.subject, input);
    }

    // Evaluate all WHEN conditions and THEN expressions once per chunk.
    auto& s = scratch();
    auto& branches = s.case_branches;
    branches.clear();
    branches.reserve(case_expr.when_thens.size());
    for (const auto& [when_expr, then_expr] : case_expr.when_thens) {
        CaseBranchEval wte;
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
                // Simple CASE: compare subject == when_value. The typed helper
                // avoids two Value materialisations per branch/row for the
                // common scalar and string cases.
                condition_met = columnCellEquals(*subject_eval.column, i, *br.when_col.column, i);
            } else if (br.when_col.column->type == binder::BoundTypeKind::BOOL) {
                // Searched CASE with a boolean WHEN: no Value round-trip.
                condition_met = !br.when_col.column->isNull(i) && br.when_col.column->getBool(i);
            } else {
                // Non-boolean WHEN is a type error at bind time; keep the old
                // Value fallback for anything the binder lets through.
                Value wv = br.when_col.column->getValue(i);
                condition_met = std::holds_alternative<bool>(wv) && std::get<bool>(wv);
            }

            if (condition_met) {
                copyColumnCell(*br.then_col.column, i, result, i);
                matched = true;
                break;
            }
        }

        if (!matched) {
            if (else_eval.column) {
                copyColumnCell(*else_eval.column, i, result, i);
            } else {
                result.setNull(i);
            }
        }
    }
}

} // namespace compute
} // namespace eugraph
