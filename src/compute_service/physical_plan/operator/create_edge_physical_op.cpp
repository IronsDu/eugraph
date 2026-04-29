#include "compute_service/physical_plan/operator/create_edge_physical_op.hpp"
#include "common/types/graph_types.hpp"
#include "compute_service/executor/row.hpp"

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<RowBatch> CreateEdgePhysicalOp::execute() {
    if (child_) {
        auto child_gen = child_->execute();
        while (auto batch = co_await child_gen.next()) {
            // Discard child output
        }
    }

    bool ok = co_await store_.insertEdge(assigned_eid_, src_id_, dst_id_, label_id_,
                                         0, // seq = 0 for first edge between this pair
                                         {} // no properties in Phase 1
    );

    RowBatch output;
    if (ok) {
        Row row;
        row.push_back(Value(static_cast<int64_t>(assigned_eid_)));
        output.push_back(std::move(row));
    }
    if (!output.empty()) {
        co_yield std::move(output);
    }
}

} // namespace compute
} // namespace eugraph
