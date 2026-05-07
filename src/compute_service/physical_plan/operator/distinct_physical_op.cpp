#include "compute_service/physical_plan/operator/distinct_physical_op.hpp"
#include "compute_service/executor/row.hpp"

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> DistinctPhysicalOp::executeChunk() {
    std::unordered_set<Row, RowHash, RowEqual> seen;

    auto child_gen = child_->executeChunk();
    while (auto chunk = co_await child_gen.next()) {
        size_t n = chunk->numRows();
        std::vector<Row> rows = chunk->toRows();

        SelectionVector new_sel;
        new_sel.is_identity = false;
        new_sel.indices.reserve(n);

        for (size_t i = 0; i < n; ++i) {
            if (seen.insert(rows[i]).second) {
                new_sel.indices.push_back(static_cast<uint32_t>(i));
            }
        }
        new_sel.count = new_sel.indices.size();

        if (new_sel.count == 0) continue;

        DataChunk output;
        for (auto& col : chunk->columns) {
            if ((col.form == VectorForm::FLAT || col.form == VectorForm::DICTIONARY) && col.buffer) {
                SelectionVector mapped;
                mapped.is_identity = false;
                mapped.indices.reserve(new_sel.count);
                for (size_t i = 0; i < new_sel.count; ++i) {
                    uint32_t physical = new_sel[i];
                    if (col.form == VectorForm::DICTIONARY) {
                        mapped.indices.push_back(col.dict_sel[physical]);
                    } else {
                        mapped.indices.push_back(physical);
                    }
                }
                mapped.count = new_sel.count;
                output.columns.push_back(Column::dict(col.buffer, mapped));
            } else if (col.form == VectorForm::CONSTANT) {
                output.columns.push_back(Column::constant(col.constant_value));
            } else {
                output.columns.push_back(Column(col.type));
            }
        }
        output.count = new_sel.count;
        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
