#include "query/physical_plan/operator/singleton_physical_op.hpp"

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> SingletonPhysicalOp::executeChunk() {
    DataChunk chunk;
    chunk.setSchema(output_types_);
    chunk.count = 1;
    chunk.sel = SelectionVector::identity(1);
    co_yield std::move(chunk);
}

} // namespace compute
} // namespace eugraph
