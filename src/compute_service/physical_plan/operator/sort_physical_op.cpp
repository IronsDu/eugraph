#include "compute_service/physical_plan/operator/sort_physical_op.hpp"
#include "compute_service/executor/row.hpp"
#include "compute_service/parser/ast.hpp"

#include <algorithm>

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<RowBatch> SortPhysicalOp::execute() {
    std::vector<Row> all_rows;
    auto child_gen = child_->execute();
    while (auto batch = co_await child_gen.next()) {
        for (auto& row : batch->rows) {
            all_rows.push_back(std::move(row));
        }
    }

    std::sort(all_rows.begin(), all_rows.end(), [this](const Row& a, const Row& b) {
        for (const auto& item : sort_items_) {
            Value va = evaluator_.evaluate(item.expr, a, input_schema_);
            Value vb = evaluator_.evaluate(item.expr, b, input_schema_);

            bool less = false, greater = false;
            std::visit(
                [&less, &greater](const auto& la, const auto& lb) {
                    using A = std::decay_t<decltype(la)>;
                    using B = std::decay_t<decltype(lb)>;
                    if constexpr ((std::is_same_v<A, int64_t> && std::is_same_v<B, int64_t>) ||
                                  (std::is_same_v<A, double> && std::is_same_v<B, double>)) {
                        less = la < lb;
                        greater = la > lb;
                    } else if constexpr (std::is_same_v<A, std::string> && std::is_same_v<B, std::string>) {
                        less = la < lb;
                        greater = la > lb;
                    }
                },
                va, vb);

            if (item.direction == cypher::OrderBy::Direction::DESC) {
                std::swap(less, greater);
            }
            if (less)
                return true;
            if (greater)
                return false;
        }
        return false;
    });

    RowBatch output;
    for (auto& row : all_rows) {
        output.push_back(std::move(row));
        if (output.size() >= RowBatch::CAPACITY) {
            co_yield std::move(output);
            output.clear();
        }
    }
    if (!output.empty()) {
        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
