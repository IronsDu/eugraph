#include "query/physical_plan/operator/correlated_source_physical_op.hpp"

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> CorrelatedSourcePhysicalOp::executeChunk() {
    DataChunk chunk;
    for (size_t i = 0; i < types_.size(); ++i) {
        auto& col = chunk.addColumn(types_[i].kind);
        col.reserve(1);
        col.setValue(0, values_[i]);
    }
    chunk.count = 1;
    chunk.sel = SelectionVector::identity(1);
    co_yield std::move(chunk);
}

} // namespace compute
} // namespace eugraph
