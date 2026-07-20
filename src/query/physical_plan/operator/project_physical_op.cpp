#include "query/physical_plan/operator/project_physical_op.hpp"

namespace eugraph {
namespace compute {

std::string ProjectPhysicalOp::toString() const {
    std::string s;
    for (size_t i = 0; i < items_.size(); i++) {
        if (i > 0)
            s += ", ";
        s += items_[i].name;
    }
    return "Project(items=[" + s + "])";
}

folly::coro::AsyncGenerator<DataChunk> ProjectPhysicalOp::executeChunk() {
    auto child_gen = child_->executeChunk();

    // Empty projection (no RETURN): drain child for side effects, produce 0 rows
    if (items_.empty()) {
        while (auto chunk = co_await child_gen.next()) {}
        co_return;
    }

    while (auto chunk = co_await child_gen.next()) {
        size_t n = chunk->numRows();

        DataChunk output;
        output.columns.reserve(items_.size());

        VectorizedEvaluator evaluator(eval_ctx_);
        for (const auto& item : items_) {
            auto col_kind = binder::getBoundExprType(item.expr).kind;
            // Multi-candidate property access may produce ListValue at runtime,
            // so the column must be ANY to hold it.
            if (auto* prop = std::get_if<std::unique_ptr<binder::BoundPropertyRef>>(&item.expr)) {
                if ((*prop)->candidates.size() > 1)
                    col_kind = binder::BoundTypeKind::ANY;
            }
            // For BoundColumnRef, the binder type may be the semantic form
            // (e.g. VERTEX) while the source column actually holds the topology
            // form (e.g. VERTEX_REF). Use the source column's type so setValue
            // doesn't silently drop the value.
            if (auto* ref = std::get_if<binder::BoundColumnRef>(&item.expr)) {
                if (ref->column_index < chunk->columns.size()) {
                    col_kind = chunk->columns[ref->column_index].type;
                }
            }
            auto& out_col = output.addColumn(col_kind);
            out_col.reserve(n);
            evaluator.evaluate(item.expr, *chunk, out_col);
        }
        output.count = n;
        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
