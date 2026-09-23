#include "query/physical_plan/operator/index_scan_values_physical_op.hpp"

namespace eugraph {
namespace compute {

folly::coro::AsyncGenerator<DataChunk> IndexScanValuesPhysicalOp::executeChunk() {
    if (!values_)
        co_return;

    // 多个索引值的匹配结果是直接拼接（没有去重语义），所以不必先收集到 vector：
    // 逐个索引值流式转发，边读边吐，内存只有一个批。
    DataChunk chunk;
    auto resetChunk = [&] {
        chunk = DataChunk{};
        chunk.count = 0;
        chunk.addColumn(binder::BoundTypeKind::VERTEX_REF);
        chunk.columns[0].reserve(DataChunk::DEFAULT_CAPACITY);
    };
    resetChunk();

    for (const auto& value : *values_) {
        std::vector<PropertyValue> one{value};
        auto gen = cancellable(store_.scanVerticesByIndexId(index_id_, one));
        while (auto batch = co_await gen.next()) {
            for (VertexId vid : *batch) {
                chunk.columns[0].setValue(chunk.count, Value(VertexRef(vid)));
                ++chunk.count;
                if (chunk.count >= DataChunk::DEFAULT_CAPACITY) {
                    chunk.sel = SelectionVector::identity(chunk.count);
                    co_yield std::move(chunk);
                    resetChunk();
                }
            }
        }
    }
    if (chunk.count > 0) {
        chunk.sel = SelectionVector::identity(chunk.count);
        co_yield std::move(chunk);
    }
}
} // namespace compute
} // namespace eugraph
