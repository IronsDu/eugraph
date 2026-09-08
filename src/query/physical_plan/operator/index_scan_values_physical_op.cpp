#include "query/physical_plan/operator/index_scan_values_physical_op.hpp"

#include <spdlog/spdlog.h>

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> IndexScanValuesPhysicalOp::executeChunk() {
    if (!values_)
        co_return;

    std::vector<VertexId> collected;
    for (const auto& value : *values_) {
        std::vector<PropertyValue> one{value};
        auto gen = store_.scanVerticesByIndexId(index_id_, one);
        while (auto batch = co_await gen.next()) {
            for (VertexId vid : *batch)
                collected.push_back(vid);
        }
    }

    constexpr size_t BATCH = 1024;
    for (size_t i = 0; i < collected.size(); i += BATCH) {
        size_t n = std::min(BATCH, collected.size() - i);
        DataChunk chunk;
        chunk.count = n;
        chunk.addColumn(binder::BoundTypeKind::VERTEX_REF);
        chunk.columns[0].reserve(n);
        for (size_t j = 0; j < n; ++j)
            chunk.columns[0].setValue(j, Value(VertexRef(collected[i + j])));
        chunk.sel = SelectionVector::identity(n);
        co_yield std::move(chunk);
    }
}

} // namespace compute
} // namespace eugraph
